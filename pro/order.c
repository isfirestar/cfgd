#include "order.h"

#include <assert.h>
#include <stdio.h>
#include <stdarg.h>

#include "ifos.h"
#include "atom.h"

#include "runtime.h"
#include "virfs.h"
#include "interact.h"
#include "refos.h"
#include "conf.h"

#define RESP_BUFF_SIZE    		(5 << 20)	/* 5MByte space to save the jesspro_response packet */

struct jesspro_server
{
	struct pro_work_thread 	*pro;
	struct pro_work_thread 	*dpc;
	int pending_fsync;
};
struct jesspro_server *__pro_server = NULL;

#define pro_min(a, b)  ((a) < (b) ? (a) : (b))

static int pro_send_response(const struct jesspro_task *task)
{
	struct pro_work_thread *thread;
	int n;

	if (!task) {
		return -1;
	}

	thread = task->thread;
	assert(thread);
	n = 0;
	if (thread->offset > 0 && task->link > 0) {
		n = jnet_write_response(task->link, (const unsigned char *)thread->response, pro_min(thread->offset, RESP_BUFF_SIZE));
	}

	thread->offset = 0;
	thread->response[0] = 0;
	return n;
}

int pro_append_response(const struct jesspro_task *task, const char *format, ...)
{
	struct pro_work_thread *thread;
	va_list ap;

	if (!task || !format) {
		return -1;
	}

	thread = task->thread;
	assert(thread);

	if (thread->offset >= RESP_BUFF_SIZE) {
		rt_alert("response offset out of range:%d", thread->offset);
		return 0;
	}

    va_start(ap, format);
    thread->offset += os_vsnprintf(&thread->response[thread->offset], (RESP_BUFF_SIZE - thread->offset), format, ap);

    return thread->offset;
}

int pro_append_binary_response(const struct jesspro_task *task, const unsigned char *data, int size)
{
	struct pro_work_thread *thread;

	if (!task || !data || size <= 0) {
		return -1;
	}

	thread = task->thread;
	assert(thread);

	if (thread->offset + size >= RESP_BUFF_SIZE ) {
		return -1;
	}

	memcpy(&thread->response[thread->offset], data, size);
	thread->offset += size;
    return thread->offset;
}

static void pro_release_task(struct jesspro_task *task)
{
	JRP *jrp;
	struct list_head *pos, *n;

	if (!task) {
		return;
	}

	pos = NULL;
	n = NULL;
	list_for_each_safe(pos, n, &task->head_jrp_option) {
		jrp = containing_record(pos, JRP, entry);
		list_del_init(pos);
		free(jrp);
	}

	pos = NULL;
	n = NULL;
	list_for_each_safe(pos, n, &task->head_jrp_payload) {
		jrp = containing_record(pos, JRP, entry);
		list_del_init(pos);
		free(jrp);
	}

	rt_release_strptr(task->origin);  /* this is the original request string duplicate in @pro_allocate_task */
	free(task);
}

static int pro_complete_task(struct jesspro_task *task, const char *status)
{
	char resp[MAX_REP_SIZE];
	int len;
	int n;

	if (!task || !status) {
		return -1;
	}

	n = 0;
	len = 0;
	if (task->title.cstrptr && task->link > 0) {
		n = os_snprintf(&resp[len], sizeof(resp) - len, "%s"CRLF, task->title.cstrptr);
		if (len + n >= sizeof(resp)) {
			return -1;
		}
		len += n;

		/* build the ack options */
		if (0 != task->dupopt[0]) {
			n = os_snprintf(&resp[len], sizeof(resp) - len, "%s"JESS_REQ_DUB_END, task->dupopt);
			if (len + n >= sizeof(resp)) {
				return -1;
			}
			len += n;
		}
		n = os_snprintf(&resp[len], sizeof(resp) - len, "%s"CRLF, status);
		if (len + n >= sizeof(resp)) {
			return -1;
		}
		len += n;

		n = jnet_write_response(task->link, (const unsigned char *)resp, len);
	}
	pro_release_task(task);
	return n;
}

static int pro_complete_task_as_fault(struct jesspro_task *task)
{
	return pro_complete_task(task, "fatal");
}

#if 0
static int pro_complete_task_as_success(struct jesspro_task *task)
{
	return pro_complete_task(task, "ok");
}
#endif

static int pro_set_handler( struct jesspro_task *task )
{
	const char *title;

	if (!task) {
		return -1;
	}

	/* meansure the len of title large than or equal to 3 */
	title = task->title.cstrptr;
	if (title[1] == 0 || title[2] == 0) {
		return -1;
	}

	task->thread = NULL;
	/* a simple and easy algorithm for fast string matching */
	switch (title[0]) {
		case 'c':
			if ( 0 == strcmp(title, "contain") ) {
				if ( task->size_jrp_payload <= 0 ) { /* contain query request canbe and MUST speicy at less 1 data item */
					rt_alert("illegal JRP size:%d associate to command [contain]", task->size_jrp_payload);
					return -1;
				}
				task->task_handler = &pro_variable_contain;
				task->thread = __pro_server->pro;
				break;
			}
			if (0 == strcmp(title, "checkpoint") ) {
				if ( 0 != task->size_jrp_payload ) { /* create checkpoint should NOT specify any data */
					rt_alert("illegal JRP size:%d associate to command [checkpoint]", task->size_jrp_payload);
					return -1;
				}
				task->task_handler = &pro_create_checkpoint;
				task->thread = __pro_server->dpc;
				break;
			}
			break;
		case 'f':
			if (0 == strcmp(title, "file") ) {
				if ( task->size_jrp_payload <= 0 ) { /* file query request canbe and MUST speicy at less 1 data item */
					rt_alert("illegal JRP size:%d associate to command [file]", task->size_jrp_payload);
					return -1;
				}
				task->task_handler = &pro_variable_read_file;
				task->thread = __pro_server->pro;
				break;
			}
			if (0 == strcmp(title, "fsync") ) {
				if ( 0 != task->size_jrp_payload ) {
					rt_alert("illegal JRP size:%d associate to command [fsync]", task->size_jrp_payload);
					return -1;
				}
				if ( task->link != INVALID_HTCPLINK) {
					rt_alert("fsync request cannot initiate from user.");
					return -1;
				}
				/* more than one fsync request pending in queue are not necessary */
				if (atom_addone(&__pro_server->pending_fsync) > 1) {
					atom_subone(&__pro_server->pending_fsync);
				} else {
					task->task_handler = &fs_fsync;
					task->thread = __pro_server->dpc;
				}
				break;
			}
			break;
		case 'k':
			if ( 0 == strcmp(title, "keepalive") ) {
				task->task_handler = NULL;
				task->thread = __pro_server->pro;
				break;
			}
			break;
		case 'm':
			if (0 == strcmp(title, "mkdir") ) {
				if ( task->size_jrp_payload <= 0 ) { /* directory create request canbe and MUST speicy at less 1 data item */
					rt_alert("illegal JRP size:%d associate to command [mkdir]", task->size_jrp_payload);
					return -1;
				}
				task->task_handler = &pro_mkdir;
				task->thread = __pro_server->pro;
				break;
			}
			break;
		case 'r':
			if (0 == strcmp(title, "rbin") ) {
				if ( 0 != task->size_jrp_payload ) { /* binary read request MUST NOT speicy any data item */
					rt_alert("illegal JRP size:%d associate to command [rbin]", task->size_jrp_payload);
					return -1;
				}
				task->task_handler = &pro_binary_read;
				task->thread = __pro_server->dpc;
				break;
			}
			if (0 == strcmp(title, "read") ) {
				if ( task->size_jrp_payload <= 0 ) { /* variable read request canbe and MUST specify at less 1 data item */
					rt_alert("illegal JRP size:%d associate to command [read]", task->size_jrp_payload);
					return -1;
				}
				task->task_handler = &pro_vairable_read;
				task->thread = __pro_server->pro;
				break;
			}
			if (0 == strcmp(title, "recover")) {
				if ( 1 != task->size_jrp_payload ) { /* recover to checkpoint request MUST specify only 1 data item */
					rt_alert("illegal JRP size:%d associate to command [recover]", task->size_jrp_payload);
					return -1;
				}
				task->task_handler = &fs_recover_checkpoint;
				task->thread = __pro_server->dpc;
				break;
			}
			if (0 == strcmp(title, "recover2")) { /* this is schedule by fs_recover_checkpoint. */
				task->task_handler = &pro_recover_checkpoint;
				task->thread = __pro_server->pro;
				strcpy(task->title.strptr, "recover");	/* for response reason, the title MUST change to 'recover'. */
				break;
			}
			if (0 == strcmp(title, "rmdir") ) {
				if ( task->size_jrp_payload <= 0 ) {  /* remove dir request canbe and MUST specify at less 1 data item */
					rt_alert("illegal JRP size:%d associate to command [rmdir]", task->size_jrp_payload);
					return -1;
				}
				task->task_handler = &pro_rmdir;
				task->thread = __pro_server->pro;
				break;
			}
			break;
		case 's':
			if (0 == strcmp(title, "scope") ) {
				if ( task->size_jrp_payload <= 0 ) {  /* scope read request canbe and MUST specify at less 1 data item */
					rt_alert("illegal JRP size:%d associate to command [scope]", task->size_jrp_payload);
					return -1;
				}
				task->task_handler = &pro_variable_scope;
				task->thread = __pro_server->pro;
				break;
			}
			if (0 == strcmp(title, "subscribe") ) {
				if ( task->size_jrp_payload <= 0 ) { /* subscribe canbe and MUST specify at less 1 data item */
					rt_alert("illegal JRP size:%d associate to command [subscribe]", task->size_jrp_payload);
					return -1;
				}
				task->task_handler = &jnet_subscribe;
				task->thread = __pro_server->pro;
				break;
			}
			if (0 == strcmp(title, "sync") ) {
				if ( 0 != task->size_jrp_payload ) { /* binary sync request MUST NOT speicy any data item */
					rt_alert("illegal JRP size:%d associate to command [sync]", task->size_jrp_payload);
					return -1;
				}
				task->task_handler = &fs_sync;
				task->thread = __pro_server->pro;
				break;
			}
			break;
		case 'u':
			if (0 == strcmp(title, "unsubscribe") ) {
				if ( task->size_jrp_payload <= 0 ) { /* unsubscribe canbe and MUST specify at less 1 data item */
					rt_alert("illegal JRP size:%d associate to command [unsubscribe]", task->size_jrp_payload);
					return -1;
				}
				task->task_handler = &jnet_unsubscribe;
				task->thread = __pro_server->pro;
				break;
			}
			break;
		case 'v':
			if ( 0 == strcmp(title, "version") ) {
				if ( 0 != task->size_jrp_payload ) {
					rt_alert("illegal JRP size:%d associate to command [version]", task->size_jrp_payload);
					return -1;
				}
				task->task_handler = &pro_query_version;
				task->thread = __pro_server->pro;
				break;
			}
			break;
		case 'w':
			if (0 == strcmp(title, "wbin") ) {
				if ( 1 != task->size_jrp_payload ) { /* binary write request MUST specify only 1 data item */
					rt_alert("illegal JRP size:%d associate to command [wbin]", task->size_jrp_payload);
					return -1;
				}
				task->task_handler = &pro_binary_write;
				task->thread = __pro_server->dpc;
				break;
			}
			if (0 == strcmp(title, "write") ) {
				if ( task->size_jrp_payload <= 0 ) { /* write command canbe and MUST specify at less 1 data item */
					rt_alert("illegal JRP size:%d associate to command [write]", task->size_jrp_payload);
					return -1;
				}
				task->task_handler = &pro_variable_write;
				task->thread = __pro_server->pro;
				break;
			}
			break;
		default:
			break;
	}

	if (NULL == task->thread) {
		rt_fatal("can not recognize command title:%s", title);
		return -1;
	}

	return 0;
}

static void pro_build_binary_jrp(const CSTRPTR *payload, const void *taskptr, struct list_head *head)
{
	JRP *jrp;

	jrp = (JRP *)malloc(sizeof(JRP));
	assert(jrp);
	rt_init_jrp(jrp, taskptr);
	rt_copy_strptr(&jrp->valuestring, payload);
	list_add_tail(&jrp->entry, head);
}

static int pro_allocate_task(HTCPLINK link,
	const CSTRPTR *origin,const CSTRPTR *title, const CSTRPTR *option, const CSTRPTR *payload, struct jesspro_task **ptask)
{
	struct jesspro_task *task;

	assert(origin && title && option && payload && ptask);

	/* allocate the task memory and duplicate task data from network request */
	task = (struct jesspro_task *)malloc(sizeof(struct jesspro_task));
	assert(task);
	memset(task, 0, sizeof(struct jesspro_task));

	/* initialization task fields */
	task->link = link;
	rt_copy_strptr(&task->origin, origin);
	rt_copy_strptr(&task->title, title);
	rt_copy_strptr(&task->payload, payload);
	task->size_jrp_payload = 0;
	INIT_LIST_HEAD(&task->head_jrp_payload);
	task->size_jrp_option = 0;
	INIT_LIST_HEAD(&task->head_jrp_option);
	INIT_LIST_HEAD(&task->entry);
	task->task_handler = NULL;

	/* fill the output pointer */
	*ptask = task;

	/* parse the option list */
	if (rt_effective_strptr(option) ) {
		/* save option origin context in task space */
		strcpy(task->dupopt, option->cstrptr);
		/* separate the opiton context and analyze to list of JRPs */
		if ( (task->size_jrp_option = rt_build_jrp(option->cstrptr, task, &task->head_jrp_option)) < 0 ) {
			rt_fatal("fails build JRP for options");
			return -1;
		}
	}

	/* parse the data request list */
	if (0 == strcmp("wbin", task->title.cstrptr)) {
		if (!task->payload.cblkptr) {
			rt_fatal("binary write request without effective payload.");
			return -1;
		}
		pro_build_binary_jrp(&task->payload, task, &task->head_jrp_payload);
		task->size_jrp_payload = 1;
	} else {
		/* for some request link checkpoing/keepalive/sync/fsync,
			payload are not necessary */
		if ( rt_effective_strptr( &task->payload) ) {
			if ( (task->size_jrp_payload = rt_build_jrp(task->payload.cstrptr, task, &task->head_jrp_payload)) < 0) {
				rt_fatal("fails build JRP for payload");
				return -1;  /* task memory already been released after @pro_complete_task_as_fault call */
			}
		}
	}

	/* parse to get the command splits,
		the worker function canbe determine now, if protocol not support
		and/or this task can complete now, an yother step are not necessary,
		in this case, memory of this task should not free immediately, calling jesspro_thread can use then to send to error response packet to requester */
	return pro_set_handler(task);
}

int pro_receive_data(HTCPLINK link,
	const CSTRPTR *origin,const CSTRPTR *title, const CSTRPTR *option, const CSTRPTR *payload)
{
	struct pro_work_thread *thread;
	struct jesspro_task *ptask;

	assert(__pro_server && title);

	ptask = NULL;
	if (pro_allocate_task(link, origin, title, option, payload, &ptask) < 0) {
		if (ptask) {
			pro_complete_task_as_fault(ptask);
		}
		return -1;
	}

	assert(ptask);

	/* the worker thread pointer have been specified, add task and dispatch it to work schedular */
	thread = ptask->thread;
	if (thread) {
		lwp_mutex_lock(&thread->mutex);
		if (thread->task_pending < MAX_PENDING_TASK ) {
			list_add_tail(&ptask->entry, &thread->task_head);
			++thread->task_pending;
			ptask = NULL;
		}
		lwp_mutex_unlock(&thread->mutex);
		lwp_event_awaken(&thread->notification);
	}

	/* the size of queue which contain pending tasks is over limit.
		this task will be drop, but packet contain fatal fail message can send to requester  */
	if (ptask) {
		rt_alert("task queue in thread context [%s] size over limite.", thread->name);
		pro_complete_task_as_fault(ptask);
		return -1;
	}
	return 0;
}

static int pro_execute_task(struct jesspro_task *task)
{
	JRP tempjrp, *jrp;
	struct list_head *pos, *n;
	struct pro_work_thread *thread;

	thread = task->thread;
	assert(thread);

	/* the jesspro_response title always the same as request title */
	thread->offset = 0;
	thread->response[0] = 0;
	pro_append_response(task, "%s"CRLF, task->title.cstrptr);

	/* all options in request packet MUST append to response */
	if (0 != task->dupopt[0]) {
		pro_append_response(task, "%s"JESS_REQ_DUB_END, task->dupopt);
	}

	/* execute task by JRP object dispatch */
	if (list_empty(&task->head_jrp_payload)) {
		assert(0 == task->size_jrp_payload);
		if (task->task_handler) {
			/* allow no jrp-data in process, for example:checkpoint,
				in these cases, take a temp JRP object which contain the task pointer pass to event handler */
			rt_init_jrp(&tempjrp, task);
			task->task_handler(&tempjrp);
		} else {
			/* allow no handler function, for example:keepalive */
			pro_append_response(task, JESS_OK);
		}
	} else {
		assert(task->size_jrp_payload > 0);
		if (task->task_handler) {
			pos = NULL;
			n = NULL;
			list_for_each_safe(pos, n, &task->head_jrp_payload) {
				jrp = containing_record(pos, JRP, entry);
				task->task_handler(jrp);
			}
		} else {
			pro_append_response(task, JESS_FATAL);
		}
	}

	/* real jesspro_response packet sent */
	return pro_send_response(task);
}

static void pro_rotate_task(struct pro_work_thread *thread)
{
	struct list_head *pos, *n;
	struct jesspro_task *task;

	if (!thread) {
		return;
	}

	while (1) {
		task = NULL;
		pos = NULL;
		n = NULL;
		lwp_mutex_lock(&thread->mutex);
		list_for_each_safe(pos, n, &thread->task_head) {
			task = containing_record(pos, struct jesspro_task, entry);
			list_del_init(pos);
			--thread->task_pending;
			break;
		}
		lwp_mutex_unlock(&thread->mutex);

		if (!task) {
			break;
		}

		INIT_LIST_HEAD(&task->entry);

		/* complete the task */
		/* the validity of the protocol has been verified.*/
		pro_execute_task(task);
		pro_release_task(task);
	}
}

static void *pro_work_routine(void *argv)
{
	struct pro_work_thread *thread;
	nsp_status_t status;

	thread = (struct pro_work_thread *)argv;

	rt_echo("working thread:%s startup.", thread->name);

	/* record the thread-id in worker object */
	thread->LWP = ifos_gettid();
	while(1) {
		status = lwp_event_wait(&thread->notification, 1000);
		if ( !thread->exit && NSP_SUCCESS_OR_ERROR_EQUAL(status, ETIMEDOUT) ) {
			pro_rotate_task( thread );
		} else {
			break;
		}
	}

	rt_echo("working thread:%s stop.", thread->name);
	return NULL;
}

static struct pro_work_thread *pro_create_work_thread(const char *name, int affinity)
{
	struct pro_work_thread *thread;
	int e;

	thread = (struct pro_work_thread *)malloc(sizeof(struct pro_work_thread) + RESP_BUFF_SIZE);
	if (!thread) {
		return NULL;
	}

	thread->exit = NO;
	if (name) {
		strcpy(thread->name, name);
	}
	lwp_mutex_init(&thread->mutex, 1);
	INIT_LIST_HEAD(&thread->task_head);
	thread->task_pending = 0;
	lwp_event_init(&thread->notification, LWPEC_SYNC);
	if (lwp_create(&thread->thread, 0, &pro_work_routine, (void *)thread) < 0) {
		free(thread);
		thread = NULL;
	}

	if (affinity >= 0) {
		e = os_set_affinity(&thread->thread, affinity);
		if (e < 0) {
	        rt_alert("failed binding navigation thread on CPU-%d, error=%d", affinity, e);
	    }else{
	        rt_echo("success binding navigation thread on CPU-%d", affinity);
	    }
	}

	return thread;
}

int pro_create()
{
	int na;

	if (__pro_server) {
		return 0;
	}

	__pro_server = (struct jesspro_server *)malloc(sizeof(struct jesspro_server));
	if (!__pro_server) {
		return -ENOMEM;
	}
	memset(__pro_server, 0, sizeof(struct jesspro_server));

	na = -1;
	os_query_conf_integer("pro-affinity", &na);
	__pro_server->pro = pro_create_work_thread("PRO", na);

	na = -1;
	os_query_conf_integer("dpc-affinity", &na);
	__pro_server->dpc = pro_create_work_thread("DPC", na);

	if (!__pro_server->pro || !__pro_server->dpc) {
		pro_release();
		return -1;
	}

	return 0;
}

static void pro_join(struct pro_work_thread *thread)
{
	void *exit;

	if (!thread) {
		return;
	}

	lwp_join(&thread->thread, &exit);
	pro_rotate_task(thread);
	lwp_event_uninit(&thread->notification);
	lwp_mutex_uninit(&thread->mutex);
}

void pro_release()
{
	if (__pro_server) {
		if (__pro_server->pro) {
			__pro_server->pro->exit = YES;
			pro_join(__pro_server->pro);
			free(__pro_server->pro);
			__pro_server->pro = NULL;
		}

		if (__pro_server->dpc) {
			__pro_server->dpc->exit = YES;
			pro_join(__pro_server->dpc);
			free(__pro_server->dpc);
			__pro_server->dpc = NULL;
		}

		free(__pro_server);
		__pro_server = NULL;
	}
}

int pro_append_request(const char *title, const JRP *jrp, const char *payload)
{
	struct jesspro_task *task;
	HTCPLINK link;
	rt_declare_strptr(request);
	rt_declare_strptr(passpayload);
	rt_declare_strptr(passoption);
	rt_declare_strptr(passtitle);
	int offset;
	char *requestptr;
	int maxreq;

	if (!title) {
		return -1;
	}

	link = INVALID_HTCPLINK;
	offset = 0;
	maxreq = MAX_TITLE_SIZE + MAX_OPT_SIZE + MAX_PAYLOAD_SIZE;
	rt_assign_strptr(&request, maxreq);
	requestptr = request.strptr;

	/* build title section */
	passtitle.strptr = &requestptr[offset];
	passtitle.len = os_snprintf(&requestptr[offset], maxreq - offset, "%s", title);
	if (passtitle.len >= MAX_TITLE_SIZE) {
		return -1;
	}
	offset += passtitle.len;
	requestptr[offset] = 0;
	offset++;

	if (jrp) {
		task = (struct jesspro_task *)jrp->taskptr;
		assert(task);
		link = task->link;

		/* set passoptions */
		passoption.strptr = &requestptr[offset];
		passoption.len = os_snprintf(&requestptr[offset], maxreq - offset, "%s", task->dupopt);
		if (passoption.len >= MAX_OPT_SIZE) {
			return -1;
		}
		offset += passoption.len;
		requestptr[offset] = 0;
		offset++;
	}

	if (payload) {
		passpayload.strptr = &requestptr[offset];
		passpayload.len = os_snprintf(&requestptr[offset], maxreq - offset, "%s", payload);
		if (passpayload.len >= MAX_PAYLOAD_SIZE) {
			return -1;
		}
		offset += passpayload.len;
		requestptr[offset] = 0;
		offset++;
	}

	return pro_receive_data( link, &request, &passtitle, &passoption, &passpayload);
}

int pro_complete_fsync()
{
	return atom_subone(&__pro_server->pending_fsync);
}

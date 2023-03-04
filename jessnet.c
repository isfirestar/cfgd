#include "jessnet.h"

#include "clist.h"
#include "naos.h"
#include "threading.h"
#include "clock.h"
#include "threading.h"
#include "atom.h"

#include "runtime.h"
#include "jesspro.h"
#include "tst.h"
#include "jessos.h"
#include "jessfs.h"

struct jnet_client_context
{
	HTCPLINK 			link;
	uint32_t 			ip;
	char				ipstr[16];
	uint16_t 			port;
	struct list_head	element; 	/* element of jnet_server::clients */
	struct list_head 	subcribes;	/* head of JRP, storage the subscribe items */
	char 				publish[MAX_PAYLOAD_SIZE];
	char			   *publishptr;
	int 				enabled;		/* after success login, this field exchange to non zero */
	uint64_t 			last_access_timepoint;	/* timepoint record for linked-timeout check */
	int 				window;
};

struct jnet_server
{
	HTCPLINK 				tcplink;
	HUDPLINK				udplink;
	HTCPLINK 				ipclink;
	tst_t 					tst;
	struct list_head		clients;	/* head for  jnet_client_context::element */
	lwp_mutex_t	list_client_locker;
#if NISV < 990u
	lwp_mutex_t  client_context_locker;
#endif
	lwp_t 		keepalive;
	nsp_boolean_t 				keepalive_exit;
	int 					qps;
};
static struct jnet_server __jnet_server;

/* maximum count of pending request pre link */
#define MAX_LINK_WINDOW		(20)

static int jnet_link_context_lock_and_get(HTCPLINK link, struct jnet_client_context **node)
{
#if NISV >= 990u
	if ( nis_cntl(link, NI_RISECTX, (void **)node) < 0 ) {
		return -1;
	}

	if (!node) {
		nis_cntl(link, NI_SINKCTX);
		return -1;
	}

	return 0;
#else
	lwp_mutex_lock(&__jnet_server.client_context_locker);
	if ( nis_cntl(link, NI_GETCTX, (void **)node) < 0 ) {
		lwp_mutex_unlock(&__jnet_server.client_context_locker);
		return -1;
	}

	if (!node) {
		lwp_mutex_unlock(&__jnet_server.client_context_locker);
		return -1;
	}

	/* keep the mutex locked */
	return 0;
#endif
}

static void jnet_link_context_unlock(HTCPLINK link)
{
#if NISV >= 990u
	nis_cntl(link, NI_SINKCTX);
#else
	lwp_mutex_unlock(&__jnet_server.client_context_locker);
#endif
}

static void jnet_on_accepted(HTCPLINK server, HTCPLINK client);
static void jnet_on_preclose(HTCPLINK link, void *context);
static void jnet_on_closed(HTCPLINK link );

static int jnet_simple_complete_request(HTCPLINK link, const char *title,	const char *option, const char *status)
{
	char response[MAX_REP_SIZE];
	int n;
	int len;

	if (!title || !status) {
		return -1;
	}

	len = 0;
	n = os_snprintf(&response[len], sizeof(response) - len, "%s"CRLF, title);
	if (n + len >= sizeof(response)) {
		return -1;
	}
	len += n;

	if (option) {
		n = os_snprintf(&response[len], sizeof(response) - len, "%s"JESS_REQ_DUB_END, option);
		if (n + len >= sizeof(response)) {
			return -1;
		}
		len += n;
	}
	n = os_snprintf(&response[len], sizeof(response) - len, "%s"CRLF, status);
	if (n + len >= sizeof(response)) {
		return -1;
	}
	len += n;

	return tcp_write(link, response, len, NULL);
}

static int jnet_on_receive_data(HTCPLINK link, const unsigned char *data, int size)
{
	struct jnet_client_context *node;
	rt_declare_strptr(title);
	rt_declare_strptr(option);
	rt_declare_strptr(request);
	rt_declare_strptr(payload);

	/* increase the QPS statistic data */
	atom_addone(&__jnet_server.qps);

	if (size <= 0 || size > MAX_REQ_SIZE || !data) {
		rt_alert("request length %d over limit.", size);
		return -1;
	}

	rt_assign_strptr(&request, size + 1);
	memcpy(request.strptr, data, size);
	request.strptr[size] = 0;

	if ( NULL == (payload.cblkptr = (const unsigned char *)rt_analyze_req_head(&request, &title, &option)) ) {
		return 0;
	}
	/* remain length MUST large than or equal to zero */
	assert( size >= (payload.cstrptr - request.cstrptr) );
	/* the length of payload */
	payload.len = size - (payload.cstrptr - request.cstrptr);
	if (0 == payload.len) {
		payload.strptr = NULL;
	}

	node = NULL;
	if ( jnet_link_context_lock_and_get(link, &node) < 0 ) {
		return -1;
	}

	/* save the last access time-point */
	node->last_access_timepoint = clock_monotonic();

	/* every link can pending 10 request most */
	if ( os_atomic_get(&node->window) >= MAX_LINK_WINDOW ) {
		rt_alert("link:%lld, request frequency over limit", link);
		/* unlock the context pointer holder */
		jnet_link_context_unlock(link);
		return jnet_simple_complete_request(link, title.cstrptr, option.cstrptr, "fatal");
	}

	/* increase the pending size of this link */
	atom_addone(&node->window);
	/* unlock the context pointer holder after all operation */
	jnet_link_context_unlock(link);

	/* the pro module have resposibility to complete the request, no matter task can be generic or not.
		if corrupt in procedure, the corresponding window MUST be reduce by one */
	pro_receive_data(link, &request, &title, &option, &payload );
	return 0;
}

static void STDCALL jnet_tcp_callback(const struct nis_event *event, const void *data)
{
	struct nis_tcp_data *tcpdata;
	HTCPLINK link;

	if (!event) {
		return;
	}

	tcpdata = (struct nis_tcp_data *)data;
	link = event->Ln.Tcp.Link;

	switch (event->Event) {
		case EVT_TCP_ACCEPTED:
			assert(tcpdata);
			jnet_on_accepted(link, tcpdata->e.Accept.AcceptLink );
			break;
		case EVT_RECEIVEDATA:
			assert(tcpdata);
			if ( jnet_on_receive_data(link, tcpdata->e.Packet.Data, tcpdata->e.Packet.Size) < 0 ) {
				tcp_destroy(link);
			}
			break;
		case EVT_PRE_CLOSE:
			assert(tcpdata);
			jnet_on_preclose(link, tcpdata->e.PreClose.Context);
			break;
		case EVT_CLOSED:
			jnet_on_closed(link);
			break;
		default:
			break;
	}
}

static void STDCALL jnet_udp_callback(const struct nis_event *event, const void *data)
{
	struct nis_udp_data *udpdata;

	udpdata = (struct nis_udp_data *)data;

	switch (event->Event) {
		case EVT_RECEIVEDATA:
			udp_write(event->Ln.Tcp.Link, udpdata->e.Packet.Data, udpdata->e.Packet.Size,
				 udpdata->e.Packet.RemoteAddress, udpdata->e.Packet.RemotePort, NULL);
			break;
		case EVT_PRE_CLOSE:
			break;
		case EVT_CLOSED:
			rt_alert("udp service link has been closed.");
			break;
		default:
			break;
	}
}

static void jnet_on_accepted(HTCPLINK server, HTCPLINK client)
{
	struct jnet_client_context *node;
	abuff_naos_inet_t abuff_inet;
	nsp_status_t status;

	node = (struct jnet_client_context *)malloc(sizeof(struct jnet_client_context));
	if (!node) {
		return;
	}
	memset(node, 0, sizeof(struct jnet_client_context));

	node->link = client;
	status = tcp_getaddr(client, LINK_ADDR_REMOTE, &node->ip, &node->port);
	if (NSP_SUCCESS(status)) {
		naos_ipv4tos(node->ip, &abuff_inet);
		strncpy(node->ipstr, abuff_inet.st, sizeof(node->ipstr) - 1);
		rt_echo("TCP link:%lld from %s:%u accepted", client, node->ipstr, node->port );
	} else{
		rt_echo("IPC link:%lld accepted", client);
	}
	INIT_LIST_HEAD(&node->subcribes);
	node->last_access_timepoint = clock_monotonic();

	/* save the node pointer as context of this link,
		easy to search the node pointer by hash table in nshost.so any time */
	nis_cntl(client, NI_SETCTX, (void *)node);

	lwp_mutex_lock(&__jnet_server.list_client_locker);
	list_add_tail(&node->element, &__jnet_server.clients);
	lwp_mutex_unlock(&__jnet_server.list_client_locker);
}

static void jnet_on_preclose(HTCPLINK link, void *context)
{
	struct jnet_client_context *node;
	struct list_head *pos, *n;
	JRP *jrp;

	node = (struct jnet_client_context *)context;
	if (!node) {
		return;
	}

	/* remove this node from client list */
	lwp_mutex_lock(&__jnet_server.list_client_locker);
	list_del_init(&node->element);
	INIT_LIST_HEAD(&node->element);
	lwp_mutex_unlock(&__jnet_server.list_client_locker);

	if (0 !=  node->ipstr[0]) {
		rt_echo("link:%lld from %s:%u", link, node->ipstr, node->port);
	}

	/* remove all subscribed item */
	pos = NULL;
	n = NULL;

	/* programming in a lower version of the 990 framework, user code MUST protect reference and free procedure of the context pointer.
		in the version higher then or equal to 990, framework protect this pointer by itself */
#if NISV < 990u
	lwp_mutex_lock(&__jnet_server.client_context_locker);
#endif
	list_for_each_safe(pos, n, &node->subcribes) {
		jrp = containing_record(pos, JRP, entry);
		list_del_init(pos);
		free(jrp);
	}
	free(node);

#if NISV < 990u
	lwp_mutex_unlock(&__jnet_server.client_context_locker);
#endif
}

static void jnet_on_closed(HTCPLINK link)
{
	if (link == __jnet_server.tcplink || link == __jnet_server.ipclink ) {
		rt_fatal("server link:%lld close", __jnet_server.tcplink);
	} else {
		rt_echo("link:%lld close", link);
	}
}

static int jnet_tcp_startup(const char *ip, unsigned short port)
{
	int attr;
	nsp_status_t status;
	static const char *jnet_ipc_file = "IPC:/dev/shm/jess.sock";

	tcp_init2(0);

	__jnet_server.tst.parser_ = &nsp__tst_parser;
	__jnet_server.tst.builder_ = &nsp__tst_builder;
	__jnet_server.tst.cb_ = sizeof ( nsp__tst_head_t);
	__jnet_server.tcplink = tcp_create2(&jnet_tcp_callback, ip, port, &__jnet_server.tst);
	if (INVALID_HTCPLINK == __jnet_server.tcplink) {
		rt_fatal("tcp_create failed.");
		return -1;
	}

	do {
	    attr = nis_cntl(__jnet_server.tcplink, NI_GETATTR);
	    if (attr >= 0 ) {
	    	attr |=	LINKATTR_TCP_UPDATE_ACCEPT_CONTEXT;
	    	attr = nis_cntl(__jnet_server.tcplink, NI_SETATTR, attr);
	    }

	    status = tcp_listen(__jnet_server.tcplink, 100);
	    if (!NSP_SUCCESS(status)) {
	    	rt_fatal("tcp_listen failed.");
	    	break;
	    }

	    rt_echo("TCP service link:%lld.", __jnet_server.tcplink);

	    /* assoicated a domian IPC server for localhost data transfer */
	    __jnet_server.ipclink = tcp_create2(&jnet_tcp_callback, jnet_ipc_file, 0, &__jnet_server.tst);
	    if (INVALID_HTCPLINK != __jnet_server.ipclink) {
	    	attr = nis_cntl(__jnet_server.ipclink, NI_GETATTR);
		    if (attr >= 0 ) {
		    	attr |=	LINKATTR_TCP_UPDATE_ACCEPT_CONTEXT;
		    	attr = nis_cntl(__jnet_server.ipclink, NI_SETATTR, attr);
		    }
		    status = tcp_listen(__jnet_server.ipclink, 100);
	    	if (NSP_SUCCESS(status)) {
		    	rt_echo("IPC service link:%lld.", __jnet_server.ipclink);
		    } else {
		    	tcp_destroy(__jnet_server.ipclink);
		    }
	    }
	    return 0;
    } while (0);

    return -1;
}

static int jnet_udp_startup(const char *ip, unsigned short port)
{
	udp_init2(0);

	__jnet_server.udplink = udp_create(&jnet_udp_callback, ip, port, UDP_FLAG_NONE);
	if (INVALID_HUDPLINK == __jnet_server.udplink) {
		rt_fatal("fail to create udp server.");
		return -1;
	}

	rt_echo("UDP service link:%lld.", __jnet_server.udplink);
	return 0;
}

static void *jnet_keepalive(void *p)
{
	struct list_head *pos, *n;
	struct jnet_client_context *node;
	uint64_t current_timepoint;
	static const uint64_t JNET_TIMEOUT_ELAPSE = 6 * 1000 * 1000 * 10;
	static const uint64_t JNET_KEEPALIVE_INTERVAL = 1 * 1000 * 1000;
	uint64_t tcp_link_timeout;
	int qps;

	node = NULL;
	n = 0;

	rt_echo("startup.");

	while (NO == __jnet_server.keepalive_exit) {
		/* yield thread to execut keepalive test */
		lwp_delay(JNET_KEEPALIVE_INTERVAL);
		/* statistic and mark the qps value */
		qps = atom_exchange(&__jnet_server.qps, 0);
		procfs_set_property("qps", qps);

		/* save current time point */
		current_timepoint = clock_monotonic();

#if 0
		/* property first privilege read from proc */
		tcp_link_timeout = procfs_get_property("tcp_link_timeout");
		if (tcp_link_timeout < 0) {
			tcp_link_timeout = JNET_TIMEOUT_ELAPSE;
		} else {
			tcp_link_timeout *= (1000 * 1000 * 10);
		}
#else
		tcp_link_timeout = JNET_TIMEOUT_ELAPSE;
#endif

		/* check all client links */
		lwp_mutex_lock(&__jnet_server.list_client_locker);
		list_for_each_safe(pos, n, &__jnet_server.clients) {
			node = containing_record(pos, struct jnet_client_context, element);
			if (current_timepoint > node->last_access_timepoint && current_timepoint - node->last_access_timepoint > tcp_link_timeout) {
				rt_echo("link:%lld timeout.", node->link);
				tcp_destroy(node->link);
			}
		}
		lwp_mutex_unlock(&__jnet_server.list_client_locker);
	}

	rt_echo("terminated.");
	return NULL;
}

int jnet_startup(const char *ip, unsigned short port)
{
	void *thexit;

    INIT_LIST_HEAD(&__jnet_server.clients);
    lwp_mutex_init(&__jnet_server.list_client_locker, 1);
#if NISV < 990u
    lwp_mutex_init(&__jnet_server.client_context_locker, 1);
#endif
    __jnet_server.keepalive_exit = NO;

    do {
    	if (jnet_tcp_startup(ip, port) < 0) {
    		break;
		}

		if (jnet_udp_startup(ip, port) < 0) {
			break;
		}

		if ( lwp_create(&__jnet_server.keepalive, 0, &jnet_keepalive, NULL) < 0) {
			rt_alert("lwp_create failed, keepalive will no longer support.");
		}

		rt_echo("jess service startup success.");
		lwp_hang();

    } while(0);

    /* interrupt the handler thread */
    pro_release();

	/* finalization */
	__jnet_server.keepalive_exit = YES;
	lwp_join(&__jnet_server.keepalive, &thexit);
    tcp_destroy(__jnet_server.tcplink);
    __jnet_server.tcplink = INVALID_HTCPLINK;
    udp_destroy(__jnet_server.udplink);
    __jnet_server.udplink = INVALID_HUDPLINK;
    lwp_mutex_uninit(&__jnet_server.list_client_locker);

#if NISV < 990u
    lwp_mutex_uninit(&__jnet_server.client_context_locker);
#endif
	return 0;
}

void jnet_subscribe(JRP *jrp)
{
	struct list_head *pos, *n;
	struct jnet_client_context *node;
	HTCPLINK link;
	struct jesspro_task *task;
	JRP *subjrp, *cursorjrp;

	if (!jrp) {
		return;
	}

	task = (struct jesspro_task *)jrp->taskptr;
	if (!task) {
		return;
	}

	if (jrp->valuestring.cstrptr) {
		pro_append_response(task, JESS_FATAL);
		return;
	}

	link = task->link;
	node = NULL;

	if (jnet_link_context_lock_and_get(link, &node) < 0) {
		pro_append_response(task, JESS_FATAL);
		return;
	}

	pos = NULL;
	n = NULL;
	list_for_each_safe(pos, n, &node->subcribes) {
		cursorjrp = containing_record(pos, JRP, entry);
		if (0 == os_strcasecmp(cursorjrp->dupkey, jrp->dupkey)) {
			pro_append_response(task, JESS_FATAL);
			/* unlock the context pointer holder after all operation */
			jnet_link_context_unlock(link);
			return;
		}
	}

	/* create the new item before any other check, outside the lock */
	subjrp = (JRP *)malloc(sizeof(JRP));
	assert(subjrp);
	memcpy(subjrp, jrp, sizeof(JRP));
	list_add_tail(&subjrp->entry, &node->subcribes);

	/* unlock the context pointer holder after all operation */
	jnet_link_context_unlock(link);

	pro_append_response(task, JESS_OK);
}

void jnet_unsubscribe(JRP *jrp)
{
	struct list_head *pos, *n;
	struct jnet_client_context *node;
	HTCPLINK link;
	struct jesspro_task *task;
	JRP *cursorjrp;
	int retval;

	if (!jrp) {
		return;
	}

	task = (struct jesspro_task *)jrp->taskptr;
	if (!task) {
		return;
	}

	link = task->link;
	node = NULL;

	if ( jnet_link_context_lock_and_get(link, &node) < 0 ) {
		pro_append_response(task, JESS_FATAL);
		return;
	}

	retval = -1;
	pos = NULL;
	n = NULL;
	list_for_each_safe(pos, n, &node->subcribes) {
		cursorjrp = containing_record(pos, JRP, entry);
		if ( 0 == os_strcasecmp(jrp->dupkey, "*") ) {
			list_del_init(pos);
			free(cursorjrp);
			retval = 0;
		} else {
			if ( 0 == os_strcasecmp(cursorjrp->dupkey, jrp->dupkey) ) {
				list_del_init(pos);
				free(cursorjrp);
				retval = 0;
			}
		}
	}

	/* unlock the context pointer holder after all operation */
	jnet_link_context_unlock(link);

	pro_append_response(task, 0 == retval ? JESS_OK : JESS_FATAL);
}

void jnet_publish(JRP *jrp)
{
	struct list_head *pos, *n;
	struct list_head *jrppos, *jrpn;
	struct jnet_client_context *node;
	JRP *cursorjrp;
	int len, i;
	char response[MAX_REQ_SIZE];
	size_t jrpkeylen, cursorjrpkeylen;

	len = 0;
	i = os_snprintf(response, sizeof(response), "publish"CRLF);
	pos = NULL;
	n = NULL;
	jrpn = NULL;
	jrppos = NULL;

	lwp_mutex_lock(&__jnet_server.list_client_locker);
	list_for_each_safe(pos, n, &__jnet_server.clients) {
		node = containing_record(pos, struct jnet_client_context, element);

		list_for_each_safe(jrppos, jrpn, &node->subcribes) {
			cursorjrp = containing_record(jrppos, JRP, entry);
			if (0 != strcmp("/", cursorjrp->dupkey)) {
				jrpkeylen = strlen(jrp->dupkey);
				cursorjrpkeylen = strlen(cursorjrp->dupkey);
				if (0 != os_strncasecmp(jrp->dupkey, cursorjrp->dupkey, cursorjrpkeylen) ) {
					continue;
				}

				/* particularly, subscribe string is "A.B", modified pair is "A.BB.C=V",
					strcasecmp cannot verify these case, the pulisher MUST quarantine these request. */
				if ( cursorjrpkeylen < jrpkeylen ) {
					if (jrp->dupkey[cursorjrpkeylen] != '.') {
						continue;
					}
				}
			}

			len = os_snprintf(&response[i], sizeof(response) - i, "%s%s=%s"CRLF, JESS_OK, jrp->dupkey, jrp->dupvalue);
			if (len + i < sizeof(response)) {
				tcp_write(node->link, response, len + i, NULL);
			}
			break;  /* one event can only be one time publish, no matter how many subscribe effective on it */
		}
	}
	lwp_mutex_unlock(&__jnet_server.list_client_locker);
}

int jnet_write_response(HTCPLINK link, const unsigned char *data, int size)
{
	struct jnet_client_context *node;

	node = NULL;
	if ( jnet_link_context_lock_and_get(link, &node) < 0 ) {
		return -1;
	}

	atom_subone(&node->window);

	/* unlock the context pointer holder after all operation */
	jnet_link_context_unlock(link);

	/* post request to framework */
	return tcp_write(link, data, size, NULL);
}

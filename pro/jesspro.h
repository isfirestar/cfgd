#if !defined JESS_PROTO_H
#define JESS_PROTO_H

#include "nisdef.h"
#include "clist.h"
#include "threading.h"
#include "threading.h"

#include "runtime.h"
#include "request.h"

#define JESS_OK					"ok\r\n"
#define JESS_FATAL				"fatal\r\n"

#define JESS_SYNC				"sync\r\n"
#define JESS_FSYNC				"fsync\r\n"
#define JESS_RECOVER2			"recover2\r\n"

#define MAX_PENDING_TASK			(100)

struct pro_work_thread
{
	char							name[8];
	lwp_mutex_t 			mutex;
	lwp_t 				thread;
	nsp_boolean_t 						exit;
	struct list_head 				task_head;
	int 							task_pending;
	lwp_event_t        notification;
	int 							LWP;
	int 							offset;		/* current byte offset in @response */
	char 							response[0];
};

typedef void (*pro_event_routine_t)(JRP *jrp);
struct jesspro_task
{
	HTCPLINK 				 link;

	struct pro_work_thread	*thread;

	CSTRPTR 				 origin;
	CSTRPTR 				 payload;							/* the origin buffer from low-level receive proc, use to free */
	CSTRPTR					 title;								/* the command word of this request task */
	char					 dupopt[MAX_OPT_SIZE];

	int 					 size_jrp_payload;
	struct list_head 		 head_jrp_payload;		/* head of request list, element type is JRP */

	int 					 size_jrp_option;
	struct list_head 		 head_jrp_option;		/* head of request option list, element type is JRP */

	struct list_head 		 entry;			/* element of jesspro_server::task_head*/

	pro_event_routine_t		 task_handler;
};

extern int pro_create();
extern void pro_release();

extern int pro_receive_data(HTCPLINK link,
	const CSTRPTR *origin,const CSTRPTR *title, const CSTRPTR *option, const CSTRPTR *payload);
extern const char *pro_analyze_request_title(const char *origin);

extern int pro_append_response(const struct jesspro_task *task, const char *format, ...);
extern int pro_append_binary_response(const struct jesspro_task *task, const unsigned char *data, int size);

extern int pro_append_request(const char *title, const JRP *jrp, const char *payload);
#define pro_append_sync()	pro_append_request("sync", NULL, NULL)
#define pro_append_fsync()	pro_append_request("fsync", NULL, NULL)
extern int pro_complete_fsync();

/* below declarations are JRP handler, scatter in binary.c/manage.c/variable.c */
extern void pro_query_version(JRP *jrp);
extern void pro_create_checkpoint(JRP *jrp);
extern void pro_recover_checkpoint(JRP *jrp);
extern void pro_mkdir(JRP *jrp);
extern void pro_rmdir(JRP *jrp);
extern void pro_binary_read(JRP *jrp);
extern void pro_binary_write(JRP *jrp);
extern void pro_variable_contain(JRP *jrp);
extern void pro_vairable_read(JRP *jrp);
extern void pro_variable_write(JRP *jrp);
extern void pro_variable_scope(JRP *jrp);
extern void pro_variable_read_file(JRP *jrp);

#endif

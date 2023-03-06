#include "compiler.h"

#include "nis.h"
#include "threading.h"
#include "clock.h"
#include "ifos.h"

#include <ctype.h>
#include <stdio.h>

#include "tst.h"
#include "runtime/runtime.h"

#include "hash.h"

static lwp_event_t __exit;
static char tag_host[16] = { 0 };
static int reqseq = 0;

struct kv_pair
{
	char key[MAX_KEY_SIZE];
	char value[MAX_VALUE_SIZE];
};

#define EASYJESS_VERSION "VersionOfProgramDefinition-ChangeInMakefile"
static void show_author_information()
{
	char author_context[512];

	sprintf(author_context,
		"easyjess %s"POSIX__EOL
		"Copyright (C) 2019 Zhejiang Guozi RoboticsCo., Ltd."POSIX__EOL
		"For bug reporting instructions, please see:"POSIX__EOL
		"<http://www.gzrobot.com/>."POSIX__EOL
		"Find the AGV manual and other documentation resources online at:"POSIX__EOL
		"<http://www.gzrobot.com/productCenter/>."POSIX__EOL
		"For help, type \"help\"."POSIX__EOL,
#if _WIN32
		jess_get_program_version(holder, sizeof(holder))
#else
		EASYJESS_VERSION
#endif
		);

	printf("%s", author_context);
}

static void show_help_context()
{
	static const char *usage_context =
		"NAME\n"
		"\teasyjess\n"
		"\nINTRODUCE\n"
		"\tilteral protocol and simle communication interact with jess\n"
		"\n"
		"SYNOPSIS\n"
		"\teasyjess\n"
		"\t\t[-h] display usage context and help informations\n"
		"\t\t[-v] query jess version\n"
		"\n\t\tRead/Write Command Group:\n"
		"\t\t[read] read field from server. eg: easyjess read driver.dio.device[0].dat[0] 127.0.0.1\n"
		"\t\t[write] write field to server. eg: easyjess write driver.dio.device[+].da=7 127.0.0.1\n"
		"\t\t[scope] obtain entire data contain in specified segment. eg: easyjess scope driver.dio 127.0.0.1\n"
		"\t\t[file] obtain entire data in a directory, organize with JSON format. eg: easyjess file driver.dio 127.0.0.1\n"
		"\n\t\tBackup/Recover Command Group:\n"
		"\t\t[checkpoint] create a checkpoint to save current config settings. eg: easyjess checkpoint 127.0.0.1\n"
		"\t\t[recover] recover config settings from specified checkpoint. eg: easyjess recover jess_checkpoint_20191126_164958 127.0.0.1\n"
		"\n\t\tDir Related Command Group:\n"
		"\t\t[contain] query all context in below level. eg: easyjess contain driver.dio.device 127.0.0.1\n"
		"\t\t[mkdir] create a directory. eg: easyjess mkdir custom 127.0.0.1\n"
		"\t\t[rmdir] delete a directory. eg: easyjess rmdir custom 127.0.0.1\n"
		"\n\t\tData Storage Command Group:\n"
		"\t\t[sync] force synchronous config data to low-level cache. eg: easyjess sync 127.0.0.1\n"
		"\n"
		;

	printf("%s", usage_context);
}

static nsp_boolean_t is_effect_ipv4( const char *ipstr )
{
	size_t len;
	char *chr, *dup, *cursor;
	int i;
	int segm;
	nsp_boolean_t retb;

	if (!ipstr) {
		return NO;
	}
	len = strlen(ipstr);

	if ( 0 == len || len >= INET_ADDRSTRLEN ) {
		return NO;
	}

	for (i = 0; i < len; i++) {
		if (!isdigit(ipstr[i]) && ipstr[i] != '.') {
			return NO;
		}
	}

	dup = strdup(ipstr);
	cursor = dup;
	i = 0;
	retb = YES;

	while (NULL != (chr = strchr(cursor, '.')) ) {
		*chr = 0;
		if (strlen(cursor) > 3) {
			retb = NO;
			break;
		}
		segm = atoi(cursor);
		if (segm > 255) {
			retb = NO;
			break;
		}

		cursor = chr + 1;
		i++;
	}

	if (i != 3) {
		retb = NO;
	}

	free(dup);
	return retb;
}

static int analy_option(const char *data, int *id, int64_t *timestamp)
{
	char *opt, *cursor, *crlf, *equ, *endptr;
	struct kv_pair kv[2];
	int i;
	int retval;

	assert(id && data && timestamp);

	opt = strdup(data);
	cursor = opt;
	retval = 0;
	i = 0;
	while (NULL != (crlf = strstr(cursor, CRLF))) {

		if (i >= 2) {
			retval = -1;
			printf("malform packet received: options amount mismatch.\n");
			break;
		}

		/* set cursor to be a legal string */
		memset(crlf, 0, 2);
		/* build the kv pair */
		equ = strchr(cursor, '=');
		if (!equ) {
			printf("malform packet received: no options key.\n");
			retval = -1;
			break;
		}
		*equ = 0;
		strcpy(kv[i].key, cursor);
		strcpy(kv[i].value, equ + 1);

		i++;
		cursor = crlf + 2;
	}

	free(opt);

	if (retval < 0) {
		printf("illegal opt:%s\n", data);
		return -1;
	}

	for ( i = 0; i < 2; i++) {
		if (0 == strcmp(kv[i].key, "id")) {
			*id = atoi(kv[i].value);
		}

		if (0 == strcmp(kv[i].key, "timestamp")) {
			*timestamp = strtoll(kv[i].value, &endptr, 10);
		}
	}

	return 0;
}

static void on_receive_data(HTCPLINK link, const void *data, int size)
{
	int64_t now, req_timestamp;
	char *cursor, *buffer, *option, *title, *symbol, *response;
	int ackseq;

	now = clock_monotonic();

	/* save to another string with null-terminate */
	buffer = (char *)malloc(size + 1);
	assert(buffer);
	memcpy(buffer, data, size);
	buffer[size] = 0;

	do {
		cursor = buffer;
		symbol = strstr(cursor, CRLF);
		if (!symbol) {
			printf("malform packet received: no title.\n");
			break;
		}
		memset(symbol, 0, 2);
		title = cursor;
		cursor = symbol + 2;

		/* check the opt segment */
		symbol = strstr(cursor, JESS_REQ_DUB_END);
		if (!symbol) {
			printf("malform packet received: no options.\n");
			break;
		}
		/* reserve a \r\n symbol */
		memset(symbol + 2, 0 , 2);
		option = cursor;
		cursor = symbol + 4;
		response = cursor;

		if ( analy_option(option, &ackseq, &req_timestamp) < 0 ) {
			break;
		}

		if (reqseq != ackseq) {
			printf("malform packet received: seq unmatch reqseq=%d ackseq=%d.\n", reqseq, ackseq);
			break;
		}

		printf("%s\n%s\n", title, response);
		printf("---------------------------------------------------\n");
		printf("%u bytes from %s: seq=%d rtt=%.2fms\n", size, tag_host, ackseq, (double)((double)now - req_timestamp) / 10000);
	} while (0);

	free(buffer);
}

static void STDCALL tcp_callback(const struct nis_event *event, const void *data)
{
	if (EVT_RECEIVEDATA == event->Event) {
		on_receive_data(event->Ln.Tcp.Link, ((struct nis_tcp_data *)data)->e.Packet.Data,
			((struct nis_tcp_data *)data)->e.Packet.Size);

		lwp_event_awaken(&__exit);
	}
	else if (EVT_CLOSED == event->Event) {
		lwp_event_awaken(&__exit);
	}
}

static HTCPLINK init_tcp_network(const char *host)
{
	tst_t tst;
	int retval;
	static const uint16_t port = 4407;
	HTCPLINK link;
	int useipc;
#if !_WIN32
	static const char *jnet_ipc_file = "ipc:/dev/shm/jess.sock";
#endif

	useipc = 0;
	link = INVALID_HTCPLINK;
	do {
		tcp_init2(0);

#if !_WIN32
		/* self detect domain socket transfer */
		if (0 == memcmp(host, "ipc", 3) || 0 == strcasecmp(host, "127.0.0.1")) {
			useipc = 1;
		}
#endif
		tst.parser_ = &nsp__tst_parser;
		tst.builder_ = &nsp__tst_builder;
		tst.cb_ = sizeof (nsp__tst_head_t);
		link = tcp_create2(&tcp_callback, useipc ? "ipc:" : NULL, 0, &tst);
		if (INVALID_HTCPLINK == link) {
			break;
		}

		if (useipc) {
			retval = tcp_connect(link, jnet_ipc_file, 0);
		} else {
			if (!is_effect_ipv4(host)) {
				printf("host %s is not a effect IPv4 address,\n", host);
				break;
			}
			retval = tcp_connect(link, host, port);
		}
		if (retval < 0) {
			printf("connect timeout\n");
			break;
		}

		return link;
	} while (0);

	if (INVALID_HTCPLINK != link) {
		tcp_destroy(link);
	}
	return INVALID_HTCPLINK;
}

int main(int argc, char **argv)
{
	char request[MAX_REQ_SIZE], opt[MAX_OPT_SIZE];
	int offset;
	HTCPLINK link;
	int i;
	char title[MAX_TITLE_SIZE];
	size_t n;

	if (argc < 2) {
		printf("usage: easyjess [command][segment...][host]\n");
		return 1;
	}

	if (argc == 2) {
		if (0 == strcmp(argv[1], "-h") || 0 == strcmp(argv[1], "--help")) {
			show_help_context();
			return 0;
		} else if (0 == strcmp(argv[1], "-v") || 0 == strcmp(argv[1], "--version")) {
			show_author_information();
			return 0;
		} else {
			printf("usage: easyjess [command][segment...][host]\n");
			return 1;
		}
	}

	n = strlen(argv[1]);
	if (0 == n || n >= sizeof(title)) {
		printf("title size over limit.\n");
		return 1;
	}
	strcpy(tag_host, argv[argc - 1]);
	strcpy(title, argv[1]);

	lwp_event_init(&__exit, LWPEC_SYNC);

	if ( INVALID_HTCPLINK == (link = init_tcp_network(tag_host)) ) {
		return 1;
	}

	reqseq = ifos_random(1, 5000);
	sprintf(opt, "id=%d"CRLF"timestamp="INT64_STRFMT"\r\n\r\n", reqseq, clock_monotonic());
	/* append title and option to request packet */
	offset = sprintf(request, "%s"CRLF"%s", title, opt);
	/* append segment to request packet if exist, can be multiple items */
	for (i = 2; i < argc - 1; i++) {
		offset += sprintf(&request[offset], "%s"CRLF, argv[i]);
	}

	tcp_write(link, request, offset, NULL);

	lwp_event_wait(&__exit, -1);
	lwp_event_uninit(&__exit);
	return 0;
}

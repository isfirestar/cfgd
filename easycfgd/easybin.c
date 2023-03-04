
#include "compiler.h"

#include "nis.h"
#include "threading.h"
#include "clock.h"
#include "ifos.h"

#include <ctype.h>
#include <stdio.h>

#include "tst.h"
#include "runtime/runtime.h"

static lwp_event_t __exit;
static char tag_host[16] = { 0 };
static int reqseq = 0;

struct kv_pair
{
	char key[MAX_KEY_SIZE];
	char value[MAX_VALUE_SIZE];
};

static void show_help_context()
{
	static const char *usage_context =
		"NAME\n"
		"\teasybin\n"
		"\nINTRODUCE\n"
		"\tilteral protocol and simle communication interact with jess\n"
		"\n"
		"SYNOPSIS\n"
		"\teasybin\n"
		"\t\t[-h] display usage context and help informations\n"
		"\t\t[read] read binary section from server. eg: easybin read offset=100 size=64 127.0.0.1\n"
		"\t\t[write] write binary section to server. eg: easybin write offset=32 \\\\x41\\\\x33\\\\x89\\\\x22\\\\x66\\\\x00 127.0.0.1\n"
		"\t\t 	write visible string to server. eg: easybin write offset=32 'hello world' 127.0.0.1\n"
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

static int analy_option(const char *data, int *id, int64_t *timestamp, int *cb)
{
	char *opt, *cursor, *crlf, *equ, *endptr;
	struct kv_pair kv[10];
	int i;
	int kvn;
	int retval;

	assert(id && data && timestamp);

	kvn = 0;
	opt = strdup(data);
	cursor = opt;
	retval = 0;
	i = 0;
	while (NULL != (crlf = strstr(cursor, "\r\n"))) {
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
		kvn++;
		cursor = crlf + 2;
	}

	free(opt);

	if (retval < 0) {
		printf("illegal opt:%s\n", data);
		return -1;
	}

	for ( i = 0; i < kvn; i++) {
		if (0 == strcmp(kv[i].key, "id")) {
			*id = atoi(kv[i].value);
		}

		if (0 == strcmp(kv[i].key, "timestamp")) {
			*timestamp = strtoll(kv[i].value, &endptr, 10);
		}

		if (0 == strcmp(kv[i].key, "size")) {
			*cb = strtoll(kv[i].value, &endptr, 10);
		}
	}

	return 0;
}

static void on_receive_data(HTCPLINK link, const void *data, int size)
{
	int64_t now, req_timestamp;
	char *cursor, *buffer, *option, *title, *symbol, *response, *status;
	int ackseq;
	int cb;
	int i;

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
		memset(symbol + 2, 0 , 2);
		option = cursor;
		cursor = symbol + 4;

		/* check the response status */
		symbol = strstr(cursor, CRLF);
		if (!symbol) {
			printf("malform packet received: no response status.\n");
			break;
		}
		memset(symbol, 0, 2);
		status = cursor;
		response = symbol + 2;

		if ( analy_option(option, &ackseq, &req_timestamp, &cb) < 0 ) {
			break;
		}

		if (reqseq != ackseq) {
			printf("malform packet received: seq unmatch reqseq=%d ackseq=%d.\n", reqseq, ackseq);
			break;
		}

		printf("%s\n%s\n", title, status);

		if (0 == strcmp(title, "rbin")) {
			for (i = 0; i < cb; i++) {
				if (isprint(response[i]) || isspace(response[i])) {
					printf("%c", (char)response[i]);
				} else {
					printf("\\x%02x", (unsigned char)response[i]);
				}
			}
			printf("\n");
		}
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

static int init_tcp_network(const char * host)
{
	tst_t tst;
	int retval;
	static const uint16_t port = 4407;
	HTCPLINK link;

	link = INVALID_HTCPLINK;
	do {
		if (!is_effect_ipv4(host)) {
			printf("host %s is not a effect IPv4 address,\n", host);
			break;
		}

		tcp_init2(0);

		link = tcp_create(&tcp_callback, NULL, 0);
		if (INVALID_HTCPLINK == link) {
			break;
		}

		tst.parser_ = &nsp__tst_parser;
		tst.builder_ = &nsp__tst_builder;
		tst.cb_ = sizeof (nsp__tst_head_t);

		if ((retval = nis_cntl(link, NI_SETTST, &tst)) < 0) {
			printf("cntl failed.\n");
			break;
		}

		if ((retval = tcp_connect(link, host, port)) < 0) {
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

int format_hex_stream(const char *argv, unsigned char *output)
{
	int offset;
	const char *cursor, *pos;
	char strbin[3], *endptr;
	unsigned char cbin;

	assert(argv && output);

	offset = 0;
	cursor = (const char *)argv;

	while (NULL != (pos = strstr(cursor, "\\x"))) {
		memcpy(&output[offset], cursor, pos - cursor);
		offset += (pos - cursor);
		cursor = pos;

		strncpy(strbin, pos + 2, 2);
		strbin[2] = 0;
		cbin = (unsigned char)strtol(strbin, &endptr, 16);
		memcpy(&output[offset], &cbin, 1);
		offset++;

		/* \x00 */
		cursor += 4;
	}

	if (cursor) {
		offset += sprintf((char *)&output[offset], "%s", cursor);
	}

	return offset;
}

void show_usage()
{
	printf("usage: easybin [read|rbin][offset][size][host]\n");
	printf("usage: easybin [write|wbin][offset][data][host]\n");
}

int main(int argc, char **argv)
{
	char request[MAX_REQ_SIZE], opt[MAX_OPT_SIZE];
	int offset;
	HTCPLINK link;
	int n;
	unsigned char buffer[1024];

	if (argc < 2) {
		show_usage();
		return 1;
	}

	if (argc == 2) {
		if (0 == strcmp(argv[1], "-h") || 0 == strcmp(argv[1], "help") || 0 == strcmp(argv[1], "--help")) {
			show_help_context();
			return 0;
		} else {
			show_usage();
			return 1;
		}
	}

	strcpy(tag_host, argv[argc - 1]);
	if ( INVALID_HTCPLINK == (link = init_tcp_network(tag_host)) ) {
		return 1;
	}

	offset = 0;
	lwp_event_init(&__exit, LWPEC_SYNC);
	reqseq = ifos_random(1, 5000);
	do {
		if (0 == strcmp(argv[1], "read") || 0 == strcmp(argv[1], "rbin")) {
			if (5 != argc) {
				break;
			}

			/* build base option */
			offset = sprintf(opt, "id=%d\r\ntimestamp="INT64_STRFMT"\r\n", reqseq, clock_monotonic());
			offset += sprintf(&opt[offset], "offset=%s\r\n", argv[2]);
			offset += sprintf(&opt[offset], "size=%s\r\n", argv[3]);

			offset = sprintf(request, "rbin\r\n%s\r\n", opt);
			tcp_write(link, request, offset, NULL);
		}

		if (0 == strcmp(argv[1], "write") || 0 == strcmp(argv[1], "wbin")) {
			if (5 != argc) {
				break;
			}

			/* build base option */
			offset = sprintf(opt, "id=%d"CRLF"timestamp="INT64_STRFMT"\r\n", reqseq, clock_monotonic());
			offset += sprintf(&opt[offset], "offset=%s\r\n", argv[2]);

			/* calculate the write length, format it into buffer */
			n = format_hex_stream(argv[3], buffer);
			sprintf(&opt[offset], "size=%d"JESS_REQ_DUB_END, n);

			/* fully build request */
			offset = sprintf(request, "wbin"CRLF"%s", opt);
			memcpy(&request[offset], buffer, n);
			offset += n;
			tcp_write(link, request, offset, NULL);
		}

		lwp_event_wait(&__exit, -1);
		lwp_event_uninit(&__exit);
		return 0;
	} while (0);

	show_usage();
	lwp_event_uninit(&__exit);
	return 1;
}

#include "compiler.h"

#include "nis.h"
#include "threading.h"
#include "clock.h"
#include "ifos.h"
#include "threading.h"
#include "atom.h"

#include <ctype.h>
#include <stdio.h>
#include <stdarg.h>

#include "tst.h"
#include "runtime/runtime.h"

#include "easyjess.h"

static char tag_host[16] = { 0 };

int echo(const char *format, ...)
{
    char display[1024];
    va_list ap;
    int n;

    va_start(ap, format);
    n = vsnprintf(display, sizeof(display), format, ap);
    va_end(ap);

    return write(1, display, n);
}

#define EASYSUBSCRIBE_VERSION "VersionOfProgramDefinition-ChangeInMakefile"
static void show_author_information()
{
    char author_context[512];

    sprintf(author_context,
        "easysubscribe %s"POSIX__EOL
        "Copyright (C) 2019 Zhejiang Guozi RoboticsCo., Ltd."POSIX__EOL
        "For bug reporting instructions, please see:"POSIX__EOL
        "<http://www.gzrobot.com/>."POSIX__EOL
        "Find the AGV manual and other documentation resources online at:"POSIX__EOL
        "<http://www.gzrobot.com/productCenter/>."POSIX__EOL
        "For help, type \"help\"."POSIX__EOL,
#if _WIN32
        jess_get_program_version(holder, sizeof(holder))
#else
        EASYSUBSCRIBE_VERSION
#endif
        );

    echo("%s", author_context);
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

static int stdout_offset = 0;

static void on_receive_data(HTCPLINK link, const void *data, int size)
{
    char *cursor, *buffer, *title, *status, *symbol;

    /* save to another string with null-terminate */
    buffer = (char *)malloc(size + 1);
    assert(buffer);
    memcpy(buffer, data, size);
    buffer[size] = 0;

    do {
        cursor = buffer;
        symbol = strstr(cursor, CRLF);
        if (!symbol) {
            echo("\nerror:>> malform packet, no title.\n");
            break;
        }
        memset(symbol, 0, 2);
        title = cursor;
        cursor = symbol + 2;

        /* check the opt segment */
        symbol = strstr(cursor, JESS_REQ_DUB_END);
        if (symbol) {
            echo("\nerror:>> malform packet, incorrect options.\n");
            break;
        }

        /* do NOT echo any thing when keepalive ack */
        if (0 == strcmp(title, "keepalive")) {
            free(buffer);
            return;
        }

        /* looking for the status segment */
        symbol = strstr(cursor, CRLF);
        if (!symbol) {
            echo("\nerror:>> malform packet, no status responed.\n");
            break;
        }
        memset(symbol, 0, 2);
        status = cursor;
        cursor = symbol + 2;

        if (1 == posix__atomic_get(&stdout_offset)) {
            echo("\n");
            posix__atomic_set(&stdout_offset, 0);
        }
        echo("%s:>> %s\n", title, status);

        if ( cursor && 0 == strcmp(title, "publish") && 0 == strcmp(status, "ok")) {
            echo("%s:>> %s", title, cursor);
        }

    } while (0);

    free(buffer);
    posix__atomic_set(&stdout_offset, 1);
    echo("subscribe:<< ");
}

static void STDCALL tcp_callback(const struct nis_event *event, const void *data)
{
    if (EVT_RECEIVEDATA == event->Event) {
        on_receive_data(event->Ln.Tcp.Link, ((struct nis_tcp_data *)data)->e.Packet.Data,
            ((struct nis_tcp_data *)data)->e.Packet.Size);
    }
    else if (EVT_CLOSED == event->Event) {
        exit(0);
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
            echo("host %s is not a effect IPv4 address,\n", host);
            break;
        }

        tcp_init();

        link = tcp_create(&tcp_callback, NULL, 0);
        if (INVALID_HTCPLINK == link) {
            break;
        }

        tst.parser_ = &nsp__tst_parser;
        tst.builder_ = &nsp__tst_builder;
        tst.cb_ = sizeof (nsp__tst_head_t);

        if ((retval = nis_cntl(link, NI_SETTST, &tst)) < 0) {
            echo("cntl failed.\n");
            break;
        }

        if ((retval = tcp_connect(link, host, port)) < 0) {
            echo("connect timeout\n");
            break;
        }

        return link;
    } while (0);

    if (INVALID_HTCPLINK != link) {
        tcp_destroy(link);
    }
    return INVALID_HTCPLINK;
}

int64_t timestamp;

void *keepalive(void *p)
{
    HTCPLINK link;

    link = (HTCPLINK)p;

    while (1) {
        lwp_delay(1000 * 1000);

        if ( posix__atomic_get64(&timestamp) - clock_monotonic() > 3 * 1000 * 1000 * 10 ) {
            tcp_write(link, "keepalive\r\n", 11, NULL);
        }
    }
    return NULL;
}

int main(int argc, char **argv)
{
    char subscribe[MAX_KEY_SIZE], key[MAX_PRE_SEG_SIZE], *keyptr;
    HTCPLINK link;
    int n;
    int offset;
    lwp_t tidp;


    if (argc != 2) {
        echo("usage: easysubscribe [host]\n");
        return 1;
    }

    if (0 == strcmp(argv[1], "-h") || 0 == strcmp(argv[1], "--help")) {
        echo("usage: easysubscribe [host]\n");
        return 0;
    } else if (0 == strcmp(argv[1], "-v") || 0 == strcmp(argv[1], "--version")) {
        show_author_information();
        return 0;
    } else {
        ;
    }

    strcpy(tag_host, argv[1]);

    if ( INVALID_HTCPLINK == (link = init_tcp_network(tag_host)) ) {
        return 1;
    }

    timestamp = clock_monotonic();
    lwp_create(&tidp, 0, keepalive, (void *)link);
    echo("subscribe:<< ");

    while (1) {
        if (NULL == (keyptr = fgets(key, sizeof(key), stdin)) ) {
            continue;
        }
        posix__atomic_set(&stdout_offset, 0);

        n = strlen(key);
        if (0 == n) {
            posix__atomic_set(&stdout_offset, 1);
            echo("subscribe:<< ");
            continue;
        }

        if (key[n - 1] == '\n') {
            key[n - 1] = 0;
            n--;
        }

        if ('!' == key[0]) {
            offset = sprintf(subscribe, "unsubscribe\r\n%s", &key[1]);
        } else {
            offset = sprintf(subscribe, "subscribe\r\n%s", key);
        }

        if ( (n = tcp_write(link, subscribe, offset, NULL)) < 0 ) {
            break;
        }

        posix__atomic_set64( &timestamp,  clock_monotonic() );
    }

    return 0;
}

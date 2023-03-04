#include "conf.h"

#include "jessos.h"

#include "ifos.h"
#include "clist.h"

#include <assert.h>
#include <stdlib.h>
#include <ctype.h>

struct conf_node
{
	struct list_head entry;
	char *key;
	char *value;
	char *line;
};

struct conf_object
{
	struct list_head conf_head;
	char *fptr;
	int64_t fsize;
};
static struct conf_object _conf = { { NULL, NULL }, NULL };

static int os_analy_line(struct conf_node *node)
{
	char *cursor;

	cursor = node->line;

	/* trim head space and tab */
	while (*cursor == ' ' || *cursor == '\t') {
		cursor++;
	}

	/* this is comment line */
	if ( *cursor == '#' ) {
		return -1;
	}

	/* these character are belong to key string */
	node->key = cursor;
	while (*cursor != ' ' && *cursor != '\t' && *cursor) {
		cursor++;
	}

	/* no value string */
	if (!*cursor) {
		return -1;
	}

	/* trim head space and tab */
	while (*cursor == ' ' || *cursor == '\t') {
		*cursor = 0;
		cursor++;
	}

	/* these character are belong to value string */
	node->value = cursor;
	while (*cursor != ' ' && *cursor != '\t' && *cursor) {
		cursor++;
	}

	while (*cursor) {
		*cursor = 0;
		cursor++;
	}

	return 0;
}

static void os_analy_conf()
{
	char *cursor;
	struct conf_node *node;
	char *eol;

	cursor = _conf.fptr;

	while (*cursor) {
		node = (struct conf_node *)malloc(sizeof(struct conf_node));
		assert(node);
		memset(node, 0, sizeof(*node));

		node->line = cursor;

		if ( NULL != (eol = strstr(cursor, "\r\n")) ) {
			memset(eol, 0, 2);
			cursor = eol + 2;
		} else {
			if ( NULL != (eol = strstr(cursor, "\n")) ) {
				*eol= 0;
				cursor = eol + 1;
			} else {
				cursor = strrchr(cursor, 0);
			}
		}

		if ( os_analy_line(node) >= 0 ) {
			list_add_tail(&node->entry, &_conf.conf_head);
		} else {
			free(node);
		}
	}
}

void os_load_conf(const char *cfgfile)
{
	int retval;
	file_descriptor_t fd;
	char path[MAXPATH];

#if _WIN32
	char pedir[128];

	if (cfgfile) {
		strncpy(path, cfgfile, sizeof(path) - 1);
	} else {
		posix__getpedir2(pedir, sizeof(pedir));
		os_snprintf(path, sizeof(path), "%s\\etc\\jess.conf", pedir);
	}
#else
	strncpy(path, cfgfile ? cfgfile : "/etc/jess.conf", sizeof(path) - 1);
#endif

	retval = ifos_file_open(path, FF_RDACCESS | FF_OPEN_EXISTING, 0644, &fd);
	if (retval < 0) {
		return;
	}

	do {
		_conf.fsize = ifos_file_fgetsize(fd);
		if (_conf.fsize <= 0) {
			break;
		}

		_conf.fptr = (char *)malloc( (size_t)_conf.fsize + 1);
		assert(_conf.fptr);
		memset(_conf.fptr, 0, (int)_conf.fsize + 1);

		retval = ifos_file_read(fd, _conf.fptr, (int)_conf.fsize);
		if (retval != _conf.fsize) {
			free(_conf.fptr);
			_conf.fptr = NULL;
			break;
		}
	} while (0);

	ifos_file_close(fd);

	if (!_conf.fptr) {
		return;
	}

	INIT_LIST_HEAD(&_conf.conf_head);
	os_analy_conf();
}

const char *os_query_conf(const char *key)
{
	struct list_head *pos, *n;
	struct conf_node *node;

	if (!_conf.fptr || _conf.fsize <= 0 || list_empty(&_conf.conf_head) || !key) {
		return NULL;
	}

	pos = NULL;
	n = NULL;
	list_for_each_safe(pos, n, &_conf.conf_head) {
		node = containing_record(pos, struct conf_node, entry);
		if ( 0 == os_strcasecmp(node->key, key) ) {
			return node->value;
		}
	}

	return NULL;
}

const char *os_query_conf_integer(const char *key, int *intvalue)
{
	const char *strvalue;
	size_t i;

	if (!key || !intvalue) {
		return NULL;
	}

	if (NULL == (strvalue = os_query_conf(key))) {
		return NULL;
	}

	for (i = 0; i < strlen(strvalue); i++) {
		if (!isdigit(strvalue[i])) {
			return NULL;
		}
	}

	*intvalue = atoi(strvalue);
	return strvalue;
}

void os_release_conf()
{
	struct conf_node *node;

	while (!list_empty(&_conf.conf_head)) {
		node = list_first_entry(&_conf.conf_head, struct conf_node, entry);
		assert(node);
		list_del_init(&node->entry);
		free(node);
	}

	INIT_LIST_HEAD(&_conf.conf_head);

	if (_conf.fptr && _conf.fsize > 0) {
		free(_conf.fptr);
		_conf.fptr = NULL;
		_conf.fsize = 0;
	}
}

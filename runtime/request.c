#include "request.h"

#include <ctype.h>

#include "refos.h"
#include "runtime.h"

/* definition of return value:
	-1. syntax error
	0.  not a array syntax
	1.	looks like a array
*/
static int rt_is_array_syntax(const char *key, const char **outputleft, const char **outputright)
{
	const char *cursor, *left, *right;

	left = right = NULL;
	cursor = key;
	while (*cursor) {
		if (*cursor == '[') {
			if (left) {		/* syntax like "[[" or "]]" are not allow. */
				return -1;
			}
			left = cursor;
		}
		if (*cursor == ']') {
			if (right) {
				return -1;
			}
			right = cursor;
		}
		cursor++;
	}

	/* a lot of request syntax is not for arrays, parse as normal socpe first,
		save the CPU time for @strdup */
	if (!left && !right) {
		return 0;
	}

	/* refuse normal error syntax, for example:
		1. left or right found, but another side no found
		write test.file.nr[1=x
		write test.file.nr1]=x
		2. right at the left side of left
		write test.file.nr]1[=x
		3. no array name:
		write test.file.[3]=x
		4. ']' not at the tial of key
		write test.file.item[1]ab=x
		5. range to save the array index is empty
		write test.file.item[]=x
		*/
	if (!left || !right || right < left || left == key || (0 != *(right + 1)) || (right == (left + 1)) ) {
		return -1;
	}

	if (outputleft) {
		*outputleft = left;
	}

	if (outputright) {
		*outputright = right;
	}

	return 1;
}

/* this method use to analysis array request syntax
	the return value:
	-2. can not find the marker of array syntax, '[' or ']', the request key not a array syntax
	-1. array marker has been found, but other syntax error
	the caller is always responsible to free @*newkey when method success called */
static int rt_analysis_array_syntax(const char *key, char **newkey, int *index)
{
	const char *originleft, *originright;
	char *dup, *i, *left, *right;
	int retval;

	if (!key || !newkey || !index) {
		return -1;
	}
	*newkey = NULL;
	*index = -1;
	left = NULL;
	right = NULL;

	if ( (retval = rt_is_array_syntax(key, &originleft, &originright)) <= 0 ) {
		return retval;
	}

	/* the array symbol offset in origin and duplicate buffer MUST the same */
	dup = os_strdup(key);
	left = dup + (originleft - key);
	right = dup + (originright - key);

	*left = 0;
	left++;
	*right = 0;
	*newkey = dup;

	/* only one symbol in array index range, it maybe a special character indicate a particular function */
	if ( right - left == 1 ) {
		if ( *left == JA_CODE_LAST || *left == JA_CODE_QUERY_SIZE || *left == JA_CODE_ADD || *left == JA_CODE_ENTIRE) {
			*index = JA_CODE_TO_SYMBOL(*left);
			return 0;
		}
	}

	/* in the range of symbol '[' and ']', the value representation is the item index of visit array.
		so, all characters MUST be explicit numerical, otherwise indicate a error */
	for (i = left; i < right; i++) {
		if (!isdigit(*i)) {
			return -1;
		}
	}

	*index = atoi(left);
	if ( *index >= MAX_ARY_SIZE || *index < 0 ) {
		rt_fatal("array size %d over limit.", *index);
		return -1;
	}

	return 0;
}

static int rt_standardize_segment(const char *key, char *name, int *index)
{
	int retval;
	char *newkey;

	if (!key || !name || !index) {
		return -EINVAL;
	}

	newkey = NULL;
	if ( (retval = rt_analysis_array_syntax(key, &newkey, index)) < 0 ) {
		rt_fatal("jrp syntax error, %s", key);
	} else {
		if ( *index < 0) {
			strcpy(name, key);
		} else {
			if (newkey) {
				strcpy(name, newkey);
			}
		}
	}

	if (newkey) {
		free(newkey);
	}

	return retval;
}

static int rt_analyze_segment(const char *key, int depth, struct jessrt_segment *segm)
{
	int retval;

	if (!key || !segm) {
		return -EINVAL;
	}

	retval = rt_standardize_segment(key, segm->name, &segm->index);
	if (retval < 0) {
		return -1;
	}

	if (0 == depth || 1 == depth) {
		/* the dir and archive MUST NOT be array */
		if (segm->index < 0) {
			segm->jsl = depth;
			return 0;
		}
		return -1;
	}

	/* other depth automic parse to logic scope or array first */
	segm->jsl = (segm->index < 0) ? JSL_LOGIC_SCOPE : JSL_ARRAY;
	return 0;
}

static int rt_separate_segment(JRP *jrp)
{
	const char *cursor;
	char *ending;
	int depth;
	size_t n;

	depth = 0;
	cursor = jrp->keystring.cstrptr;

	while ( NULL != (ending = strchr(cursor, '.')) ) {
		/* continuation 2 keyword '.' is NOT allow, eg: "driver..elmo" is illegal */
		if (cursor == ending) {
			rt_fatal("jrp syntax error, '.' symbol can not be the first character in segment");
			return -1;
		}

		*ending = 0;
		if ( depth >= MAX_DEPTH_REQ ) {
			rt_fatal("jrp syntax error, request depth over limit");
			return -1;
		}

		/* ensure that the request length does not exceed the key allowable */
		if ((n = (ending - cursor)) >= MAX_PRE_SEG_SIZE) {
			rt_fatal("jrp syntax error, pre segment size over limit");
			return -1;
		}

		if (rt_analyze_segment(cursor, depth, &jrp->segm[depth]) < 0) {
			return -1;
		}

		if (depth > 0) {
			if (jrp->segm[depth - 1].jsl == JSL_ARRAY && jrp->segm[depth - 1].index == JA_SYM_ENTIRE) {
				rt_fatal("wildcard [*] only use to remove entire array");
				return -1;
			}
		}

		depth++;
		cursor = ending + 1;

		/* the last character of entire command is keyword '.', illegal, for example:
			'./easyjess read foundation.agv_shell.white_list.item[$]. 127.0.0.1'
			the keyword '.' after item[$] MUST decline */
		if (0 == *cursor) {
			return -1;
		}
	}

	if (0 != *cursor) {
		if ( depth >= MAX_DEPTH_REQ) {
			rt_fatal("jrp syntax error, request depth over limit");
			return -1;
		}

		if ((n = strlen(cursor)) >= MAX_PRE_SEG_SIZE) {
			rt_fatal("jrp syntax error, pre segment size over limit");
			return -1;
		}

		if (rt_analyze_segment(cursor, depth, &jrp->segm[depth]) < 0) {
			return -1;
		}

		if (depth > 0) {
			if (jrp->segm[depth - 1].jsl == JSL_ARRAY && jrp->segm[depth - 1].index == JA_SYM_ENTIRE) {
				rt_fatal("wildcard [*] only use to remove entire array");
				return -1;
			}
		}

		depth++;
	}

	/* the last scope MUST from logic change to field */
	if (depth >= 3) {
		if (JSL_LOGIC_SCOPE == jrp->segm[depth - 1].jsl) {
			jrp->segm[depth - 1].jsl = JSL_FIELD;
		}
	}

	jrp->depth = depth;
	return depth;
}

/* @rt_separate_kv_field function separate the @command string into key-value model,
	the key character is '=' symbol, if exist, the left side of original string should be key-part, right side of original string should be value-part
	otherwise, no value-part return by @valuestring, this command indicate a read-only request */
static int rt_separate_kv_field(const CSTRPTR *kvpair, JRP *jrp)
{
	char *split;

	if (!kvpair || !jrp) {
		return -1;
	}

	if (!rt_effective_strptr(kvpair)) {
		return -1;
	}

	split = strchr(kvpair->strptr, '=');

	/*  illegal kvpair string that no specific key before sign '=', for example:
		"=myvalue"	*/
	if (split == kvpair->strptr) {
		rt_fatal("syntax error, '=' symbol can not be the first character");
		return -1;
	}

	if (split) {
		*split = 0;
	}

	/* save the key-string pointer in jrp */
	jrp->keystring.strptr = kvpair->strptr;
	jrp->keystring.len = (NULL != split) ? (split - kvpair->strptr) : kvpair->len;
	if ( 0 == jrp->keystring.len || jrp->keystring.len >= MAX_KEY_SIZE) {
		rt_fatal("jrp syntax error, key length over limit");
		return -1;
	}
	/* depth save the origin key-string */
	memcpy( jrp->dupkey, jrp->keystring.cstrptr, jrp->keystring.len );
	jrp->dupkey[jrp->keystring.len] = 0;

	/* dispose the value-string field */
	jrp->valuestring.strptr = NULL;
	jrp->valuestring.len = 0;
	if (split) {
		jrp->valuestring.strptr = split + 1;
		jrp->valuestring.len = kvpair->len - jrp->keystring.len - 1;

		/* illegal command string that no specific value after sign '=', for example:
			"dir.archive.subscope="
			value string length over allowable, for example:
			"dir.archive.subscope=111111111111111111111111111111111111111111111111111111111................................(a lot of data)"
		*/
		if (0 == jrp->valuestring.len || jrp->valuestring.len >= MAX_VALUE_SIZE) {
			rt_fatal("jrp syntax error, value length over limit");
			return -1;
		}

		/* save the value into duplicate buffer */
		memcpy(jrp->dupvalue, jrp->valuestring.cstrptr, jrp->valuestring.len);
		jrp->dupvalue[jrp->valuestring.len] = 0;
	}

	return 0;
}

static JRP *rt_create_jrp(const CSTRPTR *string, const void *taskptr)
{
	JRP *jrp;

	if ( NULL == (jrp = (JRP *)malloc(sizeof(JRP))) ) {
		return NULL;
	}
	rt_init_jrp(jrp, taskptr);

	if (  rt_separate_kv_field(string, jrp) >= 0 ) {
		if ( rt_separate_segment(jrp) >= 0 ) {
			return jrp;
		}
	}

	free(jrp);
	return NULL;
}

static void rt_cleanup_jrplist(struct list_head *jrplist)
{
	struct list_head *pos, *n;
	JRP *cursorjrp;

	pos = NULL;
	n = NULL;
	list_for_each_safe(pos, n, jrplist) {
		cursorjrp = containing_record(pos, JRP, entry);
		list_del_init(pos);
		free(cursorjrp);
	}

	INIT_LIST_HEAD(jrplist);
}

int rt_build_jrp(const char *payload, const void *taskptr, struct list_head *jrplist)
{
	const char *cursor;
	char *splits;
	size_t len;
	JRP *jrp;
	int n;
	CSTRPTR string;

	if (!payload || !jrplist) {
		return -1;
	}

	INIT_LIST_HEAD(jrplist);

	n = 0;
	cursor = payload;
	while (*cursor) {
		splits = strstr(cursor, CRLF);
		if (!splits) {
			break;
		}

		if (n >= MAX_REQ_SECT) {
			rt_fatal("the maximum payload segment in one request over limit.");
			rt_cleanup_jrplist(jrplist);
			return -1;
		}

		memset(splits, 0, 2);
		len = (splits - cursor);

		if ( 0 == len || len > MAX_PAYLOAD_SIZE ) {
			rt_fatal("jrp syntax error, KV-pair length over limit.");
			rt_cleanup_jrplist(jrplist);
			return -1;
		}

		string.cstrptr = cursor;
		string.len = len;
		if ( NULL == (jrp = rt_create_jrp(&string, taskptr)) ) {
			rt_cleanup_jrplist(jrplist);
			return -1;
		}
		list_add_tail(&jrp->entry, jrplist);

		n++;
		cursor = splits + 2;
	}

	if (*cursor) {
		if (n >= MAX_REQ_SECT) {
			rt_fatal("the maximum payload segmeng in one request over limit.");
			rt_cleanup_jrplist(jrplist);
			return -1;
		}

		len = strlen(cursor);
		if ( 0 == len || len > MAX_PAYLOAD_SIZE ) {
			rt_fatal("jrp syntax error, KV-pair length over limit.");
			rt_cleanup_jrplist(jrplist);
			return -1;
		}

		string.cstrptr = cursor;
		string.len = len;
		if (NULL == (jrp = rt_create_jrp(&string, taskptr))) {
			rt_cleanup_jrplist(jrplist);
			return -1;
		}
		list_add_tail(&jrp->entry, jrplist);

		n++;
	}

	return n;
}

#if _UNIT_TEST

/* gcc request.c -g3 -o request -I ../icom/ -I ../ -I ../pro -D_UNIT_TEST=1 -I ../os/
*/
int main(int argc, char **argv)
{
	struct list_head head;
	char strreq[] = "driver.dio.device[1].ais.block[0].start_address\r\ndriver.dio.device[?]\r\n"
		"driver.dio.device[1].ais.block[$].start_address=0x3000\r\ndriver.dio.device[1].ais.block[+].start_address=0x4000";

	rt_build_jrp(argc > 1 ? argv[1] : strreq, NULL, &head);
	return 0;
}

#endif

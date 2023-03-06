#if !defined JESSRT_REQUESTER_H
#define JESSRT_REQUESTER_H

#include "compiler.h"

#include "clist.h"

/* restrict the maximum number of bytes of the title string in single request packet, exclude '\r\n' delimiter length occupy */
#define MAX_TITLE_SIZE				(16)
/* restrict the maximum number of bytes of the option string in single request packet, exclude '\r\n\r\n' delimiter,
	in other words, the actual option context length limit is 124 bytes, this limit consist of option-key, option-value and '.' delimiter */
#define MAX_OPT_SIZE				(128)
/* restrict the maximum number of bytes of a segment in a single request section,
	every segment are separate by '.' delimiter, the size limit exclude the symbol.
	in special case, this limit include the array index and array symbol '[n]', so, this limit restrict array node name length to less than 64 bytes */
#define MAX_PRE_SEG_SIZE			(192)
/* restrict the maximum count of segments in a single request section, the segment count indicate the search depth.
	in JRP structure, depth is also a very import field to define the size of segment array */
#define MAX_DEPTH_REQ				(10)
/* restrict the maximum number of bytes for a key string in a single section, default to 649 bytes, include '.' delimiter symbol */
#define MAX_KEY_SIZE				(MAX_PRE_SEG_SIZE * MAX_DEPTH_REQ + (MAX_DEPTH_REQ - 1) )
/* restrict the maximum number of bytes for a value string in a single section,
	this limit are not affect 'wbin' request. */
#define MAX_VALUE_SIZE				(256)
/* restrict the maximum number of bytes of entire payload buffer in a single section, default to 905 bytes */
#define MAX_PAYLOAD_SIZE			(MAX_KEY_SIZE + MAX_VALUE_SIZE)
/* restrict the maximum sections in a single request, this indicate the most JRP count can be assigned in a task,
	exclude '\r\n' delimiter */
#define MAX_REQ_SECT				(8)
/* restrict the maximum bytes of a single request packet, default to 3764 bytes */
#define MAX_REQ_SIZE				(MAX_TITLE_SIZE + MAX_OPT_SIZE + MAX_PAYLOAD_SIZE * MAX_REQ_SECT )
/* restrict the status field size in response */
#define MAX_REP_STAT_SIZE			(16)
#define MAX_REP_SIZE 				(MAX_TITLE_SIZE + MAX_OPT_SIZE + MAX_REP_STAT_SIZE + 4)

/* restrict the maximum number of array item */
#define MAX_ARY_SIZE				(256)

#define JA_CODE_LAST			('$')
#define JA_CODE_QUERY_SIZE		('?')
#define JA_CODE_ADD				('+')
#define JA_CODE_ENTIRE			('*')
#define JA_SYM_LAST				((int)(0x7FFFFF00 | JA_CODE_LAST))
#define JA_SYM_QUERY_SIZE		((int)(0x7FFFFF00 | JA_CODE_QUERY_SIZE))
#define JA_SYM_ADD				((int)(0x7FFFFF00 | JA_CODE_ADD))
#define JA_SYM_ENTIRE			((int)(0x7FFFFF00 | JA_CODE_ENTIRE))
#define JA_CODE_TO_SYMBOL(code)		( (code) | 0x7FFFFF00 )


#define SCOPE_SAVELEN_OUTOF_LIMIT	(24)

#define KEYWORD_DELETED			"deleted"
#define CR 						0x0d
#define LF 						0x0a
#define CRLF					"\r\n"
#define JESS_REQ_DUB_END		"\r\n\r\n"

typedef struct __t_cstrptr
{
	union {
		char *strptr;
		const char *cstrptr;
		unsigned char *blkptr;
		const unsigned char *cblkptr;
		void *anonymous;
	};
	int len;		/* the size of bytes exclusive the null-terminate symbol('\0') */
} CSTRPTR;
typedef struct __t_cstrptr cstrptr_t;

#define rt_copy_strptr(dst, src)	\
	(dst)->strptr = (src)->strptr;	\
	(dst)->len = (src)->len

#define rt_release_strptr(n)	do { \
		if ( (n).anonymous && (n).len > 0 ) free((n).anonymous); \
		(n).anonymous = NULL;	\
		(n).len = 0; \
	} while (0)

#define rt_assign_strptr(dst, size)	do { \
		(dst)->strptr = (char *)malloc( (size) );	\
		assert( (dst)->strptr );	\
		(dst)->len = size;	\
	} while (0)

#define rt_declare_strptr(name)	\
	CSTRPTR name = { .anonymous = NULL, .len = 0 }

#define rt_effective_strptr(ptr)	( ptr ? ( (ptr)->anonymous && ((ptr)->len > 0) ) : 0 )

#define rt_duplicate_strptr(dst, src)	\
	memcpy((dst)->anonymous, (src)->anonymous, ((dst)->len < (src)->len ? (dst)->len : (src)->len) )

typedef enum jessrt_scope_level
{
	JSL_DIR = 0,
	JSL_ARCHIVE,
	JSL_LOGIC_SCOPE,
	JSL_ARRAY,
	JSL_ARRAY_ITEM,
	JSL_ARRAY_ITEM_AS_FIELD,
	JSL_FIELD,
} JSL;

struct jessrt_segment
{
	char name[MAX_PRE_SEG_SIZE];
	int index;
	JSL jsl;
};

typedef struct jessrt_request_packet
{
	/* link more than one JRP items, JRP list is going to be the semantic "rquest-part" */
	struct list_head entry;
	/* the requester string are consist of these segments. actual array size indicate by @depth field */
	struct jessrt_segment segm[MAX_DEPTH_REQ];
	/* the total depth level of this requester */
	int depth;
	/* actual key-string after analize, can be either modifiable ptr or constant */
	CSTRPTR keystring;
	/* actual value-string after analize, can be null, can be either modifiable ptr or constant */
	CSTRPTR valuestring;
	/* the duplicate string from original */
	char dupkey[MAX_KEY_SIZE];
	char dupvalue[MAX_VALUE_SIZE];
	/* pointer to task which contain the JRP object */
	const void *taskptr;
} JRP;

#define rt_jrp_in_clist(posptr) containing_record(posptr, JRP, entry)

#define rt_init_jrp(jrp, task)	do {	\
		memset( (jrp), 0, sizeof(JRP));	\
		INIT_LIST_HEAD(&(jrp)->entry);	\
		(jrp)->taskptr = (task);	\
	} while(0)

extern int rt_build_jrp(const char *payload, const void *taskptr, struct list_head *jrplist);

#endif

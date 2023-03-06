#include "runtime.h"

#include "request.h"
#include "ldr.h"
#include "virfs.h"
#include "refos.h"

#if _WIN32
	/* #pragma warning(disable:) */
#else
#pragma GCC diagnostic ignored "-Wunused-function"
#endif

unsigned char *rt_analyze_req_head(const CSTRPTR *request, CSTRPTR *title, CSTRPTR *option)
{
	char *splits;
	int n;
	const char *cursor;

	if (!request || !title || !option) {
		return NULL;
	}

	cursor = (const char *)request->cstrptr;
	while (cursor == (splits = strstr(cursor, CRLF)) ) {
		cursor += 2;
	}

	if (!splits) {
		rt_fatal("request title no found.");
		return NULL;
	}
	n = splits - cursor;
	if (n >= MAX_TITLE_SIZE ) {
		rt_fatal("request title length over limit");
		return NULL;
	}
	memset(splits, 0, 2);
	title->cstrptr = cursor;
	title->len = n;
	cursor = splits + 2;

	/* looking for option segment */
	splits = strstr(cursor, JESS_REQ_DUB_END);
	if (splits) {
		n = splits - cursor;
		if (n >= MAX_OPT_SIZE ) {
			rt_fatal("request option length over limit");
			return NULL;
		}
		memset(splits, 0, 4);
		option->cstrptr = cursor;
		option->len = n;
		cursor = splits + 4;
	}

	return (unsigned char *)cursor;
}

static struct jessrt_configure_node * rt_seek_array_node(const struct jessrt_configure_node *arraynode, int which)
{
	struct jessrt_configure_node *arrayitem;
	int i;
	struct list_head *pos, *n;

	i = 0;
	arrayitem = NULL;
	pos = NULL;
	n = NULL;
	list_for_each_safe(pos, n, &arraynode->array) {
		if (i++ == which) {
			arrayitem = containing_record(pos, struct jessrt_configure_node, arrayitem);
			break;
		}
	}
	return arrayitem;
}

static void rt_safe_delete_array_item(struct jessrt_configure_node *node)
{
	struct list_head *pos;
	struct jessrt_configure_node *arrayitem;

	if (!node) {
		return;
	}

	if (!node->parentnode) {
		return;
	}

	arrayitem = NULL;

	assert(node->parentnode->arraysize > 0);

	pos = node->arrayitem.next;
	list_del_init(&node->arrayitem);

	/* decrease array index backward from @node */
	for (; pos != &(node->parentnode->array); pos = pos->next){
		arrayitem = containing_record(pos, struct jessrt_configure_node, arrayitem);
		os_snprintf(arrayitem->scope_name, sizeof(arrayitem->scope_name), "%d", atoi(arrayitem->scope_name) - 1);
	}
	--node->parentnode->arraysize;
	INIT_LIST_HEAD(&node->arrayitem);
	if (node->jsonparentptr) {
		cJSON_DetachItemViaPointer(node->jsonparentptr, node->jsonptr);
		cJSON_Delete(node->jsonptr);
	}
	free(node);
}

static void rt_suface_delete_array_item(struct jessrt_configure_node *node)
{
	struct list_head *pos;
	struct jessrt_configure_node *arrayitem;

	if (!node) {
		return;
	}

	if (!node->parentnode) {
		return;
	}

	arrayitem = NULL;

	assert(node->parentnode->arraysize > 0);

	pos = node->arrayitem.next;
	list_del_init(&node->arrayitem);

	/* decrease array index backward from @node */
	for (; pos != &(node->parentnode->array); pos = pos->next){
		arrayitem = containing_record(pos, struct jessrt_configure_node, arrayitem);
		os_snprintf(arrayitem->scope_name, sizeof(arrayitem->scope_name), "%d", atoi(arrayitem->scope_name) - 1);
	}
	--node->parentnode->arraysize;
	INIT_LIST_HEAD(&node->arrayitem);
	free(node);
}

static struct jessrt_configure_node *rt_allocate_logic_scope(struct jessrt_configure_node *parentnode, const char *scope_name)
{
	struct jessrt_configure_node *newnode;

	if (!parentnode || !scope_name ) {
		return NULL;
	}

	newnode = (struct jessrt_configure_node *)malloc(sizeof(struct jessrt_configure_node));
	assert(newnode);
	if (!newnode) {
		return NULL;
	}
	memset(newnode, 0, sizeof(*newnode));
	INIT_LIST_HEAD(&newnode->array);
	INIT_LIST_HEAD(&newnode->arrayitem);

	strcpy(newnode->scope_name, scope_name);
	newnode->jsl = JSL_LOGIC_SCOPE;
	newnode->jsonptr = cJSON_CreateObject();
	newnode->jsonparentptr = parentnode->jsonptr;
	newnode->parentnode = parentnode;
	cJSON_AddItemToObject(newnode->jsonparentptr, scope_name, newnode->jsonptr);
	parentnode->next_scope = avlinsert(parentnode->next_scope, &newnode->index, &ldr_compare_scope);
	++parentnode->next_scope_size;
	return newnode;
}

static struct jessrt_configure_node *rt_allocate_field(struct jessrt_configure_node *parentnode,
	const char *scope_name, const char *valuestring)
{
	struct jessrt_configure_node *newnode;

	if (!parentnode || !scope_name || !valuestring) {
		return NULL;
	}

	newnode = (struct jessrt_configure_node *)malloc(sizeof(struct jessrt_configure_node));
	assert(newnode);
	if (!newnode) {
		return NULL;
	}
	memset(newnode, 0, sizeof(*newnode));
	INIT_LIST_HEAD(&newnode->array);
	INIT_LIST_HEAD(&newnode->arrayitem);

	strcpy(newnode->scope_name, scope_name);
	strcpy(newnode->scope_value, valuestring);
	newnode->jsl = JSL_FIELD;
	newnode->jsonptr = cJSON_CreateString(newnode->scope_value);
	newnode->jsonparentptr = parentnode->jsonptr;
	newnode->parentnode = parentnode;
	cJSON_AddItemToObject(newnode->jsonparentptr, scope_name, newnode->jsonptr);
	parentnode->next_scope = avlinsert(parentnode->next_scope, &newnode->index, &ldr_compare_scope);
	++parentnode->next_scope_size;
	return newnode;
}

static struct jessrt_configure_node *rt_allocate_array(struct jessrt_configure_node *parentnode, const char *scope_name)
{
	struct jessrt_configure_node *newnode;

	if (!parentnode || !scope_name) {
		return NULL;
	}

	newnode = (struct jessrt_configure_node *)malloc(sizeof(struct jessrt_configure_node));
	assert(newnode);
	if (!newnode) {
		return NULL;
	}
	memset(newnode, 0, sizeof(*newnode));
	INIT_LIST_HEAD(&newnode->array);
	INIT_LIST_HEAD(&newnode->arrayitem);

	strcpy(newnode->scope_name, scope_name);
	newnode->jsl = JSL_ARRAY;
	newnode->jsonptr = cJSON_CreateArray();
	newnode->jsonparentptr = parentnode->jsonptr;
	newnode->parentnode = parentnode;
	newnode->arraysize = 0;
	cJSON_AddItemToObject(parentnode->jsonptr, scope_name, newnode->jsonptr);
	parentnode->next_scope = avlinsert(parentnode->next_scope, &newnode->index, &ldr_compare_scope);
	++parentnode->next_scope_size;
	return newnode;
}

static struct jessrt_configure_node *rt_allocate_array_item(struct jessrt_configure_node *arraynode)
{
	struct jessrt_configure_node *newnode;

	if (!arraynode) {
		return NULL;
	}

	newnode = (struct jessrt_configure_node *)malloc(sizeof(struct jessrt_configure_node));
	assert(newnode);
	if (!newnode) {
		return NULL;
	}
	memset(newnode, 0, sizeof(*newnode));
	INIT_LIST_HEAD(&newnode->array);
	INIT_LIST_HEAD(&newnode->arrayitem);

	os_snprintf(newnode->scope_name, sizeof(newnode->scope_name), "%d", arraynode->arraysize);
	os_snprintf(newnode->scope_value, sizeof(newnode->scope_value), "%s[%d]", arraynode->scope_name, arraynode->arraysize);
	newnode->jsl = JSL_ARRAY_ITEM;
	newnode->jsonptr = cJSON_CreateObject();
	newnode->jsonparentptr = arraynode->jsonptr;
	newnode->parentnode = arraynode;
	cJSON_AddItemToArray(arraynode->jsonptr, newnode->jsonptr);
	list_add_tail(&newnode->arrayitem, &arraynode->array);
	arraynode->arraysize++;
	return newnode;
}

static struct jessrt_configure_node *rt_allocate_array_field(struct jessrt_configure_node *arraynode, const char *valuestring)
{
	struct jessrt_configure_node *newnode;

	if (!arraynode || !valuestring) {
		return NULL;
	}

	newnode = (struct jessrt_configure_node *)malloc(sizeof(struct jessrt_configure_node));
	assert(newnode);
	if (!newnode) {
		return NULL;
	}

	memset(newnode, 0, sizeof(*newnode));
	INIT_LIST_HEAD(&newnode->array);
	INIT_LIST_HEAD(&newnode->arrayitem);

	os_snprintf(newnode->scope_name, sizeof(newnode->scope_name), "%d", arraynode->arraysize);
	strcpy(newnode->scope_value, valuestring);
	newnode->jsl = JSL_ARRAY_ITEM_AS_FIELD;
	newnode->jsonptr = cJSON_CreateString(newnode->scope_value);
	newnode->jsonparentptr = arraynode->jsonptr;
	newnode->parentnode = arraynode;
	cJSON_AddItemToArray(arraynode->jsonptr, newnode->jsonptr);
	list_add_tail(&newnode->arrayitem, &arraynode->array);
	arraynode->arraysize++;
	return newnode;
}

static int rt_field_update(struct jessrt_configure_node *node, const char *valuestring, char *oldvaluestring)
{
	cJSON *newone;

	if (!node || !valuestring) {
		return -EINVAL;
	}

	if ( (node->jsl != JSL_FIELD) || !node->jsonptr || !node->jsonparentptr) {
		return -1;
	}

	if (oldvaluestring) {
		strcpy(oldvaluestring, node->scope_value);
	}

	/* no change */
	if (0 == strcmp(node->scope_value, valuestring)) {
		return 1;
	}

	newone = NULL;
	if (cJSON_String == node->jsonptr->type ) {
		strcpy(node->scope_value, valuestring);
		newone = cJSON_CreateString(node->scope_value);
	}

	if (cJSON_Number == node->jsonptr->type ) {
		strcpy(node->scope_value, valuestring);
		newone = cJSON_CreateNumber(atof(node->scope_value));
	}

	if (!newone) {
		return -1;
	}

	cJSON_ReplaceItemInObject(node->jsonparentptr, node->scope_name, newone);
	node->jsonptr = newone;
	return 0;
}

static int rt_array_field_update(struct jessrt_configure_node *arraynode, int which, const char *valuestring)
{
	cJSON *newone;
	struct jessrt_configure_node *node;
	int offset;

	if (!arraynode || !valuestring) {
		return -EINVAL;
	}

	offset = which;
	if ( (arraynode->jsl != JSL_ARRAY) || !arraynode->jsonptr || offset < 0) {
		return -1;
	}

	/* user can specify '$' to reach last item of array and update it.
		otherwise update operation should return fatal. */
	if ( offset >= arraynode->arraysize ) {
		if ( JA_SYM_LAST != offset  ) {
			return -1;
		}
		offset = arraynode->arraysize - 1;
	}

	node = rt_seek_array_node(arraynode, offset);
	if (!node) {
		return -1;
	}

	/* no change */
	if (0 == strcmp(node->scope_value, valuestring)) {
		return 1;
	}

	newone = NULL;
	if (cJSON_String == node->jsonptr->type ) {
		strcpy(node->scope_value, valuestring);
		newone = cJSON_CreateString(node->scope_value);
	}

	if (cJSON_Number == node->jsonptr->type ) {
		strcpy(node->scope_value, valuestring);
		newone = cJSON_CreateNumber(atof(node->scope_value));
	}

	if (!newone) {
		return -1;
	}

	cJSON_ReplaceItemInArray(arraynode->jsonptr, offset, newone);
	node->jsonptr = newone;
	return 0;
}

static struct avltree_node_t * rt_deep_delete_range(struct jessrt_configure_node *currentnode, struct jessrt_configure_node *parentnode)
{
	struct avltree_node_t *rmindex, *root;
	struct jessrt_configure_node *arrayitem;

	if (!currentnode) {
		return NULL;
	}

	/* all the next scope should be delete */
	while (NULL != (currentnode->next_scope = rt_deep_delete_range((struct jessrt_configure_node *)currentnode->next_scope, currentnode)) ) {
		;
	}

	if ( (JSL_ARRAY == currentnode->jsl) && (!list_empty(&currentnode->array))
		&& currentnode->arraysize > 0 && parentnode)
	{
		while (!list_empty(&currentnode->array)) {
			arrayitem = list_first_entry(&currentnode->array, struct jessrt_configure_node, arrayitem);
			rt_deep_delete_range(arrayitem, currentnode);
		}

		cJSON_DeleteItemFromObject(parentnode->jsonptr, currentnode->scope_name);
		parentnode->next_scope = avlremove(parentnode->next_scope, &currentnode->index, &rmindex, &ldr_compare_scope);
		free(currentnode);
		return parentnode->next_scope;
	}

	if (JSL_ARRAY_ITEM == currentnode->jsl || JSL_ARRAY_ITEM_AS_FIELD == currentnode->jsl) {
		rt_safe_delete_array_item(currentnode);
		return NULL;
	}

	/* the handler of scope or field */
	if (parentnode) {
		parentnode->next_scope = avlremove(parentnode->next_scope, &currentnode->index, &rmindex, &ldr_compare_scope);
		--parentnode->next_scope_size;

		if (currentnode->jsonparentptr) {
			cJSON_DeleteItemFromObject(currentnode->jsonparentptr, currentnode->scope_name); /* cJSON will free json memory after detach from parent */
		} else {
			cJSON_Delete(currentnode->jsonptr);
		}
		currentnode->jsonptr = NULL;
		free(currentnode);
		return parentnode->next_scope;
	} else {
		/* the delete operator for dir */
		root = ldr_remove_scope(currentnode->scope_name, &rmindex);
		free(currentnode);
		return root;
	}
}

static struct avltree_node_t * rt_suface_delete_range(struct jessrt_configure_node *currentnode, struct jessrt_configure_node *parentnode)
{
	struct avltree_node_t *rmindex, *root;
	struct jessrt_configure_node *arrayitem;

	if (!currentnode) {
		return NULL;
	}

	/* all the next scope should be delete */
	while (NULL != (currentnode->next_scope = rt_suface_delete_range((struct jessrt_configure_node *)currentnode->next_scope, currentnode)) ) {
		;
	}

	if ( (JSL_ARRAY == currentnode->jsl) && (!list_empty(&currentnode->array))
		&& currentnode->arraysize > 0 && parentnode)
	{
		while (!list_empty(&currentnode->array)) {
			arrayitem = list_first_entry(&currentnode->array, struct jessrt_configure_node, arrayitem);
			rt_suface_delete_range(arrayitem, currentnode);
		}

		parentnode->next_scope = avlremove(parentnode->next_scope, &currentnode->index, &rmindex, &ldr_compare_scope);
		free(currentnode);
		return parentnode->next_scope;
	}

	if (JSL_ARRAY_ITEM == currentnode->jsl || JSL_ARRAY_ITEM_AS_FIELD == currentnode->jsl) {
		rt_suface_delete_array_item(currentnode);
		return NULL;
	}

	/* the handler of scope or field */
	if (parentnode) {
		parentnode->next_scope = avlremove(parentnode->next_scope, &currentnode->index, &rmindex, &ldr_compare_scope);
		--parentnode->next_scope_size;
		currentnode->jsonptr = NULL;
		free(currentnode);
		return parentnode->next_scope;
	} else {
		/* the delete operator for dir */
		root = ldr_remove_scope(currentnode->scope_name, &rmindex);
		free(currentnode);
		return root;
	}
}

static int rt_delete_any(struct jessrt_configure_node *node, JSL force)
{
	if (!node) {
		return -EINVAL;
	}

	if (force != node->jsl) {
		return -EINVAL;
	}

	rt_deep_delete_range(node, node->parentnode);
	return 0;
}

#define  rt_delete_field(object)		rt_delete_any(object, JSL_FIELD)
#define  rt_delete_scope(object)		rt_delete_any(object, JSL_LOGIC_SCOPE)
#define  rt_delete_array(object)		rt_delete_any(object, JSL_ARRAY)

static int rt_suface_delete_any(struct jessrt_configure_node *node, JSL force)
{
	if (!node) {
		return -EINVAL;
	}

	if (force != node->jsl) {
		return -EINVAL;
	}

	rt_suface_delete_range(node, node->parentnode);
	return 0;
}

#define  rt_suface_delete_field(object)		rt_suface_delete_any(object, JSL_FIELD)
#define  rt_suface_delete_scope(object)		rt_suface_delete_any(object, JSL_LOGIC_SCOPE)
#define  rt_suface_delete_array(object)		rt_suface_delete_any(object, JSL_ARRAY)

static
struct avltree_node_t * rt_delete_archive(struct jessrt_configure_node *node)
{
	if (!node) {
		return NULL;
	}

	if (JSL_ARCHIVE != node->jsl) {
		return NULL;
	}

	fs_delete_file(node->section);
	return rt_deep_delete_range(node, node->parentnode);
}

static struct avltree_node_t * rt_suface_delete_archive(struct jessrt_configure_node *node)
{
	if (!node) {
		return NULL;
	}

	if (JSL_ARCHIVE != node->jsl) {
		return NULL;
	}

	return rt_deep_delete_range(node, node->parentnode);
}

static struct avltree_node_t * rt_delete_dir(struct jessrt_configure_node *node)
{
	if (!node) {
		return NULL;
	}

	if (JSL_DIR != node->jsl) {
		return NULL;
	}

	fs_delete_dir(node->scope_name);
	return rt_deep_delete_range(node, node->parentnode);
}

static struct avltree_node_t * rt_suface_delete_dir(struct jessrt_configure_node *node)
{
	if (!node) {
		return NULL;
	}

	if (JSL_DIR != node->jsl) {
		return NULL;
	}

	return rt_suface_delete_range(node, node->parentnode);
}

int rt_reach_last_scope(JRP *jrp, struct jessrt_configure_node **output)
{
	int retval;
	struct jessrt_configure_node *node, searcher, *arrayitem;
	struct avltree_node_t *tree, *found;
	nsp_boolean_t reach_array_node;
	int i;
	struct jessrt_segment *segmptr;

	if (!jrp || !output) {
		return -EINVAL;
	}

	/* initial output pointer to be null */
	*output = NULL;
	retval = 0;
	node = NULL;
	tree = ldr_root();
	reach_array_node = NO;

	for ( i = 0; i < jrp->depth; i++) {
		segmptr = &jrp->segm[i];

		if (YES == reach_array_node) {
    		retval = -1;
    		break;
    	}

    	strcpy(searcher.scope_name, segmptr->name);
    	found = avlsearch(tree, &searcher.index, &ldr_compare_scope);
    	if (!found) {
    		retval = -1;
    		break;
    	}
    	node = rt_covert_node_byavl(found);

    	do {
    		/* dir/file/scope type, direct to next level scope */
			if (JSL_ARRAY != node->jsl) {
				tree = node->next_scope;
				break;
			}

			/* if specify the direct array node item, query command MUST reach last segment now,
					upon this, array node will be return, otherwise, query fail */
			if (segmptr->index < 0) {
				reach_array_node = 1;
				break;
			}

			/* the request array index outof maximum */
			if (segmptr->index >= node->arraysize) {
				retval = -1;
				break;
			}

			arrayitem = rt_seek_array_node(node, segmptr->index);
			if (!arrayitem) {
				retval = -1;
				break;
			}

			/* the array node do not have @next_scope, use @node->array.next_scope to index the children nodes */
			tree = arrayitem->next_scope;
			node = arrayitem;
    	} while (0);

    	if (retval < 0) {
    		break;
    	}
	}

    if (retval >= 0 && node) {
    	*output = node;
    }

    return retval;
}

int rt_search(JRP *jrp)
{
	int retval;
	struct avltree_node_t *found, *tree;
	struct jessrt_configure_node *node, searcher, *item;
	struct jessrt_segment *segmptr;
	int i;

	retval = 0;

	tree = ldr_root();
	for (i = 0; i < jrp->depth; i++) {

		/* roll back the return value */
		retval = 0;
		segmptr = &jrp->segm[i];
		strcpy(searcher.scope_name, segmptr->name);
		if (NULL == (found = avlsearch(tree, &searcher.index, &ldr_compare_scope)) ) {
			retval = -1;
			break;
		}
    	node = rt_covert_node_byavl(found);

    	if (JSL_LOGIC_SCOPE == node->jsl) {
    		/* real node type is logic scope but request level not, this request should fault */
    		if (JSL_LOGIC_SCOPE != segmptr->jsl) {
    			retval = -1;
    			break;
    		}

    		/* the last level is a logic scope, this read request should fault */
    		if (i == jrp->depth - 1) {
    			retval = -1;
    			break;
    		}

    		tree = node->next_scope;
    		continue;
    	}

    	/* the request is going to read value of a dir or archive,
    		function returns immediately and successfully, otherwise, relocate the tree pointer to next scope. */
    	if (JSL_DIR == node->jsl || JSL_ARCHIVE == node->jsl) {
    		if (i == jrp->depth - 1) {
    			strcpy(jrp->dupvalue, node->scope_value);
    			break;
    		} else {
    			tree = node->next_scope;
    			continue;
    		}
    	}

    	if (JSL_FIELD == node->jsl) {
    		strcpy(jrp->dupvalue, node->scope_value);
    		/* request command like : 'dir.archive.scope.field.unknown.unknown' NOT allow */
    		retval = ((i == jrp->depth - 1) ? 0 : -1);
    		if (retval >= 0) {
    			if (node->jsl != segmptr->jsl) {
    				retval = -1;  /* 'dir.archive.scope.filed[+]' NOT allow */
    			}
    		}
    		break;
    	}

    	if (JSL_ARRAY == node->jsl) {
    		 /* array syntax must have the next request node, otherwise it will always be error syntax. */
    		if (JSL_ARRAY != segmptr->jsl || list_empty(&node->array) || node->arraysize <= 0 ) {
    			retval = -1;
    			break;
    		}

    		/* query array size are now support, but it MUST be the last segment of request command,
				command canbe: 'dir.archive.socp.array[?]' */
			if (JA_SYM_QUERY_SIZE == segmptr->index) {
				os_snprintf(jrp->dupvalue, sizeof(jrp->dupvalue), "%d", node->arraysize);
				retval = ((i == jrp->depth - 1) ? 0 : -1);
				break;
			}

			/* add array item request are NOT support in query method */
			if (JA_SYM_ADD == segmptr->index || JA_SYM_ENTIRE == segmptr->index) {
				retval = -1;
				break;
			}

			/* direct access array item are not allow,for example:
				command : 'dir.archive.scope.array' will get a error,
				wildcard '$' are support to locate to last item of array */
			if (JA_SYM_LAST == segmptr->index ) {
				item = rt_seek_array_node(node, node->arraysize - 1);
			} else if (segmptr->index < node->arraysize && segmptr->index >= 0 ) {
				item = rt_seek_array_node(node, segmptr->index);
			} else {
				item = NULL;
			}

			/* the query index is out of range */
			if (!item) {
				retval = -1;
				break;
			}
			tree = item->next_scope;

			/* this item may be a array sub object */
			if ( JSL_ARRAY_ITEM == item->jsl) {
				retval = -1;
				continue;
			}

			/* this item is a direct field in a array,
				in this case, next level request in requester MUST be illegal  */
			if ( JSL_ARRAY_ITEM_AS_FIELD == item->jsl ) {
				strcpy(jrp->dupvalue, item->scope_value);
				retval = ((i == jrp->depth - 1) ? 0 : -1);
				break;
			}
			break;
    	}

    	retval = -1;
    	break;
	}

    return retval;
}

static int rt_append(struct jessrt_configure_node *node, const JRP *jrp, int offset, struct jessrt_configure_node **archiveptr)
{
	struct jessrt_configure_node *newnode;
	const struct jessrt_segment *segmptr;
	int dirid;
	int section;
	struct fs_directory *dirptr;

	if (!node || !jrp ) {
		return -1;
	}

	if (offset >= jrp->depth) {
		return 0;
	}

	segmptr = &jrp->segm[offset];
	if (JSL_FIELD == segmptr->jsl) {
		if (rt_allocate_field(node, segmptr->name, jrp->valuestring.cstrptr)) {
			return 0;
		}
		return -1;
	}

	if (JSL_LOGIC_SCOPE == segmptr->jsl) {
		if ((newnode = rt_allocate_logic_scope(node, segmptr->name)) == NULL ) {
			return -1;
		}
		return rt_append(newnode, jrp, offset + 1, archiveptr);
	}

	if (JSL_ARCHIVE == segmptr->jsl ) {
		section = -1;
		dirptr = fs_search_dir_byname(node->scope_name);
		if (dirptr){
			dirid = dirptr->id;

			/* file MUST under dir level */
			if (dirid > 0) {
				section = fs_allocate_file(segmptr->name, dirid, 0, NULL);
				if (section < 0) {
					return -1;
				}
			}
		}

		/* alloc from runtime space, nomatter allocation from filesystem fault or success */
		if ((newnode = ldr_create_archive(node, segmptr->name, NULL)) == NULL) {
			if (section >= 0) {
				fs_delete_file(section);
			}
			return -1;
		}
		newnode->section = section;

		if (archiveptr) {
			*archiveptr = newnode;
		}

		return rt_append(newnode, jrp, offset + 1, archiveptr);
	}

	if (JSL_ARRAY == segmptr->jsl) {
		if (node->arraysize < 0 || (segmptr->index != node->arraysize && JA_SYM_ADD != segmptr->index)) {
			return -1;
		}

		if (node->arraysize >= MAX_ARY_SIZE) {
			rt_fatal("array size over limit.");
			return -1;
		}

		/* add a not exist array into current context */
		if (JSL_ARRAY != node->jsl) {
			newnode = rt_allocate_array(node, segmptr->name);
			if (!newnode) {
				return -1;
			}
			/* offset do NOT atomic add, continue use current layer */
			return rt_append(newnode, jrp, offset, archiveptr);
		}

		/* array in key is the last segment, it means value type should be JSL_ARRAY_ITEM_AS_FIELD */
		if (offset == jrp->depth - 1) {
			newnode = rt_allocate_array_field(node, jrp->valuestring.cstrptr);
			if (!newnode) {
				return -1;
			}
		} else {
			newnode = rt_allocate_array_item(node);
			if (!newnode) {
				return -1;
			}
			return rt_append(newnode, jrp, offset + 1, archiveptr);
		}
	}
	return 0;
}

static int rt_delete(struct jessrt_configure_node *node, const JRP *jrp, struct jessrt_configure_node **archiveptr, char *oldvaluestring)
{
	const struct jessrt_segment *segmptr;
	struct jessrt_configure_node *arrayitem;

	if (JSL_FIELD == node->jsl) {
		if (oldvaluestring) {
			strcpy(oldvaluestring, node->scope_value);
		}
		return rt_delete_field(node);
	}

	if (JSL_LOGIC_SCOPE == node->jsl) {
		return rt_delete_scope(node);
	}

	if (JSL_ARRAY == node->jsl) {
		segmptr = &jrp->segm[jrp->depth - 1];
	 	if (node->arraysize <= 0 || list_empty(&node->array) ) {
	 		return -1;
	 	}
	 	/* no specify index upon array, delete entire array */
	 	if (segmptr->index < 0) {
	 		return rt_delete_array(node);
	 	}
	 	if (segmptr->index >= 0 && segmptr->index < node->arraysize) {
	 		arrayitem = rt_seek_array_node(node, segmptr->index);
			if (oldvaluestring) {
				strcpy(oldvaluestring, arrayitem->scope_value);
			}
	 		return rt_delete_any(arrayitem, arrayitem->jsl);
	 	}
	 	if (JA_SYM_LAST == segmptr->index ) {
	 		arrayitem = rt_seek_array_node(node, node->arraysize - 1);
			if (oldvaluestring) {
				strcpy(oldvaluestring, arrayitem->scope_value);
			}
	 		return rt_delete_any(arrayitem, arrayitem->jsl);
	 	}
	 	if (JA_SYM_ENTIRE == segmptr->index) {
	 		/* remove array node one by one, but NOT reduce the arraysize field in node, for it will be automatic decrase in @rt_delete_any  */
	 		while (node->arraysize > 0) {
	 			arrayitem = rt_seek_array_node(node, node->arraysize - 1);
	 			rt_delete_any(arrayitem, arrayitem->jsl);
	 		}
	 		/* remove the array container after entire element of array have been removed. */
	 		return rt_delete_any(node, node->jsl);
	 	}
	}

	if (JSL_ARCHIVE == node->jsl) {
		rt_delete_archive(node);
		if (archiveptr) {
			*archiveptr = NULL;
		}
		return 0;
	}

	return -1;
}

/* @rt_reach_last_match_scope function search from AVL container,
	try test segment scope name in @jrp and config database until type missmatch or reach the bottom of stroage stack,
	in success, the retrun value indicate the offset which found by procedure, @last @previous @archiveptr will set appropriately. */
static int rt_reach_last_match_scope(JRP *jrp,
	struct jessrt_configure_node **last, struct jessrt_configure_node **previous, struct jessrt_configure_node **archiveptr)
{
	int i;
	struct jessrt_segment *segmptr;
	struct jessrt_configure_node *node, searcher, *arrayitem;
	struct avltree_node_t *tree, *found;
	int retval;

	if (!jrp || !last || !previous) {
		return -1;
	}

	if (archiveptr) {
		*archiveptr = NULL;
	}


	node = NULL;
	*last = NULL;
	*previous = NULL;

	retval = 0;
	tree = ldr_root();

	for (i = 0; i < jrp->depth; i++) {
		node = NULL;
		segmptr = &jrp->segm[i];

		/* request node no found NOT a error, one of the possibility is acquire to add object into current tree*/
		strcpy(searcher.scope_name, segmptr->name);
		if (NULL == (found = avlsearch(tree, &searcher.index, &ldr_compare_scope)) ) {
			break;
		}
    	node = rt_covert_node_byavl(found);
    	*previous = node;

		/* save the top node pointer maybe helpful */
		if (JSL_ARCHIVE == node->jsl && archiveptr) {
			*archiveptr = node;
		}

    	/* when node item jsl is array but command element is not, break the loop as a error  */
    	if ((JSL_ARRAY == node->jsl || JSL_ARRAY == segmptr->jsl) && node->jsl != segmptr->jsl) {
    		retval = -1;
    		break;
    	} else {
    		segmptr->jsl = node->jsl;
    	}

    	/* determine the next socpe pointer,differential dispose array and logic scope */
    	if (JSL_ARRAY == node->jsl) {
			/*add array item while index equal array size*/
			if (node->arraysize >= 0 && segmptr->index == node->arraysize){
				node = NULL;
				break;
			}

			/* operator on a empty array MUST fails except add request */
    		if (node->arraysize <= 0 || list_empty(&node->array) ) {
    			if (JA_SYM_ADD == segmptr->index) {
    				node = NULL;
    				break;
    			}
    			retval = -1;
    			break;
    		}

    		if (segmptr->index >= 0 && segmptr->index < node->arraysize) {
    			arrayitem = rt_seek_array_node(node, segmptr->index);
    			if (arrayitem) {
    				tree = arrayitem->next_scope;
    			}
    			*previous = arrayitem;
    		} else {
    			if (JA_SYM_LAST == segmptr->index) {
    				arrayitem = rt_seek_array_node(node, node->arraysize - 1);
    				if (arrayitem) {
	    				tree = arrayitem->next_scope;
	    			}
    				*previous = arrayitem;
    			} else if (JA_SYM_QUERY_SIZE == segmptr->index) {
    				retval = -1;
    				break;
    			} else if (JA_SYM_ADD == segmptr->index) {
    				node = NULL;
    				break;
    			} else if (JA_SYM_ENTIRE == segmptr->index) {
    				break;
    			}else {
    				retval = -1;
    				break;
    			}
    		}
    	} else {
    		tree = node->next_scope;
    	}
    	found = NULL;
	}

	if (retval < 0) {
		return -1;
	}

	*last = node;
	return i;
}

int rt_config_update(JRP *jrp, struct jessrt_configure_node **archiveptr)
{
	struct jessrt_configure_node *node, *previous;
	int retval, offset;
	char oldvaluestring[MAX_VALUE_SIZE];
	char check_point[MAXPATH + MAX_VALUE_SIZE + 1];

	oldvaluestring[0] = 0;
	retval = -1;
	previous = NULL;
	node = NULL;

	if (0 == jrp->dupvalue[0] || !jrp->valuestring.cstrptr) {
		return -1;
	}

	do {
		/* below method overview:
			1. all element in jrp have been found: 		(node)
			2. at least one element in jrp no found :	(!node && previous), (offset - 1) is the first element mismatch in jrp segment */
		offset = rt_reach_last_match_scope(jrp, &node, &previous, archiveptr);
		if (offset < 0) {
			break;
		}

		/* request element list are fully matched existing, most of delete request can be performed,
			field/array_field update request can be performed too */
		if ( node ) {
			if (0 == strcmp(KEYWORD_DELETED, jrp->valuestring.cstrptr)) {
				retval = rt_delete(node, jrp, archiveptr, oldvaluestring);

				if (retval >= 0 && node->section <= 0
					&& (strstr(jrp->dupkey, "proc.jess.checkpoint") == jrp->dupkey)
					&& *oldvaluestring){
					os_choose_dir(NULL, check_point, NULL, NULL, NULL);
					strcat(check_point, oldvaluestring);
					os_delete_file(check_point);
				}
				break;
			}

			/*forced type matching on the last node*/
			if (JSL_FIELD == node->jsl && node->jsl == jrp->segm[jrp->depth - 1].jsl) {
				retval = rt_field_update(node, jrp->valuestring.cstrptr, oldvaluestring);
				break;
			}

			if (JSL_ARRAY == node->jsl) {
				retval = rt_array_field_update(node, jrp->segm[jrp->depth - 1].index, jrp->valuestring.cstrptr);
				break;
			}

			break;
		}

		/*request irp contains invalid fields, for example:
			request line: 		a.b.c.d.e=0		but,
			actual pair:	 	a.b.c.d=1

			request line:		a.b.c[0].d=0	but,
			actual pair:		a.b.c[0]=0

			'e' does not existed, in this case, we should failure the request call,break possibility:
			1. break by avlsearch and previous found node's level is JSL_FIELD
			2. break by avlsearch and previous found node's level is JSL_ARRAY_ITEM_AS_FIELD

			request element list are not fully matched existing, some add operation can be performed,
			 it MUST be break by avlsearch,
			 in this case, can NOT update any field's value string to 'deleted'.

			 take account of the below request:
			 a.b.c.d.e.f=1
			 while the actual node level layout like:
			 a.b.c
			 @rt_reach_last_match_scope invoke return the NULL node pointer but previous pointer to node @c,
			 runtime algorithm can NOT stop the legal append request */
		if (previous)  {
			if (JSL_FIELD == previous->jsl || JSL_ARRAY_ITEM_AS_FIELD == previous->jsl) {
				break;
			}

			/* request a write operation on a non exist file. dir.non_exist=value,
				but, request on a non exist file field are allow */
			if (JSL_DIR == previous->jsl && jrp->depth <= 2) {
				break;
			}

			/* attempt to delete a unexist item */
			if (0 == strcmp(jrp->valuestring.cstrptr, KEYWORD_DELETED)) {
				break;
			}

			retval = rt_append(previous, jrp, offset, archiveptr);
			break;
		}
	} while (0);

	return retval;
}

int rt_create_dir(JRP *jrp)
{
	if (!jrp) {
		return -EINVAL;
	}

	/* dir operation can not allow '=' no matter the name specify or value set */
	if (1 != jrp->depth || jrp->valuestring.cstrptr) {
		return -1;
	}

	/* 'proc' will be decline in this step */
	if (fs_allocate_dir(jrp->segm[0].name) < 0) {
		return -1;
	}

	if (!ldr_create_dir(jrp->segm[0].name)) {
		fs_delete_dir(jrp->segm[0].name);
		return -1;
	}

	return 0;
}

int rt_remove_dir(JRP *jrp)
{
	struct jessrt_configure_node *node, *cursornode;
	struct avltree_node_t *cursor;

	node = NULL;

	if (!jrp) {
		return -1;
	}

	/* dir operation can not allow '=' no matter the name specify or value set */
	if (jrp->valuestring.cstrptr) {
		return -1;
	}

	if ( 0 == strcmp(jrp->dupkey, "/") ) {
		cursornode = rt_covert_node_byavl(ldr_root());
		while (NULL != (cursor = rt_delete_dir(cursornode)) ) {
			cursornode = rt_covert_node_byavl(cursor);
		}
		fs_clear_config();
		return 0;
	}

	if (1 != jrp->depth) {
		return -1;
	}

	if (rt_reach_last_scope(jrp, &node) < 0 ) {
		return -1;
	}

	if (node) {
		rt_delete_dir(node);
		fs_override_file(node->section, node->jsonptr);
	}

	return 0;
}

int rt_suface_remove_dir(JRP *jrp)
{
	struct jessrt_configure_node *node, *cursornode;
	struct avltree_node_t *cursor;

	node = NULL;

	if (!jrp) {
		return -1;
	}

	if ( 0 == strcmp(jrp->dupkey, "/") ) {
		cursornode = rt_covert_node_byavl(ldr_root());
		while (NULL != (cursor = rt_suface_delete_dir(cursornode)) ) {
			cursornode = rt_covert_node_byavl(cursor);
		}
		return 0;
	}

	if (1 != jrp->depth) {
		return -1;
	}

	if (rt_reach_last_scope(jrp, &node) < 0 ) {
		return -1;
	}

	if (node) {
		rt_suface_delete_dir(node);
	}

	return 0;
}

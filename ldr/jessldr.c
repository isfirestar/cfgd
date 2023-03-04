#include "jessldr.h"

#include "ifos.h"
#include "threading.h"

#include "runtime.h"
#include "jessos.h"
#include "jessfs.h"

static const char *__jessldr_scope_level_name[] = { "dir", "archive", "logic", "array", "item", "filed"};
// static const char __jessldr_invalid_scope_name_component_character[] = {
// 	'.', '[', ']', '$', '?', '/', '\\', ',','%', '#', '+', '='
// };
static const char __jessldr_invalid_scope_name_component_character[] = {
	'.', '[', ']', '$', '?', ',','%', '#', '+', '='
};

struct jessldr_server
{
	struct avltree_node_t *root;
	char configdir[MAXPATH];
	lwp_mutex_t mutex;
};
static struct jessldr_server __jessldr_server = {.root = NULL };

int ldr_compare_scope(const void *left, const void *right)
{
	const struct jessrt_configure_node *left_node = (struct jessrt_configure_node *)left;
	const struct jessrt_configure_node *right_node = (struct jessrt_configure_node *)right;
	return os_strcasecmp(left_node->scope_name, right_node->scope_name);
}

struct avltree_node_t *ldr_root()
{
	return __jessldr_server.root;
}

const struct avltree_node_t *ldr_constant_root()
{
	return __jessldr_server.root;
}

struct avltree_node_t *ldr_remove_scope(const char *scope_name, struct avltree_node_t **rmindex)
{
	struct jessrt_configure_node target;

	if (!scope_name || !rmindex) {
		return NULL;
	}
	strcpy(target.scope_name, scope_name);
	__jessldr_server.root = avlremove(__jessldr_server.root, &target.index, rmindex, &ldr_compare_scope);
	return __jessldr_server.root;
}

/* important: if keyword '.' or '[' or ']' include in node's name, this node is going to be scrapped.
	if the keyword exists in the @scope_name, it may cause errors in the indexing proc.*/
static int ldr_organize_scope_name(const char *origin, char *scope_name, nsp_boolean_t success_on_overflow)
{
	static const char OUT_OF_LIMIT[] = "(jessldr:out-of-limit)";
	static const size_t out_of_limit_len = sizeof(OUT_OF_LIMIT);
	char *cursor;
	size_t i, j;

	if (!origin) {
		return NO;
	}

	i = 0;

	if (scope_name) {
		cursor = scope_name;
		while (0 != (*cursor = *origin) ) {
			if (i++ == (MAX_PRE_SEG_SIZE - 1) ) {
				rt_fatal("name over limit.");
				if (!success_on_overflow) {
					return -1;
				}
				memset(&scope_name[SCOPE_SAVELEN_OUTOF_LIMIT], 0, MAX_PRE_SEG_SIZE - SCOPE_SAVELEN_OUTOF_LIMIT);
				strncpy(&scope_name[SCOPE_SAVELEN_OUTOF_LIMIT], OUT_OF_LIMIT, out_of_limit_len);
				return (int)i;
			}

			for (j = 0; j < sizeof(__jessldr_invalid_scope_name_component_character); j++) {
				if (*cursor == __jessldr_invalid_scope_name_component_character[j]) {
					rt_alert("invalidate character %c in scope name", *cursor);
					return -1;
				}
			}

			cursor++;
			origin++;
		}
		return (int)((0 == i) ? (-1) : (i));
	}

	i = strlen(origin);
	return (int)( ( (i >= MAX_PRE_SEG_SIZE) || (i == 0) )? (-1) : (i));
}

static void ldr_next_misc(cJSON *jsonptr, cJSON *jsonparentptr, struct jessrt_configure_node *parentnode)
{
	struct jessrt_configure_node *node, *arrayitem;
	int i;
	size_t jsonstrlen;

	if (!jsonptr) {
		return;
	}

	/* for anonymous node,
		numeric type willbe ignore,
		object type of array type willbe directly indexing the next logical level */
	if (!jsonptr->string) {
		if (cJSON_Number != jsonptr->type) {
			if (cJSON_Array == jsonptr->type || cJSON_Object == jsonptr->type) {
				ldr_next_misc(jsonptr->child, jsonptr, parentnode);
			} else {
				ldr_next_misc(jsonptr->next, jsonparentptr, parentnode);
			}
		}
		return;
	}

	node = NULL;

	do {
		node = (struct jessrt_configure_node *)malloc(sizeof(struct jessrt_configure_node));
		assert(node);
		memset(node, 0, sizeof(struct jessrt_configure_node));
		INIT_LIST_HEAD(&node->array);
		INIT_LIST_HEAD(&node->arrayitem);

		if ( cJSON_Object == jsonptr->type ) {
			if ( ldr_organize_scope_name(jsonptr->string, node->scope_name, YES) > 0 ) {
				node->jsl = JSL_LOGIC_SCOPE;
				ldr_next_misc(jsonptr->child, jsonptr, node);
				break;
			}
		}

		if (cJSON_Number == jsonptr->type) {
			if ( ldr_organize_scope_name(jsonptr->string, node->scope_name, YES) > 0 ) {
				node->jsl = JSL_FIELD;
				os_snprintf(node->scope_value, sizeof(node->scope_value), "%.3f", jsonptr->valuedouble);
				break;
			}
		}

		if ( (cJSON_String == jsonptr->type || cJSON_Raw == jsonptr->type) && jsonptr->valuestring) {
			if ( ldr_organize_scope_name(jsonptr->string, node->scope_name, YES) > 0 ) {
				if ( (jsonstrlen = strlen(jsonptr->valuestring)) >= MAX_VALUE_SIZE ) {
					rt_fatal("string length over limit:%d", jsonstrlen);
					strncpy(node->scope_value, jsonptr->valuestring, SCOPE_SAVELEN_OUTOF_LIMIT);
					strcat(node->scope_value, "(jessldr:out-of-limit)");
				} else {
					strcpy(node->scope_value, jsonptr->valuestring);
				}
				node->jsl = JSL_FIELD;
				break;
			}
		}

		if (cJSON_Array == jsonptr->type) {
			node->jsl = JSL_ARRAY;
			strcpy(node->scope_name, jsonptr->string);
			node->arraysize = cJSON_GetArraySize(jsonptr);
			for (i = 0; i < node->arraysize; i++) {
				arrayitem = (struct jessrt_configure_node *)malloc(sizeof(struct jessrt_configure_node));
				assert(arrayitem);
				memset(arrayitem, 0, sizeof(struct jessrt_configure_node));
				INIT_LIST_HEAD(&arrayitem->array);
				INIT_LIST_HEAD(&arrayitem->arrayitem);
				list_add_tail(&arrayitem->arrayitem, &node->array);

				os_snprintf(arrayitem->scope_name, sizeof(arrayitem->scope_name), "%d", i);
				arrayitem->parentnode = node;
				arrayitem->jsonparentptr = jsonptr;
				arrayitem->jsonptr = cJSON_GetArrayItem(jsonptr, i);
				if (arrayitem->jsonptr->type == cJSON_Object) {
					arrayitem->jsl = JSL_ARRAY_ITEM;
					os_snprintf(arrayitem->scope_value, sizeof(arrayitem->scope_value), "%s[%d]", node->scope_name, i);
					ldr_next_misc(arrayitem->jsonptr, jsonptr, arrayitem);
				} else if (arrayitem->jsonptr->type == cJSON_String) {
					arrayitem->jsl = JSL_ARRAY_ITEM_AS_FIELD;
					if ( (jsonstrlen = strlen( arrayitem->jsonptr->valuestring)) >= MAX_VALUE_SIZE ) {
						rt_fatal("array item value length over limit, length:%d", jsonstrlen);
						strncpy(arrayitem->scope_value, arrayitem->jsonptr->valuestring, 64);
						strcat(arrayitem->scope_value, "(jessldr:out-of-limit)");
					} else {
						strcpy(arrayitem->scope_value, arrayitem->jsonptr->valuestring);
					}
				} else if (arrayitem->jsonptr->type == cJSON_Number) {
					arrayitem->jsl = JSL_ARRAY_ITEM_AS_FIELD;
					os_snprintf(arrayitem->scope_value, sizeof(arrayitem->scope_value), "%.3f", arrayitem->jsonptr->valuedouble);
				} else{
					;
				}
			}
			break;
		}

		free(node);
		node = NULL;
	} while(0);

	if (node) {
		node->jsonptr = jsonptr;
		node->jsonparentptr = jsonparentptr;
		node->parentnode = parentnode;
		parentnode->next_scope = avlinsert(parentnode->next_scope, &node->index, &ldr_compare_scope);
		++parentnode->next_scope_size;
	}

	ldr_next_misc(jsonptr->next, jsonparentptr, parentnode);
}

static cJSON * ldr_parse_posix_file(const char *path, char **fdata, int64_t *fsize)
{
	file_descriptor_t fd;
	char *filedata;
	cJSON *jsonptr;
	int64_t filesize;
	int retval;

	if (!path) {
		return NULL;
	}

	filedata = NULL;
	fd = INVALID_FILE_DESCRIPTOR;
	jsonptr = NULL;

	do {
		if ( (retval = ifos_file_open(path, FF_RDACCESS | FF_OPEN_EXISTING, 0644, &fd)) < 0) {
			rt_alert("ifos_file_open failed on file: %s with error:%d", path, retval);
			break;
		}

		filesize = ifos_file_fgetsize(fd);
		if ( filesize <= 0 || filesize >= FS_MAX_FILE_SIZE) {
			rt_alert("ifos_file_fgetsize failed on file: %s with error:%d", path, filesize);
			break;
		}

		filedata = malloc((size_t)filesize);
		assert(filedata);
		if (!filedata) {
			break;
		}

		if ( (retval = ifos_file_read(fd, filedata, (int)filesize)) != filesize ) {
			rt_alert("ifos_file_read failed on file: %s with error:%d", path, retval);
			break;
		}

		jsonptr = cJSON_Parse(filedata);
		if (!jsonptr) {
			rt_alert("parse to json failed, %s.", path);
		}
	} while (0);

	ifos_file_close(fd);

	if (filedata) {
		free(filedata);
	}

	if (jsonptr) {
		if (fdata) {
			*fdata = cJSON_PrintUnformatted(jsonptr);
			*fsize = strlen(*fdata);
		}
	}

	return jsonptr;
}

static int ldr_config_load_archive(struct jessrt_configure_node *dirnode, int dir_id)
{
	void *dirp;
	char file[MAXPATH], filepath[MAXPATH + MAX_VALUE_SIZE];
	char *chr;
	cJSON *jsonptr;
	char *fdata;
	int64_t fsize;
	size_t jsonstrlen;
	int n;

	dirp = os_create_dirview(dirnode->scope_value);
	if (!dirp) {
		rt_alert("failed open director %s, error:%d", dirnode->scope_value, os_errno());
		return -1;
	}

	while ( NULL != (dirp = os_foreach_file(dirp, dirnode->scope_value, file)) ) {
		n = os_snprintf(filepath, sizeof(filepath), "%s%s", dirnode->scope_value, file);
		if ( n >= sizeof(filepath)) {
			continue;
		}

		/* using @strrchr is more fast, but,
			we can't evade some special illegal situations, for example:
			'vehicle.more.json'
			in this case, the keyword '.' will block the search. */
		if (NULL == (chr = strchr(file, '.')) ) {
			rt_alert("[%s] no reg file", file);
			continue;
		}

		if (0 != os_strcasecmp(".json", chr)) {
			rt_alert("[%s] not json file or name illegal.", file);
			continue;
		}

		if ( ldr_organize_scope_name(file, NULL, NO) < 0 ) {
			continue;
		}

		jsonstrlen = strlen(file);
		if ( jsonstrlen >= MAX_PRE_SEG_SIZE ) {
			rt_fatal("file name length out of limit:%d", jsonstrlen);
			continue;
		}

		fdata = NULL;
		jsonptr = ldr_parse_posix_file(filepath, &fdata, &fsize);
		if (jsonptr) {
			fs_allocate_file(file, dir_id, (int)fsize, (const unsigned char *)fdata);
		}
		if (fdata && fsize > 0) {
			free(fdata);
		}
	}
	os_close_dirview(dirp);
	return 0;
}

static int ldr_config_load_dir(const char *name)
{
	void *dirp;
	struct jessrt_configure_node dirnode;
	char nextdir[128];
	int dir_id;

	if (!name) {
		return -EINVAL;
	}

	dirp = os_create_dirview(name);
	if (!dirp) {
		rt_alert("failed open dir [%s] error:%d", name, os_errno());
		return -1;
	}

	while ( NULL != (dirp = os_foreach_dir(dirp, name, nextdir)) ) {
		memset(&dirnode, 0, sizeof(dirnode));
		if ( ldr_organize_scope_name(nextdir, dirnode.scope_name, NO) <= 0 ) {
			continue;
		}
		dirnode.jsl = JSL_DIR;
		os_snprintf(dirnode.scope_value, sizeof(dirnode.scope_value), "%s%s%c", name, nextdir, POSIX__DIR_SYMBOL);
		dir_id = fs_allocate_dir(nextdir);
		if (dir_id > 0) {
			ldr_config_load_archive(&dirnode, dir_id);
		}
	}
	os_close_dirview(dirp);
	return 0;
}

int ldr_load_config(const char *configdir)
{
	char current_level_dir[512];
	size_t srclen;

	if (!configdir) {
		return -EINVAL;
	}

	if (__jessldr_server.root) {
		return 0;
	}

	srclen = strlen(configdir);
	if (configdir[srclen - 1] != POSIX__DIR_SYMBOL) {
		os_snprintf(current_level_dir, sizeof(current_level_dir), "%s%c", configdir, POSIX__DIR_SYMBOL);
	} else {
		strcpy(current_level_dir, configdir);
	}
	lwp_mutex_init(&__jessldr_server.mutex, 1);

	return ldr_config_load_dir(current_level_dir);
}

int ldr_filesystem_load_archive(struct jessrt_configure_node *dir, int dirid)
{
	cJSON *jsonptr;
	struct jessrt_configure_node *archive;
	int next;
	struct fs_archive *archiveptr;

	next = 0;
	while ((next = fs_seek_next_file(next, dirid, &archiveptr)) >= 0) {
		if (!archiveptr) {
			break;
		}

		if (0 == archiveptr->name[0]) {
			continue;
		}

		jsonptr = (cJSON *)cJSON_Parse((const char *)archiveptr->data);
		/* if @jsonptr is nil, @ldr_create_archive proc will create a new json data structer */
		if (NULL != (archive = ldr_create_archive(dir, archiveptr->name, jsonptr))) {
			archive->section = archiveptr->section;
			if (jsonptr) {
				rt_echo("loading file [%s]", archiveptr->name);
				ldr_next_misc(archive->jsonptr, NULL, archive);
			}
			else {
				rt_alert("file [%s] are not json format", archiveptr->name);
			}
		}
	}
	return 0;
}

int ldr_filesystem_load_dir()
{
	struct jessrt_configure_node *node;
	int next;
	struct fs_directory *dirptr;

	next = 0;
	while ((next = fs_seek_next_dir(next, &dirptr)) > 0) {
		if (!dirptr) {
			break;
		}

		if (0 == dirptr->name[0]) {
			continue;
		}

		node = (struct jessrt_configure_node *)malloc(sizeof(struct jessrt_configure_node));
		assert(node);
		memset(node, 0, sizeof(*node));

		node->jsl = JSL_DIR;
		strcpy(node->scope_name, dirptr->name);
		os_snprintf(node->scope_value, sizeof(node->scope_value), "jessfs/%s%c", dirptr->name, POSIX__DIR_SYMBOL);

		rt_echo("loading dir [%s]", dirptr->name);
		__jessldr_server.root = avlinsert(__jessldr_server.root, &node->index, &ldr_compare_scope);
		ldr_filesystem_load_archive(node, dirptr->id);
	}
	return 0;
}

int ldr_load_filesystem()
{
	if (!__jessldr_server.root) {
		lwp_mutex_init(&__jessldr_server.mutex, 1);
	}

	return  ldr_filesystem_load_dir();
}

static void __ldr_view(struct avltree_node_t *tree)
{
	struct jessrt_configure_node *node;

	if (!tree) {
		return;
	}

	__ldr_view(tree->lchild);
	__ldr_view(tree->rchild);

	node = rt_covert_node_byavl(tree);
	printf("[%s]%s=%s\n", __jessldr_scope_level_name[node->jsl], node->scope_name, node->scope_value);
	if (node->next_scope) {
		__ldr_view(node->next_scope);
	}
}

void ldr_view()
{
	__ldr_view(__jessldr_server.root);
}

struct jessrt_configure_node *ldr_create_dir(const char *name)
{
	struct jessrt_configure_node *newnode;
	char newdir_path[512];
	size_t len;

	if (!name) {
		return NULL;
	}

	len = strlen(name);
	if (POSIX__DIR_SYMBOL == name[len - 1]) {
		os_snprintf(newdir_path, sizeof(newdir_path), "jessfs/%s", name);
	} else {
		os_snprintf(newdir_path, sizeof(newdir_path), "jessfs/%s%c", name, POSIX__DIR_SYMBOL);
	}

	newnode = (struct jessrt_configure_node *)malloc(sizeof(struct jessrt_configure_node));
	assert(newnode);
	if (!newnode) {
		return NULL;
	}
	memset(newnode, 0, sizeof(*newnode));
	INIT_LIST_HEAD(&newnode->array);
	INIT_LIST_HEAD(&newnode->arrayitem);

	strcpy(newnode->scope_name, name);
	strcpy(newnode->scope_value, newdir_path);
	newnode->jsl = JSL_DIR;
	newnode->jsonptr = NULL;
	newnode->jsonparentptr = NULL;
	newnode->parentnode = NULL;
	newnode->next_scope_size = 0;
	newnode->section = -1;
	__jessldr_server.root = avlinsert(__jessldr_server.root, &newnode->index, &ldr_compare_scope);
	return newnode;
}

struct jessrt_configure_node * ldr_create_archive(struct jessrt_configure_node *parentnode, const char *scope_name, cJSON *jsonptr)
{
	struct jessrt_configure_node *newnode;
	char *chr;
	char scope_value[MAX_VALUE_SIZE];
	int n;

	if (!parentnode || !scope_name ) {
		return NULL;
	}

	if (JSL_DIR != parentnode->jsl) {
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

	/* 	scope name MUST exclude file extensions,
		 scope value MUST include file extensions, but,
		 input parameters can include file extensions or not */
	chr = strrchr(scope_name, '.');
	if (chr) {
		if (0 == os_strcasecmp(".json", chr)) {
			strncpy(newnode->scope_name, scope_name, chr - scope_name);
		}
	} else {
		strcpy(newnode->scope_name, scope_name);
	}
	n = os_snprintf(scope_value, sizeof(scope_value), "%s%s.json", parentnode->scope_value, newnode->scope_name);
	if (n >= MAX_VALUE_SIZE) {
		free(newnode);
		return NULL;
	}
	strcpy(newnode->scope_value, scope_value);
	newnode->jsl = JSL_ARCHIVE;
	/* when archive loaded from file, the jsonptr should be pass from calling thread,
		otherwise, archive is a empty json object */
	newnode->jsonptr = jsonptr ? jsonptr : cJSON_CreateObject();
	newnode->jsonparentptr = NULL;
	newnode->parentnode = parentnode;
	newnode->section = -1;
	parentnode->next_scope = avlinsert(parentnode->next_scope, &newnode->index, &ldr_compare_scope);
	++parentnode->next_scope_size;
	return newnode;
}

void ldr_load_archive(cJSON *jsonptr, struct jessrt_configure_node *parentnode)
{
	if (parentnode && jsonptr){
		ldr_next_misc(jsonptr, NULL, parentnode);
	}
}

struct jessrt_configure_node *ldr_search_scope(const char *name)
{
	struct jessrt_configure_node searcher;
	struct avltree_node_t *found;

	strcpy(searcher.scope_name, name);

	/* request node no found NOT a error, one of the possibility is acquire to add object into current tree */
	if (NULL == (found = avlsearch(__jessldr_server.root, &searcher.index, &ldr_compare_scope)) ) {
		return NULL;
	}
	return rt_covert_node_byavl(found);
}

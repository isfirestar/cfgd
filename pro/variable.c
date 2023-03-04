#include "jesspro.h"

#include "runtime.h"
#include "jessfs.h"
#include "jessldr.h"
#include "jessnet.h"
#include "jessos.h"

static void pro_scan_current_scope_level(const struct avltree_node_t *tree, JRP *jrp)
{
	struct jessrt_configure_node *node;

	if (!tree) {
		return;
	}

	node = rt_covert_node_byavl(tree);
	pro_append_response(jrp->taskptr, "%s"CRLF, node->scope_name);
	pro_scan_current_scope_level(tree->lchild, jrp);
	pro_scan_current_scope_level(tree->rchild, jrp);
}

void pro_vairable_read(JRP *jrp)
{
	if (jrp->valuestring.len > 0) {
		pro_append_response(jrp->taskptr, JESS_FATAL);
		rt_fatal("request explicit associate with a value.");
		return;
	}

	if (rt_search(jrp) < 0) {
		pro_append_response(jrp->taskptr, JESS_FATAL);
		return;
	}

	pro_append_response(jrp->taskptr, JESS_OK);
	pro_append_response(jrp->taskptr, "%s=%s"CRLF, jrp->dupkey, jrp->dupvalue);
}

void pro_variable_contain(JRP *jrp)
{
	struct jessrt_configure_node *node;

	if (jrp->valuestring.len > 0) {
		pro_append_response(jrp->taskptr, JESS_FATAL);
		rt_fatal("request explicit associate with a value.");
		return;
	}

	if (0 == strcmp("/", jrp->dupkey) || 0 == strcmp("*", jrp->dupkey)) {
		pro_append_response(jrp->taskptr, JESS_OK);
		pro_scan_current_scope_level(ldr_constant_root(), jrp);
		return;
	}

	do {
		if (rt_reach_last_scope(jrp, &node) < 0) {
			pro_append_response(jrp->taskptr, JESS_FATAL);
			break;
		}

		/* the lastest scope canbe render,the response is successful,
			no matter next scope is existed or not */
		pro_append_response(jrp->taskptr, JESS_OK);
		if (node->next_scope) {
			pro_scan_current_scope_level(node->next_scope, jrp);
		}

	} while( 0 );
}

void pro_variable_write(JRP *jrp)
{
	struct jessrt_configure_node *archive;
	int retval;

	if (0 == jrp->dupvalue[0] || !jrp->valuestring.cstrptr) {
		rt_fatal("request with ambiguous written value specify.");
		pro_append_response(jrp->taskptr, JESS_FATAL);
		return;
	}

	archive = NULL;
	retval = rt_config_update(jrp, &archive);
	if (retval < 0) {
		pro_append_response(jrp->taskptr, JESS_FATAL);
		return;
	}

	/* ok, next, overwrite the archive scope if exist */
	pro_append_response(jrp->taskptr, JESS_OK);

	/* something changed */
	if (0 == retval && 0 != os_strcasecmp("proc", jrp->segm[0].name)) {
		/* archive acquire to overridden */
		if (archive) {
			fs_override_file(archive->section, archive->jsonptr);
		} else {
			/* maybe operator to delete archive or dir, it acquire follow a sync */
			fs_append_sync();
		}
		/* publish message to client which acquire it */
		jnet_publish(jrp);
	}
}

static void var_makeup_pair(const struct avltree_node_t *tree, const char *scope_name, JRP *jrp)
{
	struct jessrt_configure_node *node, *arrayitem;
	char scope_name_buff[MAX_PRE_SEG_SIZE * 3 + 4];
	char *next_scope_name;
	struct list_head *pos, *n;
	int next_scope_len;

	if (!tree) {
		return;
	}

	node = rt_covert_node_byavl(tree);

	if (scope_name) {
		next_scope_len = strlen(scope_name) + MAX_PRE_SEG_SIZE + 2;
		/* it MUST acquire additional 2 bytes to save '.' and terminate '\0' */
		next_scope_name = (char *)malloc(next_scope_len);
		assert(next_scope_name);
		if (!next_scope_name) {
			return;
		}
		os_snprintf(next_scope_name, next_scope_len, "%s.%s", scope_name, node->scope_name);
	} else {
		next_scope_name = (char *)malloc(MAX_PRE_SEG_SIZE + 1);
		assert(next_scope_name);
		if (!next_scope_name) {
			return;
		}
		strcpy(next_scope_name, node->scope_name);
	}

	switch (node->jsl) {
		case JSL_FIELD:
			pro_append_response(jrp->taskptr, "%s=%s"CRLF, next_scope_name, node->scope_value);
			break;
		case JSL_LOGIC_SCOPE:
		case JSL_ARCHIVE:
		case JSL_DIR:
			var_makeup_pair(node->next_scope, next_scope_name, jrp);
			break;
		case JSL_ARRAY:
			pos = NULL;
			n = NULL;
			list_for_each_safe(pos, n, &node->array) {
				arrayitem = containing_record(pos, struct jessrt_configure_node, arrayitem);
				if (arrayitem->jsl == JSL_ARRAY_ITEM) {
					if (scope_name) {
						os_snprintf(scope_name_buff, sizeof(scope_name_buff), "%s.%s[%s]", scope_name, node->scope_name, arrayitem->scope_name);
					}
					var_makeup_pair(arrayitem->next_scope, scope_name_buff, jrp);
				} else {
					if (arrayitem->jsl == JSL_ARRAY_ITEM_AS_FIELD) {
						pro_append_response(jrp->taskptr, "%s[%s]=%s"CRLF, next_scope_name, arrayitem->scope_name, arrayitem->scope_value);
					}
				}
			}
			break;
		case JSL_ARRAY_ITEM:
		default:
			break;
	}

	var_makeup_pair(tree->lchild, scope_name, jrp);
	var_makeup_pair(tree->rchild, scope_name, jrp);
	free(next_scope_name);
}

void pro_variable_scope(JRP *jrp)
{
	struct jessrt_configure_node *node;

	if (jrp->valuestring.len > 0) {
		pro_append_response(jrp->taskptr, JESS_FATAL);
		rt_fatal("request explicit associate with a value.");
		return;
	}

	if (0 == strcmp("/", jrp->dupkey) || 0 == strcmp("*", jrp->dupkey)) {
		pro_append_response(jrp->taskptr, JESS_OK);
		var_makeup_pair((const struct avltree_node_t *)ldr_constant_root(), NULL, jrp);
		return;
	}

	if (rt_reach_last_scope(jrp, &node) >= 0) {
		if (node->jsl == JSL_FIELD || node->jsl == JSL_ARRAY_ITEM_AS_FIELD) {
			pro_append_response(jrp->taskptr, JESS_OK"%s=%s"CRLF, jrp->dupkey, node->scope_value);
		} else {
			pro_append_response(jrp->taskptr, JESS_OK);
			if (node->jsl == JSL_ARRAY) {
				/* when the node is array type, all it's sub items are linked in @arrayitem field */
				var_makeup_pair((const struct avltree_node_t *)&node->index, node->scope_name, jrp);
			} else if (node->jsl == JSL_ARRAY_ITEM) {
				/* when the node is array item type, all it's sub items are in next scope, base key name is value string of current node */
				var_makeup_pair((const struct avltree_node_t *)node->next_scope, node->scope_value, jrp);
			}else {
				/* otherwise, link to next scope, but base key name use key string of current node */
				var_makeup_pair((const struct avltree_node_t *)node->next_scope, node->scope_name, jrp);
			}
		}
	} else {
		pro_append_response(jrp->taskptr, JESS_FATAL);
	}
}

void pro_variable_read_file(JRP *jrp)
{
	struct jessrt_configure_node *node;
	const unsigned char *data;
	int n;

	if (jrp->valuestring.len > 0) {
		pro_append_response(jrp->taskptr, JESS_FATAL);
		rt_fatal("request explicit associate with a value.");
		return;
	}

	node = NULL;
	if (rt_reach_last_scope(jrp, &node) < 0) {
		pro_append_response(jrp->taskptr, JESS_FATAL);
		return;
	}

	if (node->jsl != JSL_ARCHIVE) {
		pro_append_response(jrp->taskptr, JESS_FATAL);
		return;
	}

	pro_append_response(jrp->taskptr, JESS_OK);
	if ( ( n = fs_read_file_bysect(node->section, &data)) > 0 ) {
		pro_append_response(jrp->taskptr, "%s", (const char *)data);
	}
}

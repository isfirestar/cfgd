#if !defined JSONIFY_LOADED_H
#define JSONIFY_LOADED_H

#include "avltree.h"
#include "cJSON.h"

/* this is the standard compare routine for any avl tree method */
extern int ldr_compare_scope(const void *left, const void *right);

/* direct catch the root node of config tree,
	recommend to invoke lock object before these two function call */
extern struct avltree_node_t *ldr_root();
extern const struct avltree_node_t *ldr_constant_root();
extern struct avltree_node_t *ldr_remove_scope(const char *scope_name, struct avltree_node_t **rmindex);

/* view the current config tree,
	do NOT invoke lock object during @ldr_view proc */
extern void ldr_view();

/* load all json file as configure from specify director @configdir,
	do NOT invoke lock object during @ldr_load_config proc */
extern int ldr_load_config(const char *configdir);
extern int ldr_load_filesystem();

extern struct jessrt_configure_node *ldr_create_dir(const char *name);
extern struct jessrt_configure_node *ldr_create_archive(struct jessrt_configure_node *parentnode, const char *scope_name, cJSON *jsonptr);
extern void ldr_load_archive(cJSON *jsonptr, struct jessrt_configure_node *parentnode);


extern struct jessrt_configure_node *ldr_search_scope(const char *name);

#endif

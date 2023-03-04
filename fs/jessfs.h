#if !defined JESSFS_H
#define JESSFS_H

#include "compiler.h"

#include "threading.h"

#include "request.h"

#define FS_PAGE_SIZE					(4096)
#define FS_MAX_FILE_SIZE				(FS_PAGE_SIZE << 1)		/* 8KB pre-alloced */

#define FS_MAX_DIR						(128)
#define FS_MAX_ARCH						(256)
#define FS_MAX_ITEM_MASK				(FS_MAX_ARCH >> 3)

#define FS_MAX_BIN_SIZE        			(1 << 20)  /* 1 MBytes canbe use for mapping */

#pragma pack(push ,1)

struct fs_directory
{
	int id;				/* id <= 0 means unactive */
	char name[64];
	unsigned char unused[64];
};

struct fs_archive
{
	/* aligned to dir node type */
	int id;				/* id <= 0 means unactive */
	char name[64];
	unsigned char unused[64];

	int dirid;			/* the id of dir node, dir is parent of archive node */
	int size;
	int section;
	unsigned char data[FS_MAX_FILE_SIZE];
};

struct fs_config_layout
{
	unsigned char mask[FS_MAX_ITEM_MASK];		/* mark the file section which in use for all bits */
	struct fs_directory dir[FS_MAX_DIR];		/* pre-alloc 128 nodes for directory */
	struct fs_archive archive[FS_MAX_ARCH];
};

#pragma pack(pop)

struct fs_address_mapping
{
    unsigned char * mptr;
    uint32_t 		mlen;
    unsigned char * uptr;
#if 0
    unsigned char 	mask[FS_MAX_ITEM_MASK];	/* the dirty block data in memory, which need to flush into file */
#endif
};

struct fs_server
{
	struct fs_address_mapping 	binary;			/* binary files address mapping */
	unsigned char 				*cache;
	unsigned char 				*config;
	int 						next_node_id;
	struct fs_config_layout *layout;
	unsigned char   			*bitmask;		/* mark the file block which inuse, pointer to fs_config_layout::mask */
	lwp_mutex_t 		cache_lock;

	lwp_t 			flush_thread;
	nsp_boolean_t 					flush_exit;
	int 						count_overwritten;
	int 						permit_override;
};
extern struct fs_server __fs_server;

/* initial/uninitial method for jessfs */
extern void fs_initial();
extern void fs_uninit();
extern int fs_startup(nsp_boolean_t *reorganization);

extern int fs_load_config_database(nsp_boolean_t *reorganization);
extern void fs_load_config_cache();

/* when calls of  @fs_startup failed, call this method to inital the jess database file which save in /var/jess/
	if this call failed, program can never use memory mapping to manage disk file data */
extern int fs_allocate_dir(const char *name);
extern int fs_allocate_file(const char *name, int dirid, int size, const unsigned char *data);

/* delete functionss for directory and archive file,
	@fs_clear_config can use to truncate configure disk file, both dir and file is going to delete */
extern int fs_delete_dir(const char *name);
extern int fs_delete_file(int section);
extern int fs_clear_config();

extern void fs_update_seq(void * layout);
/* dir search methods. */
extern struct fs_directory *fs_search_dir_byname(const char *name);

/* archive search methods. */
extern struct fs_archive *fs_search_file_byname(const char *name, int dirid);

/* sequence dir/file nodes, return value will be the next index to try*/
extern int fs_seek_next_dir(int index, struct fs_directory **dirptr);
extern int fs_seek_next_file(int index, int dirid, struct fs_archive **archiveptr);

/* @node is the pointer to the archive node return by @fs_seek_next_file,
	the return value is a cJSON * pointer to the result of parse this file */
extern void *fs_analyze_file(int section);

/* overwrite file data for specify archive node associated with it's section */
extern int fs_override_file(int section, const void *jsonptr);

/* flush file data from normal cache to low level cache, return value is how many file(s) have been flushed*/
extern void fs_sync(JRP *jrp);
extern void fs_fsync(JRP *jrp);
extern void fs_append_sync();

/* call @fs_read_file read the specified file by node section if the buffer is long enough,
	otherwise call @jessfs_read_file2 to get data and free *data by calling thread */
extern int fs_read_file_bysect(int section, const unsigned char **dataptr);

/* the binary r/w method export by jessfs */
extern int fs_read_binary(int offset, int size, unsigned char **data);
extern int fs_write_binary(int offset, int size, const unsigned char *data);

/* checkpoint/snapshot method for backup/recover filesystem */
extern int fs_create_checkpoint(const char *file);
extern void fs_recover_checkpoint(JRP *jrp);

/* safe recover filesystem database file */
extern int fs_swap_database();

/* proc filesystem method */
extern int procfs_add_checkpoint(const char *file);
extern const char *procfs_get_version();
extern int procfs_rebuild();
extern int procfs_get_property(const char *field);
extern void procfs_set_property(const char *field, int n);

#endif

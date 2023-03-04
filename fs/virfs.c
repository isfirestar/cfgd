#include "virfs.h"

#include "ifos.h"
#include "atom.h"
#include "threading.h"
#include "avltree.h"

#include "cJSON.h"

#include "refos.h"
#include "order.h"

union fs_binary_head
{
    struct {
        char version[8];
    };

    char placeholder[1024];
};

struct fs_binary_layout
{
    union fs_binary_head head;
    unsigned char uptr[0];
};

struct fs_server __fs_server = {
	.config = NULL,
	.binary = { NULL, 0, NULL },
	.cache = NULL,
	.next_node_id = 0,
	.bitmask = NULL,
	.flush_exit = NO,
	.count_overwritten = 0,
	.permit_override = 1
};
const unsigned char BIT_MASK[] = { 1, 2, 4, 8, 0x10, 0x20, 0x40, 0x80 };

/* render the first director node pointer, def in inner.c */
extern struct fs_directory *fs_seek_free_dir_location(int *section);
/* render the first archive node pointer, return the section number on success, def in inner.c */
extern struct fs_archive *fs_seek_free_file_location(int *section);
/* search archive pointer by it's section holder number, def in inner.c */
extern struct fs_archive *fs_search_file_bysect(int section);
/* mark or change the bitmask of config section, def in inner.c */
extern int fs_set_section_bitmask(int index, int used);

struct jessfs_avl_node{
	struct avltree_node_t index;
	int dirid;
	int section;
};

int fs_seek_next_dir(int index, struct fs_directory **dirptr)
{
	int i;
	int n;
	struct fs_directory *cursor;

	if (!dirptr || index < 0 || index >= FS_MAX_DIR ) {
		return -1;
	}

	*dirptr = NULL;
	for (i = index; i < FS_MAX_DIR; i++) {
		cursor = &__fs_server.layout->dir[i];
		if (cursor->id > 0) {
			*dirptr = cursor;
			break;
		}
	}

	if (NULL != dirptr) {
		n = i + 1;
		return ( (n >= FS_MAX_DIR) ? (-1) : (n) );
	}

	return -1;
}

int fs_seek_next_file(int index, int dirid, struct fs_archive **archiveptr)
{
	int i;
	int n;
	struct fs_archive *cursor;

	if (!archiveptr || index < 0 || index >= FS_MAX_ARCH ) {
		return -1;
	}

	*archiveptr = NULL;
	for (i = index; i < FS_MAX_ARCH; i++) {
		cursor = &__fs_server.layout->archive[i];
		if (cursor->id > 0 && cursor->dirid == dirid) {
			*archiveptr = cursor;
			break;
		}
	}

	if (NULL != *archiveptr) {
		n = i + 1;
		return ( (n >= FS_MAX_ARCH) ? (-1) : (n) );
	}

	return -1;
}

void *fs_analyze_file(int section)
{
	struct fs_archive *archive;

	if (NULL == (archive = fs_search_file_bysect(section)) ) {
		return NULL;
	}

	/* only use in initial proc, lock does NOT necessary */
	return cJSON_Parse((const char *)archive->data);
}

int fs_read_file_bysect(int section, const unsigned char **dataptr)
{
	struct fs_archive *archive;

	if (section < 0 || section >= FS_MAX_ARCH || !dataptr ) {
		return -1;
	}

	archive = fs_search_file_bysect(section);
	if (archive) {
		archive->data[archive->size] = 0;
		*dataptr = archive->data;
		return archive->size;
	}

	return -1;
}

void fs_uninit()
{
	void *thretval;

	__fs_server.flush_exit = YES;
	lwp_join(&__fs_server.flush_thread, &thretval);

	if (__fs_server.binary.mptr) {
		os_munmap(__fs_server.binary.mptr, __fs_server.binary.mlen);
		__fs_server.binary.mptr = NULL;
	}

	if (__fs_server.cache) {
		free(__fs_server.cache);
		__fs_server.cache = NULL;
	}

	if (__fs_server.config) {
		free(__fs_server.config);
		__fs_server.config = NULL;
	}

	/* release the cache mutex */
	lwp_mutex_uninit(&__fs_server.cache_lock);
}

static int fs_zerolization_range(file_descriptor_t fd, int size)
{
    char *zeroization;
    int n;

    if (size <= 0) {
    	return -1;
    }

    ifos_file_seek(fd, 0);

    zeroization = (char *)malloc(size);
    if (!zeroization) {
        return -ENOMEM;
    }
    memset(zeroization, 0, sizeof(size));
    n = ifos_file_write(fd, zeroization, size);
    free(zeroization);
	ifos_file_flush(fd);
    return n;
}

static int fs_map_binary_region()
{
    int retval;
    file_descriptor_t fd;
    int64_t fsize;
    struct fs_binary_layout *view;
    void *addr;
	char file[MAXPATH];

    addr = NULL;
    fd = INVALID_FILE_DESCRIPTOR;

	os_choose_dir(NULL, NULL, NULL, NULL, file);

    do {
		retval = ifos_file_open(file, FF_RDACCESS | FF_WRACCESS | FF_OPEN_ALWAYS, 0644, &fd);
        if (retval < 0) {
            rt_fatal("ifos_file_open failed with error=%d", errno);
            break;
        }

        fsize = ifos_file_fgetsize(fd);
        if (fsize < 0) {
            rt_fatal("ifos_file_fgetsize failed with error=%d", errno);
            break;
        }

        /* ensure the mapping length match the current archive version */
		if(fsize < FS_MAX_BIN_SIZE) {
            retval = fs_zerolization_range(fd, FS_MAX_BIN_SIZE);
            if (retval < 0) {
                break;
            }
		}

        retval = -1;
		addr = os_mmap(fd, FS_MAX_BIN_SIZE);
		if (!addr) {
			rt_fatal("os_mmap failed with error=%d", os_errno());
			break;
		}

        view = (struct fs_binary_layout *)addr;
        if (0 != strcmp(view->head.version, "fs")) {
            memset(addr, 0, FS_MAX_BIN_SIZE);
            strcpy(view->head.version, "fs");
        }

        __fs_server.binary.uptr = view->uptr;
        __fs_server.binary.mptr = addr;
		__fs_server.binary.mlen = FS_MAX_BIN_SIZE;

        /* all ok */
        retval = 0;
		rt_echo("successful mapped binary storage region.");
    } while (0);

	if (fd >= 0) {
        ifos_file_close(fd);
    }

	if (retval < 0) {
		os_munmap(addr, FS_MAX_BIN_SIZE);
	}

    return retval;
}

int fs_load_config_database(nsp_boolean_t *reorganization)
{
	int retval;
    file_descriptor_t fd;
    int64_t fsize;
	char file[MAXPATH];
	unsigned char *buffer;

    fd = INVALID_FILE_DESCRIPTOR;
	os_choose_dir(NULL, NULL, file, NULL, NULL);

	/* create or re-create low-level cache */
	if (!__fs_server.config) {
		__fs_server.config = (unsigned char *)malloc(sizeof(struct fs_config_layout));
	}
	buffer = (unsigned char *)malloc(sizeof(struct fs_config_layout));
	assert(__fs_server.config && buffer);

    do {
		retval = ifos_file_open(file, FF_RDACCESS | FF_WRACCESS | FF_OPEN_ALWAYS, 0644, &fd);
        if ( (retval < 0) || (INVALID_FILE_DESCRIPTOR == fd) ) {
            rt_fatal("ifos_file_open failed with error=%d", retval);
            break;
        }

        fsize = ifos_file_fgetsize(fd);
        retval = (int)fsize;
        if (fsize < 0) {
            rt_fatal("ifos_file_fgetsize failed with error=%d", retval);
            break;
        }

        /* ensure the mapping length match the current archive version */
		if(fsize != sizeof(struct fs_config_layout)) {
			rt_alert("config file size:%lld not equal to sizeof(struct fs_config_layout), jess shall automatic rebuild it.", fsize);
            retval = fs_zerolization_range(fd, sizeof(struct fs_config_layout));
            if (retval < 0) {
                break;
            }

            /* size mismatch the config layout structer, maybe using old version of jess program,
				the most possibility is @ifos_file_open called create a new file */
			if (reorganization) {
				*reorganization = YES;
			}
		}

		ifos_file_seek(fd, 0);
		if ((retval = ifos_file_read(fd, buffer, sizeof(struct fs_config_layout))) != sizeof(struct fs_config_layout)) {
			rt_fatal("ifos_file_read failed with error:%d", retval);
			break;
		}

		retval = 0;
    } while (0);

    if (retval >= 0 ) {
    	/* update all index-dir in cache */
    	fs_update_seq(buffer);
    	/* copy and sve data into config memory pointer, this operation MUST running under DPC thread or jess initialize procedure */
    	lwp_mutex_lock(&__fs_server.cache_lock);
		memcpy(__fs_server.config, buffer, sizeof(struct fs_config_layout));
		lwp_mutex_unlock(&__fs_server.cache_lock);

		rt_echo("successful loaded config database.");
    }

	if (INVALID_FILE_DESCRIPTOR != fd) {
    	ifos_file_close(fd);
    }

    if (buffer) {
    	free(buffer);
    	buffer = NULL;
    }

    return retval;
}

void fs_load_config_cache()
{
	/* create or re-create high-level cache */
	if (!__fs_server.cache) {
		__fs_server.cache = (unsigned char *)malloc(sizeof(struct fs_config_layout));
	}
	assert ( __fs_server.cache);

	lwp_mutex_lock(&__fs_server.cache_lock);
	memcpy(__fs_server.cache, __fs_server.config, sizeof(struct fs_config_layout));
	lwp_mutex_unlock(&__fs_server.cache_lock);

	/* use cache pointer as layout */
	__fs_server.layout = (struct fs_config_layout *)__fs_server.cache;
	/* the bit mask is at the begin of layout memory */
	__fs_server.bitmask = __fs_server.layout->mask;
}

static void *fs_flush_check(void *p)
{
	int seconds, writes;
	static const uint64_t AUTO_SYNC_PERIOD = 1 * 1000 * 1000;/* interval set to 1 second */

	rt_echo("thread startup.");

	seconds = 0;
	writes = 0;
	while (!__fs_server.flush_exit ) {
		lwp_delay(AUTO_SYNC_PERIOD);

		writes += atom_exchange(&__fs_server.count_overwritten, 0);
		++seconds;
		if ( (0 == writes) || ( seconds < 60) ) {
			continue;
		}

		/*	fs data should flush to disk file by following rule(Imitate the 'Redis' opensource program):
			equal to or large than 10000 updates in 60 seconds,
		 	equal to or large than 10 updates in 300 seconds,
		 	equal to or large than 1 updates in 900 seconds  */
		if ( (writes >= 10000) || (seconds >= 900) || (seconds >= 300 && writes >= 10) ) {
			pro_append_sync();
			writes = seconds = 0;
		}
	}

	rt_echo("thread terminated.");
	pro_append_sync();
	return NULL;
}

void fs_initial()
{
	lwp_mutex_init(&__fs_server.cache_lock, 1);

	__fs_server.flush_exit = NO;
	if ( lwp_create(&__fs_server.flush_thread, 0, &fs_flush_check, NULL) < 0 ) {
		rt_alert("lwp_create failed, fsync function will no longer support.");
	}
}

int fs_startup(nsp_boolean_t *reorganization)
{
	char fsdir[MAXPATH];

	os_choose_dir(NULL, fsdir, NULL, NULL, NULL);

	do {
		/* existed dir is no problem*/
		if (ifos_pmkdir(fsdir) < 0) {
			break;
		}

		if (fs_map_binary_region() < 0) {
			break;
		}

		if (fs_load_config_database(reorganization) < 0) {
			break;
		}

		fs_load_config_cache();
		return 0;
	}while( 0 );

	fs_uninit();
	return -1;
}

int fs_allocate_dir(const char *name)
{
	struct fs_directory *newnode;
	int dir_section;

	if (!name) {
		return -1;
	}

	if ( 0 == os_strcasecmp(name, "proc")) {
		rt_alert("mkdir with name \'proc\'' is illegal.");
		return -EEXIST;
	}

	/* name repeat are not allow */
	if (NULL != fs_search_dir_byname(name) ) {
		rt_alert("target dir [%s] illegal or exist on alloc.", name);
		return -EEXIST;
	}

	/* memory address from config pyhsical file */
	newnode = fs_seek_free_dir_location(&dir_section);
	if (!newnode) {
		rt_alert("dir address full.");
		return -1;
	}
	memset(newnode, 0, sizeof(struct fs_directory));
	newnode->id = (1 + dir_section) << 16;
	strcpy(newnode->name , name);
	return newnode->id;
}

int fs_allocate_file(const char *name, int dirid, int size, const unsigned char *data)
{
	struct fs_archive *newnode;
	int section;

	/* size == 0 is allow */
	if (!name || size < 0 || dirid <= 0 || size >= FS_MAX_FILE_SIZE) {
		rt_fatal("illegal file alloc request, parent node:%d, size:%d", dirid, size);
		return -1;
	}

	/* name repeat are not allow */
	if (NULL != fs_search_file_byname(name, dirid)) {
		rt_alert("target file [%s] already exist on alloc.", name);
		return -EEXIST;
	}

	newnode = fs_seek_free_file_location(&section);
	if (!newnode) {
		return -1;
	}
	memset(newnode, 0, sizeof(struct fs_archive));
	newnode->section = section;
	newnode->id = dirid + section + 1;
	newnode->dirid = dirid;
	strcpy(newnode->name , name);
	newnode->size = size;
	if (size > 0 && data) {
		memcpy(newnode->data, data, newnode->size);
	}
	return newnode->section;
}

int fs_override_file(int section, const void *jsonptr)
{
	char *jsonfiledata;
	struct fs_archive *archive;
	int size;

	if (section < 0 || section >= FS_MAX_ARCH || !jsonptr ) {
		rt_alert("file section illegal:%d", section);
		return -1;
	}

	archive = fs_search_file_bysect(section);
	if (!archive) {
		return -1;
	}

	jsonfiledata = cJSON_PrintUnformatted(jsonptr);
	if (!jsonfiledata) {
		return -1;
	}
	size = (int)strlen(jsonfiledata);

	if (size <= FS_MAX_FILE_SIZE) {
		/* update size of this node */
		archive->size = size;
		/* use cached file data copy, wait for sync command to flush to disk */
		memcpy(archive->data, jsonfiledata, archive->size);
		memset(&archive->data[archive->size], 0, sizeof(archive->data) - archive->size);
		/* append next sync asynchronous request */
		fs_append_sync();
	} else {
		rt_alert("archive [%s] overflow", archive->name);
	}

	cJSON_free(jsonfiledata);
	return 0;
}

void fs_append_sync()
{
	atom_addone(&__fs_server.count_overwritten);
}

void fs_sync(JRP *jrp)
{
	/* no permission to sync data */
	if ( 0 == os_atomic_get(&__fs_server.permit_override)) {
		if (jrp) {
			pro_append_response(jrp->taskptr, JESS_FATAL);
		}
		return;
	}

	/* ignore sync request when there are no WRITE operation have been occur.
		in this situation, this JRP shall response as a success status */
	if ( 0 == os_atomic_get(&__fs_server.count_overwritten) ) {
		pro_append_response(jrp->taskptr, JESS_OK);
		return;
	}

	lwp_mutex_lock(&__fs_server.cache_lock);
	memcpy(__fs_server.config, __fs_server.cache, sizeof(struct fs_config_layout));
	lwp_mutex_unlock(&__fs_server.cache_lock);

	/* if the sync request from network, than build response */
	if (jrp) {
		pro_append_response(jrp->taskptr, JESS_OK);
	}
	/* atomic append a file synchronous request */
	pro_append_fsync();
}

int fs_swap_database()
{
	int i;
	int retval;
	char swap[MAXPATH], cfgdb[MAXPATH];

	os_choose_dir(NULL, NULL, cfgdb, swap, NULL);

	i = 0;
	while ( (i++ < 3) && (retval = os_rename_file(swap, cfgdb)) < 0 ) {
		lwp_delay(1 * 1000 * 1000);
		rt_echo("os_rename_file failed with error:%d", retval);
	}

	return retval;
}

void fs_fsync(JRP *jrp)
{
	unsigned char *buffer;
	char swap[MAXPATH];
	int retval;
	file_descriptor_t fd;

	/* no permission to sync data */
	if ( 0 == os_atomic_get(&__fs_server.permit_override)) {
		if (jrp) {
			pro_append_response(jrp->taskptr, JESS_FATAL);
		}
		return;
	}

	/* dump config file context from cache */
	buffer = (unsigned char *)malloc(sizeof(struct fs_config_layout));
	if (!buffer) {
		return;
	}
	lwp_mutex_lock(&__fs_server.cache_lock);
	memcpy(buffer, __fs_server.config, sizeof(struct fs_config_layout));
	lwp_mutex_unlock(&__fs_server.cache_lock);

	/* try to write context to temp disk file */
	do {
		os_choose_dir(NULL, NULL, NULL, swap, NULL);

		fd = INVALID_FILE_DESCRIPTOR;

		retval = ifos_file_open(swap, FF_RDACCESS | FF_WRACCESS | FF_CREATE_ALWAYS, 0644, &fd);
	    if ( (retval < 0) || (INVALID_FILE_DESCRIPTOR == fd) ) {
	    	rt_alert("ifos_file_open failed on file %s with error:%d", swap, retval);
	        break;
	    }

	    ifos_file_seek(fd, 0);
	    retval = ifos_file_write(fd, buffer, sizeof(struct fs_config_layout) );
	    if ( retval != sizeof(struct fs_config_layout) ) {
	    	retval = -1;
	    	rt_echo("part %d bytes written to swap file.", retval);
	    	break;
	    }

		if ((retval = ifos_file_flush(fd)) < 0) {
			rt_echo("ifos_file_flush failed with error:%d", retval);
			break;
		}

	} while(0);

	/* cleanup the cache buffer and close the file descriptor for swap file */
	free(buffer);
	if (INVALID_FILE_DESCRIPTOR != fd) {
		ifos_file_close(fd);
	}

	/* on success, rename the temp file to config file */
	if (retval < 0) {
		os_remove_file(swap);
		return;
	}

	fs_swap_database();
	/* decrease the fsync reference counter in jesspro module,
		order to allow next fsync request pending into queue */
	pro_complete_fsync();
}

int fs_delete_file(int section)
{
	struct fs_archive *archive;

	if (section < 0 || section >= FS_MAX_ARCH ) {
		return -1;
	}

	archive = fs_search_file_bysect(section);
	if (!archive) {
		rt_alert("failed search section %d on delete", section);
		return -1;
	}

	rt_echo("drop archive [%s]", archive->name);

	/* reset mask to unused */
	fs_set_section_bitmask(archive->section, 0);
	/* clear high level cache */
	memset(archive, 0, sizeof(struct fs_archive));
	return 0;
}

int fs_delete_dir(const char *name)
{
	int i;
	struct fs_directory *dirptr;
	struct fs_archive *archiveptr;
	int dirid;

	dirptr = fs_search_dir_byname(name);
	if (!dirptr) {
		return -1;
	}
	dirid = dirptr->id;

	rt_echo("drop dir [%s]", dirptr->name);
	/* zero dir id means deleted */
	memset(dirptr, 0, sizeof(struct fs_directory));

	for (i = 0; i < FS_MAX_ARCH; i++) {
		archiveptr = &__fs_server.layout->archive[i];
		if (archiveptr->id > 0 && archiveptr->dirid == dirid) {
			rt_echo("drop archive [%s]", archiveptr->name);
			/* reset mask to unused */
			fs_set_section_bitmask(archiveptr->section, 0);
			/* clear high level cache */
			memset(archiveptr, 0, sizeof(struct fs_archive));
		}
	}
	return 0;
}

int fs_read_binary(int offset, int size, unsigned char **data)
{
	if (!__fs_server.binary.mptr || !__fs_server.binary.uptr || __fs_server.binary.mlen <= 0 ) {
        return -ENOMEM;
    }

    if (!data || size <= 0 ) {
        return -EINVAL;
    }

    if ( (offset + size) > (FS_MAX_BIN_SIZE - sizeof(struct fs_binary_layout)) ) {
    	rt_alert("parameter illegal [%d:%d]", offset, size);
        return -EINVAL;
    }

    *data = __fs_server.binary.uptr + offset;
    return size;
}

int fs_write_binary(int offset, int size, const unsigned char *data)
{
	if (!__fs_server.binary.mptr || !__fs_server.binary.uptr || __fs_server.binary.mlen <= 0 ) {
        return -ENOMEM;
    }

    if (!data || size <= 0 ) {
        return -EINVAL;
    }

    if ( (offset + size) > (FS_MAX_BIN_SIZE - sizeof(struct fs_binary_layout)) ) {
    	rt_alert("parameter illegal [%d:%d]", offset, size);
        return -EINVAL;
    }

    memcpy(__fs_server.binary.uptr + offset, data, size);
    return size;
}

int fs_clear_config()
{
	char file[MAXPATH];

	/* clear all data in high-level cache */
	if (__fs_server.cache) {
		memset(__fs_server.cache, 0, sizeof(struct fs_config_layout));
	}

	/* clear all data in low-level cache */
	if (__fs_server.config) {
		lwp_mutex_lock(&__fs_server.cache_lock);
		memset(__fs_server.config, 0, sizeof(struct fs_config_layout));
		lwp_mutex_unlock(&__fs_server.cache_lock);
	}

	/* delete the source file */
	os_choose_dir(NULL, NULL, file, NULL, NULL);
	os_remove_file(file);
	return 0;
}

static int fs_inner_compare(const void *left, const void *right)
{
	const struct jessfs_avl_node *left_node = (struct jessfs_avl_node *)left;
	const struct jessfs_avl_node *right_node = (struct jessfs_avl_node *)right;
	return left_node->dirid - right_node->dirid;
}

void fs_update_seq(void * layout)
{
	struct fs_config_layout * jessfs_layout;
	struct avltree_node_t *root;
	struct avltree_node_t *avl_node;
	struct jessfs_avl_node * node;
	struct jessfs_avl_node dir_node;
	int index, archive_offset;
	int flag_reflush;
	if (!layout){
		return;
	}

	root = NULL;
	jessfs_layout = (struct fs_config_layout *)layout;
	flag_reflush = 0;

	/*Traverse the entire dir array*/
	for (index = 0; index < FS_MAX_DIR; index++){
		if (jessfs_layout->dir[index].id > 0){
			node = malloc(sizeof(struct jessfs_avl_node));
			if (node){
				memset(node, 0, sizeof(*node));
				node->dirid = jessfs_layout->dir[index].id;
				node->section = index;

				/*update dir id if needed*/
				if (jessfs_layout->dir[index].id != (index + 1) << 16){
					jessfs_layout->dir[index].id = (index + 1) << 16;
					flag_reflush = 1;
				}
				root = avlinsert(root, &node->index, fs_inner_compare);
			}
		}
	}

	/*Refactoring archive index*/
	if (flag_reflush && root){
		for (archive_offset = 0; archive_offset < FS_MAX_ARCH; archive_offset++){
			if (jessfs_layout->archive[archive_offset].id <= 0){
				continue;
			}

			dir_node.dirid = jessfs_layout->archive[archive_offset].dirid;
			avl_node = avlsearch(root, &dir_node.index, fs_inner_compare);

			if (avl_node){
				node = (struct jessfs_avl_node *)avl_node;
				jessfs_layout->archive[archive_offset].dirid = jessfs_layout->dir[node->section].id;
				jessfs_layout->archive[archive_offset].id = jessfs_layout->dir[node->section].id + archive_offset + 1;
			}
		}
	}

	while (root){
		dir_node.dirid = ((struct jessfs_avl_node *)root)->dirid;
		root = avlremove(root, &dir_node.index, &avl_node, fs_inner_compare);
		if (avl_node){
			free((struct jessfs_avl_node *)avl_node);
		}
	}
}

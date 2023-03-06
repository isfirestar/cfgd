#include "virfs.h"

#include "refos.h"

/* fs server object, def in jessfs.c */
extern struct fs_server __fs_server;
/* bit mask of one Bytre, def in jessfs.c */
extern const unsigned char BIT_MASK[];

static int fs_search_free_section(nsp_boolean_t hold)
{
	int i;
	int j;

	for (i = 0; i < FS_MAX_ITEM_MASK; i++ ) {
		for (j = 0; j < 8; j++) {
			if ( (~__fs_server.bitmask[i]) & BIT_MASK[j] ) {
				if (hold) {
					__fs_server.bitmask[i] |= BIT_MASK[j];
				}
				return ((i << 3) + j);
			}
 		}
	}

	return -1;
}

int fs_set_section_bitmask(int index, int used)
{
	int i;
	int j;

	if (index < 0) {
		return -1;
	}

	i = index >> 3;
	if (i >= FS_MAX_ITEM_MASK) {
		return -1;
	}

	j = index % 8;

	if (used) {
		__fs_server.bitmask[i] |= BIT_MASK[j];
	} else {
		__fs_server.bitmask[i] &= ~BIT_MASK[j];
	}

	return 0;
}

struct fs_directory *fs_search_dir_byname(const char *name)
{
	int i;
	struct fs_directory *dirptr;

	if (!name) {
		return NULL;
	}

	dirptr = NULL;
	for (i = 0; i < FS_MAX_DIR; i++) {
		dirptr = &__fs_server.layout->dir[i];
		if (0 == os_strcasecmp(name, dirptr->name) && dirptr->id > 0) {
			break;
		}
		dirptr = NULL;
	}

	return dirptr;
}

struct fs_directory *fs_seek_free_dir_location(int *section)
{
	int i;
	struct fs_directory *dirptr;

	dirptr = NULL;
	for (i = 0; i < FS_MAX_DIR; i++) {
		dirptr = &__fs_server.layout->dir[i];
		if ( dirptr->id <= 0 ) {
			*section = i;
			break;
		}
		dirptr = NULL;
	}

	return dirptr;
}

struct fs_archive *fs_seek_free_file_location(int *section)
{
	struct fs_archive *archive;
	int n;

	if (!section) {
		return NULL;
	}

	n = fs_search_free_section(YES);
	if (n < 0 ) {
		return NULL;
	}

	/* save the section on seccuess */
	archive = &__fs_server.layout->archive[n];
	if (archive->id <= 0 ) {
		*section = n;
		return archive;
	}

	return NULL;
}

struct fs_archive *fs_search_file_bysect(int section)
{
	struct fs_archive *archive;

	if (section < 0 || section >= FS_MAX_ARCH) {
		return NULL;
	}

	archive = &__fs_server.layout->archive[section];
	if (archive->id > 0) {
		return archive;
	}

	return NULL;
}

struct fs_archive *fs_search_file_byname(const char *name, int dirid)
{
	int i;
	struct fs_archive *archive;

	if (!name) {
		return NULL;
	}

	archive = NULL;
	for ( i = 0; i < FS_MAX_ARCH; i++) {
		archive = &__fs_server.layout->archive[i];
		if (archive->dirid == dirid && 0 == os_strcasecmp(archive->name, name) ) {
			break;
		}
		archive = NULL;
	}

	return archive;
}

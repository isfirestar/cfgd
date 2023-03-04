#include "jesspro.h"

#include "clock.h"

#include "runtime.h"
#include "jessfs.h"
#include "jessldr.h"
#include "jessos.h"

void pro_mkdir(JRP *jrp)
{
	if (1 != jrp->depth) {
		pro_append_response(jrp->taskptr, JESS_FATAL);
		return;
	}

	if ( rt_create_dir(jrp) < 0 ) {
		pro_append_response(jrp->taskptr, JESS_FATAL);
	} else {
		pro_append_response(jrp->taskptr, JESS_OK);
		fs_append_sync();
	}
}

void pro_rmdir(JRP *jrp)
{
	if (1 != jrp->depth) {
		pro_append_response(jrp->taskptr, JESS_FATAL);
		return;
	}

	if ( rt_remove_dir(jrp) < 0 ) {
		pro_append_response(jrp->taskptr, JESS_FATAL);
	} else {
		pro_append_response(jrp->taskptr, JESS_OK);
		fs_append_sync();
	}
}

void pro_create_checkpoint(JRP *jrp)
{
	char file[MAXPATH];
	datetime_t time;

	clock_systime(&time);
	os_snprintf(file, sizeof(file), "jess_checkpoint_%04d%02d%02d_%02d%02d%02d", time.year, time.month, time.day, time.hour, time.minute, time.second);

	/* ask jess filesystem to dump the checkpoint file */
	if ( fs_create_checkpoint(file) < 0 ) {
		pro_append_response(jrp->taskptr, JESS_FATAL);
		return;
	}

	/* add checkpoint information to jess proc */
	rt_echo("save checkpoint info into jess/proc");
	procfs_add_checkpoint(file);
	pro_append_response(jrp->taskptr, JESS_OK);
}

void pro_recover_checkpoint(JRP *jrp)
{
	do {
		rt_suface_remove_dir(jrp);

		/* load data from config database file, save into __fs_server::config */
		if ( fs_load_config_database(NULL) < 0 ) {
			pro_append_response(jrp->taskptr, JESS_FATAL);
			break;
		}
		/* copy data from __fs_server::config to __fs_server::cache */
		fs_load_config_cache();

		/* rebuild the filesystem layout */
		if (ldr_load_filesystem() < 0) {
			pro_append_response(jrp->taskptr, JESS_FATAL);
			break;
		}

		pro_append_response(jrp->taskptr, JESS_OK);
		procfs_rebuild();
	} while(0);

	/* open permission for sync/fsync convert database */
	os_atomic_set(&__fs_server.permit_override, 1);
}

void pro_query_version(JRP *jrp)
{
	JRP *versionjrp;
	struct list_head head;
	char request[MAX_REQ_SIZE];

	strcpy(request, "proc.jess.version"CRLF);
	if ( rt_build_jrp(request, jrp->taskptr, &head) != 1 ) {
		pro_append_response(jrp->taskptr, JESS_FATAL);
		return;
	}

	versionjrp = rt_jrp_in_clist(head.next);
	if (rt_search(versionjrp) < 0) {
		pro_append_response(jrp->taskptr, JESS_FATAL);
	} else {
		pro_append_response(jrp->taskptr, JESS_OK);
		pro_append_response(jrp->taskptr, "version=%s"CRLF, versionjrp->dupvalue);
	}

	free(versionjrp);
}

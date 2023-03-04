#include "virfs.h"

#include "ifos.h"
#include "threading.h"

#include "runtime.h"
#include "refos.h"
#include "order.h"

/* fs server object, def in jessfs.c */
extern struct fs_server __fs_server;

int fs_create_checkpoint(const char *file)
{
	unsigned char *buffer;
	char snapshot[MAXPATH], fsdir[MAXPATH];
	int retval;
	file_descriptor_t fd;

	/* dump config file context from cache */
	buffer = (unsigned char *)malloc(sizeof(struct fs_config_layout));
	if (!buffer) {
		return -1;
	}
	lwp_mutex_lock(&__fs_server.cache_lock);
	memcpy(buffer, __fs_server.config, sizeof(struct fs_config_layout));
	lwp_mutex_unlock(&__fs_server.cache_lock);

	fd = INVALID_FILE_DESCRIPTOR;
	retval = -1;

	/* try to write context to temp disk file */
	do {
		os_choose_dir(NULL, fsdir, NULL, NULL, NULL);
		if (os_snprintf(snapshot, sizeof(snapshot), "%s%s", fsdir, file) >= sizeof(snapshot)) {
			rt_alert("can not create checkpoint, total length over limit.");
			break;
		}

		retval = ifos_file_open(snapshot, FF_RDACCESS | FF_WRACCESS | FF_CREATE_NEWONE, 0644, &fd);
	    if ( (retval < 0) || (INVALID_FILE_DESCRIPTOR == fd) ) {
	    	rt_alert("can not create checkpoint file %s, error:%d", snapshot, retval);
	        break;
	    }

	    if ( (retval = ifos_file_write(fd, buffer, sizeof(struct fs_config_layout)) ) <= 0 ) {
	    	rt_echo("can not write buffer to disk checkpoint file, error:%d", retval);
	    	break;
	    }

	    rt_echo("checkpoint snapshot success, file:%s.", snapshot);
	} while(0);

	if (INVALID_FILE_DESCRIPTOR != fd) {
		ifos_file_close(fd);
	}
	free(buffer);
	return retval;
}

void fs_recover_checkpoint(JRP *jrp)
{
	char snapshot[MAX_KEY_SIZE + MAXPATH], swap[MAXPATH], fsdir[MAXPATH];
	int retval;
	file_descriptor_t fdsnapshot, fdswap;
	unsigned char *buffer;
	int64_t size;

	buffer = NULL;
	fdsnapshot = fdswap = INVALID_FILE_DESCRIPTOR;

	os_choose_dir(NULL, fsdir, NULL, swap, NULL);
	os_snprintf(snapshot, sizeof(snapshot), "%s%s", fsdir, jrp->dupkey);

	/* disable the config overwritten,
		this value will not change until checkpoint convert complete or fail */
	os_atomic_set(&__fs_server.permit_override, 0);

	/* move checkpoint file to swap */
	do {
		retval = ifos_file_open(snapshot, FF_RDACCESS | FF_OPEN_EXISTING, 0644, &fdsnapshot);
	    if ( (retval < 0) || (INVALID_FILE_DESCRIPTOR == fdsnapshot) ) {
	    	rt_alert("ifos_file_open failed on file %s, with error:%d", snapshot, retval);
	        break;
	    }
	    if (sizeof(struct fs_config_layout) != (size = ifos_file_fgetsize(fdsnapshot)) ) {
			retval = -1;
	    	rt_alert("checkpoint size mismatch %lld", size);
	    	break;
	    }

	    buffer = (unsigned char *)malloc(sizeof(struct fs_config_layout));
	    if (!buffer) {
			retval = -ENOMEM;
	    	break;
	    }

	    retval = ifos_file_open(swap, FF_RDACCESS | FF_WRACCESS | FF_CREATE_ALWAYS, 0644, &fdswap);
	    if ( (retval < 0) || (INVALID_FILE_DESCRIPTOR == fdswap) ) {
	    	rt_alert("ifos_file_open failed on swap file %s, with error:%d", swap, retval);
	        break;
	    }

	    if ( (retval = ifos_file_read(fdsnapshot, buffer, sizeof(struct fs_config_layout))) < 0 ) {
	    	rt_echo("ifos_file_read failed with error:%d", retval);
	    	break;
	    }

	    ifos_file_seek(fdswap, 0);
	    if ( (retval = ifos_file_write(fdswap, buffer, sizeof(struct fs_config_layout)) ) != sizeof(struct fs_config_layout) ) {
	    	retval = -1;
	    	rt_echo("ifos_file_write failed with error:%d", retval);
	    	break;
	    }
	} while( 0 );

	if (INVALID_FILE_DESCRIPTOR != fdsnapshot) {
		ifos_file_close(fdsnapshot);
		fdsnapshot = INVALID_FILE_DESCRIPTOR;
	}

	if (INVALID_FILE_DESCRIPTOR != fdswap) {
		ifos_file_close(fdswap);
		fdswap = INVALID_FILE_DESCRIPTOR;
	}

	if (buffer) {
		free(buffer);
		buffer = NULL;
	}

	if (retval < 0) {
		os_remove_file(swap);
		/* recover the overwrite permission when checkpoint convert fail */
		os_atomic_set(&__fs_server.permit_override, 1);
		return;
	}

	/* use swap file instead the old config database file. */
	fs_swap_database();
	/* schedule a pro recover task to dispatcher */
	pro_append_request("recover2", jrp, "/");
	/* to disable the respond message send to client when this task is end.
		the correct resonse time is the "recover2" task completed */
	((struct jesspro_task *)jrp->taskptr)->link = INVALID_HTCPLINK;
}

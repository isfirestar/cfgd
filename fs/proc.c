#include "virfs.h"

#include <string.h>

#include "runtime.h"
#include "ldr.h"
#include "refos.h"
#include "request.h"
#include "order.h"

static int procfs_config_update(const char *command, struct jessrt_configure_node **archive)
{
	struct list_head head;
	JRP *jrp;

	if ( 1 != rt_build_jrp(command, NULL, &head) ) {
		return -1;
	}

	jrp = containing_record(head.next, JRP, entry);
	rt_config_update(jrp, archive);
	free(jrp);
	return 0;
}

static int procfs_load_checkpoint()
{
	char dir[256], file[256], update[512];
	void *dirview;
	struct jessrt_configure_node *archive;
	size_t cmplen = strlen("jess_checkpoint_");

	strcpy(update, "proc.jess.checkpoint=deleted");
	procfs_config_update(update, &archive);

	os_choose_dir(NULL, dir, NULL, NULL, NULL);

	dirview = os_create_dirview(dir);
	if (!dirview) {
		return -1;
	}

	while (NULL != (os_foreach_file(dirview, dir, file)) ) {
		if (0 == os_strncasecmp(file, "jess_checkpoint_", cmplen)) {
			os_snprintf(update, sizeof(update), "proc.jess.checkpoint[+]=%s", file);
			procfs_config_update(update, &archive);
		}
	}

	os_close_dirview(dirview);
	return 0;
}

static void procfs_load_property()
{
	static const int MAX_TCP_PENDING = 10;
	static const int TCP_LINK_TIMEOUT = 6;
	char update[MAX_PAYLOAD_SIZE];
	struct jessrt_configure_node *archive;

	os_snprintf(update, sizeof(update), "proc.jess.tcp_max_pending=%d", MAX_TCP_PENDING);
	procfs_config_update(update, &archive);
	os_snprintf(update, sizeof(update), "proc.jess.tcp_link_timeout=%d", TCP_LINK_TIMEOUT);
	procfs_config_update(update, &archive);
}

void procfs_set_property(const char *field, int n)
{
	char update[MAX_PAYLOAD_SIZE];

	if (field) {
		os_snprintf(update, sizeof(update), "proc.jess.%s=%d", field, n);
		/* for thread safe reason, the request MUST dispatch and schedule in pro procedure */
		pro_append_request("write", NULL, update);
	}
}

int procfs_get_property(const char *field)
{
	struct list_head head;
	JRP *jrp;
	char update[MAX_PAYLOAD_SIZE];

	if (!field) {
		return -1;
	}

	os_snprintf(update, sizeof(update), "proc.jess.%s", field);
	if ( 1 != rt_build_jrp(update, NULL, &head) ) {
		return -1;
	}

	jrp = containing_record(head.next, JRP, entry);
	if (rt_search(jrp) < 0 ) {
		return -1;
	}

	return atoi(jrp->dupvalue);
}

#if _WIN32
#pragma comment(lib, "Version.lib")
char *jess_get_program_version(char *holder, int size)
{
	char path[MAXPATH];
	DWORD dwhandle, dwLen;
	char *Block;
	UINT uLen;
	VS_FIXEDFILEINFO *lpFixedFileInfo;

	strcpy_s(holder, size, "0.0");
	Block = NULL;

	do {
		posix__fullpath_current2(path, sizeof(path));

		dwhandle = 0;
		dwLen = GetFileVersionInfoSizeA(path, &dwhandle);
		if (0 == dwLen) {
			break;
		}

		Block = (char *)malloc(dwLen);
		if (!GetFileVersionInfoA(path, 0, dwLen, Block)) {
			break;
		}

		if (VerQueryValueA(Block, "\\", &lpFixedFileInfo, &uLen)) {
			if (uLen == sizeof(VS_FIXEDFILEINFO)) {
				sprintf_s(holder, size, "%d.%d", (lpFixedFileInfo->dwProductVersionLS >> 16) & 0xff, lpFixedFileInfo->dwProductVersionLS & 0xff);
			}
		}

	} while (0);

	if (Block) {
		free(Block);
	}

	return holder;
}
#endif

int procfs_mark_version()
{
	char update[MAX_PAYLOAD_SIZE];
	struct jessrt_configure_node *archive;

#if _WIN32
	char holder[128];
	sprintf_s(update, sizeof(update), "proc.jess.version=%s", jess_get_program_version(holder, sizeof(holder)));
#else
	strcpy(update, "proc.jess.version="_BUILTIN_VERSION );
#endif
	return procfs_config_update(update, &archive);
}

const char *procfs_get_version()
{
#if _WIN32
	static char holder[128];
	return jess_get_program_version(holder, sizeof(holder));
#else
	return _BUILTIN_VERSION;
#endif
}

int procfs_rebuild(cJSON* jsonptr)
{
	struct jessrt_configure_node *proc;/* *archive;*/

	proc = ldr_create_dir("proc");
	if (!proc) {
		return -1;
	}

	ldr_create_archive(proc, "jess", NULL);
	ldr_create_archive(proc, "history", NULL);
	/* save software version info into proc.jess.version */
	procfs_mark_version();
	/* insert all checkpoint files into proc.jess.checkpoint */
	procfs_load_checkpoint();
	/* load other property on jess startup or recreate */
	procfs_load_property();
	return 0;
}

int procfs_add_checkpoint(const char *file)
{
	char  update[MAX_PAYLOAD_SIZE];
	struct jessrt_configure_node *archive;

	if (!file) {
		return -1;
	}

	os_snprintf(update, sizeof(update), "proc.jess.checkpoint[+]=%s", file);
	return procfs_config_update(update, &archive);
}



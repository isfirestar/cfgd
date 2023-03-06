#include "logger.h"

#include "interact.h"
#include "virfs.h"
#include "ldr.h"
#include "refos.h"
#include "order.h"
#include "conf.h"

#include "args.h"

void STDCALL ecr(const char *host_event, const char *reserved, int rescb)
{
	log_save("nshost", kLogLevel_Trace, kLogTarget_Filesystem, "%s", host_event);
}

int main(int argc, char **argv)
{
	int n;
	char file[MAXPATH];
	int charg;
	nsp_boolean_t reorganization;
	uint16_t port;
	const char *logdir;
	const char *cfgfile;

	if ((charg = arg_analyze_input(argc, argv)) < 0) {
		return 1;
	}

	/* determine config file and load it into memory */
	cfgfile = NULL;
	if (charg & kInvisibleOptIndex_ConfigFilePath) {
		cfgfile = arg_query_cfgfile();
	}
	os_load_conf(cfgfile);

	if (NULL != (logdir = os_query_conf("logdir"))) {
		log_init2(logdir);
	} else {
		log_init();
	}

	nis_checr(&ecr);

	/* Initialization of basic components of jessfs */
	/* 20210630: Jess doesn't fresh disk automaticly */
	//fs_initial();

	/* specify order by application command line */
	if (charg & kInvisibleOptIndex_Reorganization) {
		rt_echo("user order to reorganization configure.");
		fs_clear_config();
	}

	/* load or try load configure from existing filesystem archives, or,
		reorganization it for /etc/agv/, xml convertion MUST be finished  */
	reorganization = NO;
	n = fs_startup(&reorganization);
	if ( n < 0 ) {
		return 1;
	}
	rt_echo("reorganization flag=%d", reorganization);

	/* first time jess service run or other situation that jess confirm to reorganization,
		or, specify order by application command line */
	if (reorganization || (charg & kInvisibleOptIndex_Reorganization)) {
		rt_echo("jessfs order to reorganization configure.");
		/* proc will load data from filesystem, nomatter etc loader success or fault
			by first time run, etc loader use to collect data which save in old config files */
		os_choose_dir(file, NULL, NULL, NULL, NULL);

		/* erase all data in current config file on disk, reload from /etc/agv/ directory */
		fs_clear_config();
		ldr_load_config(file);
	}

	/* build the filesystem */
	ldr_load_filesystem();

	/* initialization proc fs for jess */
	procfs_rebuild(NULL);

	/* startup the protocol parser, local sync are allow now */
    if (pro_create() < 0 ) {
    	return 1;
    }

    /* after filesystem build and pro initialized, if current snapshot is reorganizaed, sync command should be post initiative */
	if (reorganization || (charg & kInvisibleOptIndex_Reorganization)) {
		pro_append_sync();
	}

	/* determine the port for service */
	if (!os_query_conf_integer("port", &port)) {
		port = arg_query_port();
	}
	rt_echo("service port is:%u", port);
	return jnet_startup(NULL, port);
}

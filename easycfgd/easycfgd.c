#include "nis.h"
#include "threading.h"
#include "clock.h"
#include "ifos.h"
#include <string.h>
#include "../tst.h"
#include "compiler.h"
#include "clist.h"

#include <ctype.h>
#include <stdio.h>
#include "atom.h"

lwp_event_t __exit;
lwp_event_t wait_handle_;
uint64_t __record = 0;
static int command_retval = -1;
static HTCPLINK link_ = INVALID_HTCPLINK;
static int global_flag_ = 0;

#define MAX_LIMIT_SIZE 2<<10<<10

#define EASYJESS_VERSION "VersionOfProgramDefinition-ChangeInMakefile"

struct jess_cmd_t{
	char cmd_[1<<20];
	struct list_head node_;
};

struct jess_th_data_t{
	int render_count_;

	uint32_t interval_;

	char response_[1 << 20];

	struct list_head root_;
};

static char st_file_path[256];

struct rpc_command{
	const char * command_;
	int(*actor_)(char *cmd, char * option, struct jess_th_data_t *response);
};

static int jess_build_profile_packet(char *cmd, char * option, struct jess_th_data_t *);
static int jess_build_export_packet(char *cmd, char * option, struct jess_th_data_t *);
static int jess_build_import_packet(char *cmd, char * option, struct jess_th_data_t *);

static struct rpc_command command_[] = {
	{ "profile", jess_build_profile_packet},
	{ "export", jess_build_export_packet},
	{ "import", jess_build_profile_packet },
	{ "redirect", jess_build_import_packet}
};

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

static void jess_display_author_information()
{
	char author_context[512];

	sprintf(author_context,
		"easyjess %s"POSIX__EOL
		"Copyright (C) 2019 Zhejiang Guozi RoboticsCo., Ltd."POSIX__EOL
		"For bug reporting instructions, please see:"POSIX__EOL
		"<http://www.gzrobot.com/>."POSIX__EOL
		"Find the AGV manual and other documentation resources online at:"POSIX__EOL
		"<http://www.gzrobot.com/productCenter/>."POSIX__EOL
		"For help, type \"help\"."POSIX__EOL,
#if _WIN32
		jess_get_program_version(holder, sizeof(holder))
#else
		EASYJESS_VERSION
#endif
		);

	printf("%s", author_context);
}

static void jess_display_usage()
{
	static const char *usage_context =
		"NAME\n"
		"\teasyjess simple communication cmmand with jess\n"
		"\n"
		"SYNOPSIS\n"
		"\teasyjess\n"
		"\t\t[-h] display usage context and help informations\n"
		"\t\t[-o] request with option data. eg: ./easyjess read driver.dio.device[0].id -o id=5 -o offset=7 127.0.0.1\n"
		"\t\t[-i] interval between request sent. eg: ./easyjess write driver.dio.device[+].dat[+]=7 -i 2000 -n 5 127.0.0.1\n"
		"\t\t[-n] number of request executed. eg: ./easyjess scope driver.dio.device -n 6 127.0.0.1\n"
		"\t\t[-v] Query easyjess version\n"
		"\t\t[General Command]:\n"
		"\t\t[import|profile] improt configuration to target. eg: easyjess import ./127.ini 127.0.0.1\n"
		"\t\t[export] exprot configuration form XXX. eg: easyjess export ./127.ini 127.0.0.1\n"
		"\t\t[redirect] read easyjess command from local file. eg: easyjess redirect ./eg_jess.ini 127.0.0.1\n"
		"\t\t[read] read field from jess. eg: easyjess read driver.dio.device[0].dat[0] 127.0.0.1\n"
		"\t\t[write] write field to jess. eg: easyjess write driver.dio.device[+].da=7 127.0.0.1\n"
		"\t\t[contain] query key list in next range. eg: easyjess contain driver.dio.device 127.0.0.1\n"
		"\t\t[scope] query key value pairs of child. eg: easyjess scope driver.dio 127.0.0.1\n"
		"\t\t[file] direct request file data. eg: easyjess file driver.dio 127.0.0.1\n"
		"\t\t[wbin] write binary file. eg: easyjess wbin -o offset=5 -o size=6 test12 127.0.0.1\n"
		"\t\t[rbin] read binary file. eg:easyjess rbin -o offset=5 -o size=6 127.0.0.1\n"
		"\t\t[checkpoint] create a checkpoint to save current config settings. eg: easyjess checkpoint 127.0.0.1\n"
		"\t\t[mkdir] create directory. eg: easyjess mkdir custom 127.0.0.1\n"
		"\t\t[rmdir] delete directory. eg: easyjess rmdir custom 127.0.0.1\n"
		"\t\t[sync] force sync data into disk file. eg: easyjess sync 127.0.0.1\n"
		"\t\t[subscribe]subscribe concerned pair change. eg: easyjess subscribe driver.dio 127.0.0.1\n"
		"\t\t[recover]recover config settings by speify checkpoint file. eg: easyjess recover jess_checkpoint_20191126_164958 127.0.0.1\n"
		;

	printf("%s", usage_context);
}

static void export_file(char * buff){
	file_descriptor_t fd;

	if (ifos_file_open(st_file_path, FF_WRACCESS | FF_CREATE_ALWAYS, 0644, &fd) < 0){
		printf("open file %s failed\r\n", st_file_path);
		return;
	}

	char *scope_begin = strstr(buff, "ok");
	if (scope_begin){
		scope_begin += 2;
		while (*scope_begin == '\n' || *scope_begin == '\r'){
			scope_begin++;
		}
		ifos_file_write(fd, scope_begin, strlen(scope_begin));
	}
	ifos_file_close(fd);
}

static void jess_replace(char * first, char * end, char re_old, char re_new){
	if (!first || !end){
		return;
	}

	while (first < end){
		if (*first == re_old){
			*first = re_new;
		}
		first++;
	}
}

static void STDCALL __easyjess_tcp_callback(const struct nis_event *event, const void *data)
{
	uint64_t now;
	int size = 0;

	if (EVT_RECEIVEDATA == event->Event) {
		now = clock_monotonic();
		size = ((struct nis_tcp_data *)data)->e.Packet.Size;
		char *malloc_buff = (char *)malloc(size + 1);
		malloc_buff[size] = '\0';
		memcpy(malloc_buff, ((struct nis_tcp_data *)data)->e.Packet.Data, size);
		if (strstr(malloc_buff, "keepalive")==malloc_buff){
			free(malloc_buff);
			return;
		}

		if (strstr(malloc_buff, "ok")){
			command_retval = 0;
		}
		printf("%s\n", malloc_buff);

		if (global_flag_ && strstr(malloc_buff, "scope")){
			export_file(malloc_buff);
			atom_exchange(&global_flag_, 0);
		}
		free(malloc_buff);
		printf(">>>>>>>>> jess transfer %u Bytes,consume %.3f ms.\n", ((struct nis_tcp_data *)data)->e.Packet.Size, (double)((double)now - (double)__record) / 10000 );
		__record = now;
		lwp_event_awaken(&__exit);
	}
	else if (EVT_CLOSED == event->Event){
		printf("tcp link %ld closed\r\n", event->Ln.Tcp.Link);
		lwp_event_awaken(&__exit);
		lwp_event_awaken(&wait_handle_);
	}
}

static nsp_boolean_t __is_effective_ipv4( const char *ipstr )
{
	size_t len;
	char *chr, *dup, *cursor;
	int i;
	int segm;
	nsp_boolean_t retb;

	if (!ipstr) {
		return NO;
	}
	len = strlen(ipstr);

	if ( 0 == len || len >= INET_ADDRSTRLEN ) {
		return NO;
	}

	for (i = 0; i < len; i++) {
		if (!isdigit(ipstr[i]) && ipstr[i] != '.') {
			return NO;
		}
	}

	dup = strdup(ipstr);
	cursor = dup;
	i = 0;
	retb = YES;

	while (NULL != (chr = strchr(cursor, '.')) ) {
		*chr = 0;
		if (strlen(cursor) > 3) {
			retb = NO;
			break;
		}
		segm = atoi(cursor);
		if (segm > 255) {
			retb = NO;
			break;
		}

		cursor = chr + 1;
		i++;
	}

	if (i != 3) {
		retb = NO;
	}

	free(dup);
	return retb;
}

static int parse_profile(int size, char * buffer, char * option, char * response){
	int retval = -1;

	if (!buffer && !option){
		return -1;
	}

	strlen(option) > 0 ? sprintf(response, "write\r\n%s\r\n", option) : sprintf(response, "write\r\n");

	char * line = (char *)buffer;
	while (1){
		char * splits = strstr(line, "\n");
		if (splits){
			*splits = '\0';
		}

		int length = strlen(line);
		if (length > 0 && line[length -1] == '\r'){
			line[length -1] = '\0';
		}

		if (strlen(line) > 0){
			sprintf(response + strlen(response), "%s\r\n", line);
			retval = 0;
		}

		if (!splits){
			break;
		}

		line = splits + 1;
	}

	return retval;
}

static int read_file(char * path, int *size, char ** content){
	file_descriptor_t fd;
	int64_t file_size = 0;
	int retvel = -1;

	if (!path || !size || !content){
		return -1;
	}
	if (ifos_file_open(path, FF_RDACCESS | FF_OPEN_EXISTING, 0, &fd) < 0){
		printf("open file %s failed\r\n", path);
		return -1;
	}

	do
	{
		file_size = ifos_file_fgetsize(fd);
		if (file_size > MAX_LIMIT_SIZE){
			printf("profile limit size %d\n", MAX_LIMIT_SIZE);
			break;
		}

		if (file_size == 0){
			printf("profile %s total size == 0\n", path);
			break;
		}

		*content = (char *)malloc((int)file_size + 1);
		if (!*content){
			printf("malloc buffer failed \n");
			break;
		}

		if (ifos_file_read(fd, *content, (int)file_size) < file_size){
			free(*content);
			*content = NULL;
			break;
		}
		*size = file_size;
		(*content)[file_size] = '\0';

		if (strlen(*content) < file_size){
			printf("profile format error\n");
			free(*content);
			*content = NULL;
			break;
		}
		retvel = 0;
	} while (0);
	ifos_file_close(fd);

	return retvel;
}

static int jess_build_general_packet(char *title, char *cmd, char * option , char *response){
	if (!option || !title || !response || !title){
		return -1;
	}

	if (strlen(option) > 0){
		sprintf(response, "%s\r\n%s\r\n", title, option);
	}
	else{
		sprintf(response, "%s\r\n", title);
	}

	if (strlen(cmd)){
		sprintf(response + strlen(response), "%s", cmd);
	}
	return 0;
}

static void * th_keep_alive(void *param){
	char buff[] = {"keepalive\r\n"};

	while (1){
		posix__sleep(2000);

		if (tcp_write(link_, buff, sizeof(buff), NULL) < 0){
			printf("send keepalive failed\n");
			break;
		}
	}
	return NULL;
}

static int init_tcp_network(char * host){
	tst_t tst;
	int retval = -1;
	char * cursor = NULL;
	uint16_t port = 4407;
	/* check it specified a effective host ip address */
	char *ipv4 = strdup(host);
	if ((cursor = strstr(ipv4, ":"))){
		*cursor = 0;
		cursor += 1;
		port = atoi(cursor);
	}

	do
	{
		if (port >= 65535 || port <= 0){
			printf("invalid port\r\n");
			break;
		}

		if (!__is_effective_ipv4(ipv4)) {
			printf("host %s is not a effective ip v4 address,\n", ipv4);
			break;
		}

		tcp_init2(0);

		link_ = tcp_create(&__easyjess_tcp_callback, NULL, 0);
		if (INVALID_HTCPLINK == link_) {
			break;
		}

		tst.parser_ = &nsp__tst_parser;
		tst.builder_ = &nsp__tst_builder;
		tst.cb_ = sizeof (nsp__tst_head_t);

		if ((retval = nis_cntl(link_, NI_SETTST, &tst)) < 0) {
			printf("tcp control tst failed.\n");
			break;
		}

		if ((retval = tcp_connect(link_, ipv4, port)) < 0) {
			printf("tcp connect %s:%d failed.\n", ipv4, port);
			break;
		}
	} while (0);

	free(ipv4);
	return retval;
}

static int jess_build_packet(char * title, char *jess_buff, char * option, struct jess_th_data_t * jess_root){
	int retval = -1;
	int find_rpc = 0;
	for (int index = 0; index < cchof(command_); index++){
		if (!strcmp(command_[index].command_, title)){
			retval = command_[index].command_ ? command_[index].actor_(jess_buff, option, jess_root) : -1;
			find_rpc = 1;
			break;
		}
	}

	if (!find_rpc){
		if (jess_build_general_packet(title, jess_buff, option, jess_root->response_)){
			printf("build easyjess packet error\n");
			return -1;
		}
		retval = 0;
	}
	return retval;
}

static int jess_parse_cmd(char * cmd, struct jess_th_data_t *jess_root){
	if (!jess_root || !cmd){
		return -1;
	}

	char * cursor = cmd;
	char *ending = NULL;
	char option[1024] = {0};
	char *jess_buff = NULL;
	char * title = cursor;
	int render_count = 0;
	int interval = 0;
	int flag = 0;
	int retval = -1;

	jess_buff = malloc(1 << 20);
	if (!jess_buff){
		return -1;
	}
	jess_buff[0] = 0;

	ending = strstr(cursor, " ");
	if (ending){
		*ending = '\0';
	}

	do
	{
		if (!ending){
			break;
		}
		cursor = ending + 1;

		while (NULL != (ending = strstr(cursor, " "))){
			*ending = '\0';
			jess_replace(cursor, ending, ';', ' ');
			do
			{
				if (strlen(cursor) == 0){
					break;
				}

				if (*cursor != '-'){
					switch (flag){
					case 0:
						sprintf(jess_buff + strlen(jess_buff), "%s\r\n", cursor);
						break;
					case 1:
						sprintf(option + strlen(option), "%s\r\n", cursor);
						break;
					case 2:
						render_count = atoi(cursor);
						break;
					case 3:
						interval = atoi(cursor);
						break;
					default:
						break;
					}
					flag = 0;
					break;
				}

				if (strlen(cursor) < 2){
					break;
				}

				switch (*(cursor+ 1))
				{
				case 'o':
					flag = 1;
					break;
				case 'n':
					flag = 2;
					break;
				case 'i':
					flag = 3;
					break;
				default:
					flag = 0;
					break;
				}
			} while (0);
			cursor = ending + 1;
		}
	} while (0);

	retval = jess_build_packet(title, jess_buff, option, jess_root);
	free(jess_buff);
	jess_root->render_count_ = render_count > 0 ? render_count : 1;
	jess_root->interval_ = interval > 0 ? interval : 0;
	return retval;
}

static void *th_proc_command(void * param){
	struct jess_th_data_t * jess_root = (struct jess_th_data_t *) param;
	if (!jess_root){
		return NULL;
	}

	struct jess_cmd_t * jess_cmd = NULL;
	struct list_head * node = NULL;
	int th_exit = 0;
	while (!th_exit){
		if (!list_empty(&jess_root->root_)){
			node = jess_root->root_.next;
			list_del(node);
			jess_cmd = container_of(node, struct jess_cmd_t, node_);

			if (!jess_parse_cmd(jess_cmd->cmd_, jess_root)){
				for (int index = 0; index < jess_root->render_count_; index++){
					posix__reset_waitable_handle(&__exit);

					__record = clock_monotonic();

					if (tcp_write(link_, jess_root->response_, strlen(jess_root->response_), NULL) < 0){
						printf("tcp write failed\n");
						th_exit = 1;
						break;
					}

					if (jess_root->interval_ > 0 && index != (jess_root->render_count_ - 1)){
						posix__sleep(jess_root->interval_);
					}
					else{
						if (ETIMEDOUT == lwp_event_wait(&__exit, INFINITE)) {
							printf("wait for jess response timeout.\n");
						}
					}
				}

				if(strcmp(jess_cmd->cmd_, "subscribe") == 0){
					lwp_event_wait(&wait_handle_, INFINITE);
				}
			}
			free(jess_cmd);
		}
		else{
			break;
		}
	}

	return NULL;
}

int main(int argc, char **argv)
{
	int retval = -1;
	char *host;
	lwp_t th;
	lwp_t th_heartbeat;
	struct jess_th_data_t * jess_root = NULL;
	st_file_path[0] = '\0';

	if ((argc == 2 )&& (0 == strcmp(argv[1], "-h"))){
		jess_display_usage();
		return 0;
	}
	else if ((argc == 2) && (0 == strcmp(argv[1], "-v"))){
		jess_display_author_information();
		return 0;
	}
	else if (argc < 3){
		printf("Invalid easyjess command\r\n");
		return -1;
	}

	do {
		host = argv[argc - 1];
		posix__init_notification_waitable_handle(&__exit);
		posix__init_notification_waitable_handle(&wait_handle_);

		if (init_tcp_network(host) < 0){
			break;
		}

		jess_root = (struct jess_th_data_t *)malloc(sizeof(struct jess_th_data_t));
		if (!jess_root){
			break;
		}

		memset(jess_root, 0, sizeof(*jess_root));
		INIT_LIST_HEAD(&jess_root->root_);

		struct jess_cmd_t *cmd_node = (struct jess_cmd_t *)malloc(sizeof(struct jess_cmd_t));
		if (!cmd_node){
			break;
		}
		memset(cmd_node, 0, sizeof(*cmd_node));
		INIT_LIST_HEAD(&cmd_node->node_);
		lwp_create(&th_heartbeat, 0, &th_keep_alive, NULL);

		/*防止命令行中 参数中带有空格*/
		for (int index = 1; index < argc - 1; index++){
			jess_replace(argv[index], argv[index] + strlen(argv[index]), ' ', ';');
			sprintf(cmd_node->cmd_ + strlen(cmd_node->cmd_), "%s ", argv[index]);
		}

		list_add_tail(&cmd_node->node_, &jess_root->root_);
		retval = lwp_create(&th, 0, &th_proc_command, (void *)jess_root);
		if (retval >= 0){
			lwp_join(&th, NULL);
		}
	}while(0);

	if (link_ != INVALID_HTCPLINK){
		tcp_destroy(link_);
	}

	if (jess_root){
		free(jess_root);
	}

	return retval >= 0 ? command_retval : retval;
}

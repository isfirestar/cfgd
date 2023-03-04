#if !defined JSONIFY_ESCAPE_H
#define JSONIFY_ESCAPE_H

#include "avltree.h"

#include "cJSON.h"

#include "request.h"

#if _UNIT_TEST
	#if _WIN32
		#define rt_echo( fmt, ...) printf("[%s] "fmt, __FUNCTION__, ##__VA_ARGS__)
		#define rt_alert( fmt, ...) printf("[%s] "fmt, __FUNCTION__, ##__VA_ARGS__)
		#define rt_fatal( fmt, ...) printf("[%s] "fmt, __FUNCTION__, ##__VA_ARGS__)
		#define rt_trace( fmt, ...) printf("[%s] "fmt, __FUNCTION__, ##__VA_ARGS__)
	#else
		#define rt_echo( fmt, arg...) printf("[%s] "fmt, __FUNCTION__, ##arg)
		#define rt_alert( fmt, arg...) printf("[%s] "fmt, __FUNCTION__, ##arg)
		#define rt_fatal( fmt, arg...) printf("[%s] "fmt, __FUNCTION__, ##arg)
		#define rt_trace( fmt, arg...) printf("[%s] "fmt, __FUNCTION__, ##arg)
	#endif
#else
	#include "logger.h"
	#if _WIN32
		#define rt_echo( fmt, ...) log_save("jess", \
				kLogLevel_Info, kLogTarget_Stdout | kLogTarget_Filesystem, "[%s] "fmt, __FUNCTION__, ##__VA_ARGS__)
		#define rt_alert( fmt, ...) log_save("jess", \
				kLogLevel_Warning, kLogTarget_Stdout | kLogTarget_Filesystem, "[%s] "fmt, __FUNCTION__, ##__VA_ARGS__)
		#define rt_fatal( fmt, ...) log_save("jess", \
				kLogLevel_Error, kLogTarget_Stdout | kLogTarget_Filesystem, "[%s] "fmt, __FUNCTION__, ##__VA_ARGS__)
		#define rt_trace( fmt, ...) log_save("jess", \
				kLogLevel_Trace, kLogTarget_Filesystem, "[%s] "fmt, __FUNCTION__, ##__VA_ARGS__)
	#else
		#define rt_echo( fmt, arg...) log_save("jess", \
				kLogLevel_Info, kLogTarget_Stdout | kLogTarget_Filesystem, "[%s] "fmt, __FUNCTION__, ##arg)
		#define rt_alert( fmt, arg...) log_save("jess", \
				kLogLevel_Warning, kLogTarget_Stdout | kLogTarget_Filesystem, "[%s] "fmt, __FUNCTION__, ##arg)
		#define rt_fatal( fmt, arg...) log_save("jess", \
				kLogLevel_Error, kLogTarget_Stdout | kLogTarget_Filesystem, "[%s] "fmt, __FUNCTION__, ##arg)
		#define rt_trace( fmt, arg...) log_save("jess", \
				kLogLevel_Trace, kLogTarget_Filesystem, "[%s] "fmt, __FUNCTION__, ##arg)
	#endif
#endif

struct jessrt_configure_node
{
	struct avltree_node_t index;
	struct avltree_node_t *next_scope;
	int next_scope_size;
	int arraysize;
	struct list_head array;
	struct list_head arrayitem;
	char scope_name[MAX_PRE_SEG_SIZE];
	char scope_value[MAX_VALUE_SIZE];	/* when JSL equal to JSL_DIR, socpe value is the name of config director,
										when JSL equal to JSL_ARCHIVE, scope value is the full path of config file,
										otherwise, scope value is the value string of josn node */
	JSL jsl;
	cJSON *jsonptr;
	cJSON *jsonparentptr;
	struct jessrt_configure_node *parentnode;
	int section;
};

#define rt_covert_node_byavl(tree) containing_record(tree, struct jessrt_configure_node, index)

extern unsigned char *rt_analyze_req_head(const CSTRPTR *request, CSTRPTR *title, CSTRPTR *option);

/* search config value by @key,
	here is a description of the query syntax, the example json file save at /etc/agv/@dir/@archive.json, context like:
		{
			"scope" : {
				"field" : "value"
			}
			"array" : [
				{
					"str1" : "1"
					"str2" : "2"
				}
				{
					"str1" : "10"
					"str2" : "20"
				}
				{
					"str1" : "100"
					"str2" : "200"
				}
			]
		}
	the correct query syntax:
	1. support direct query last field, command can be like: 'dir.archive.socpe.field':
		query result: 'dir.archive.socpe.field=value'
	2. support direct query dir node or any scope node, command can be like: 'dir' or 'dir.archive' ro 'dir.archive.scope',
		because the logic socpe node have no value storaged, the query result will be empty string.
		query result: 'dir=/etc/agv/dir/' or
					 	'dir.archive=/etc/agv/dir/archive.json' or
					 	'dir.archive.scope='
	3. support query size of specify array, command can be like: 'dir.archive.array[?]',
		query result: 'dir.archive.array[?]=3'
	4. support query item in array range by specify zero based index, command canbe like: 'dir.archive.array[1].str2'
		query result: 'dir.archive.array[1].str2=20'
	5. support query last item of array range, command canbe like: 'dir.archive.array[$].str1'
		query result: 'dir.archive.array[$].str1=100'
	the incorrect query syntax:
	1. request command include one or more unknonw segment that cannot be found in memory node structure, for example:
		'dir.archive.socpe.subscope.filed' , the "subscope" field can not be parse
	2. part of request command attach the bottom of memory node structure, for example:
		'dir.archive.scope.field.next.other', the "next.other" follow "field" is not allow.
	3. the specified index of array out of range, for example:
		'dir.archive.array[4]', index 4 cannot be indexes
	4. try to using command line not readonly, for example:
		'dir.archive.scope.field=100', cannot update variable any where by @search call
	5. try to using command context semantics are not readonly, for example:
		'dir.archive.array[+].str1', cannot use modifiable semantics by @search call
*/
extern int rt_search(JRP *jrp);

/* attempt to parse key string and render to last request segmenet, return the effective node pointer to calling thread by @output,
	calling thread will own the lock object after success call,user MUST explicit call @jsonify_release_lock to release the lock object.
	do NOT invoke lock object before @rt_reach_last_scope call  */
extern int rt_reach_last_scope(JRP *jrp, struct jessrt_configure_node **output);

/* @rt_config_update routine use to update/additional any configure to specify json object.
	@command MUST be the standard request string, null-terminated
	whether or not the result of the success call creates a new top scope node,
		the actual top scope node pointer owned by the request command will be return by parameter @archive
		@archive pointer canbe use to call @fs_override_file to overwrite new json data into physical file.
			exceptional, when the command order to detele top scope and success call, the @archive will return NULL, because file has been already deleted
	notice:
	1. @command MUST be the standard request string, formated like 'dir.archive.scope.scope.field=value'
	2. In string @command, only the following characters are allowed: ['0', '9'] ['a', 'z'] '.' '=' '_' SPACE
	3. an attempt to change the type of an existing node will fail, for example, json file at:
			/etc/agv/foundation/config.json
		context like:
			{
				"scope" : {
					"key" : "value"
				}
			}
		but request string set to : 'foundation.config.scope.key.subkey=other'
		In the original state, "key" is @JSL_FILED node with @cJSON::cJSON_String type,
		but command attempt to change "key" to @JSL_LOGIC_SCOPE node with @cJSON::cJSON_Object type
	4. calling thread can add a new field into exist json object, for example, by using above(3) json file:
		request string canbe : 'foundation.config.scope.newkey=newvalue'
		after success call, the json file willbe change to:
			{
				"scope" : {
					"key" : "value",
					"newkey" : "newvalue"
				}
			}
	5. calling thread can add hierarchical scope, after a exist scope, for example, by using above(4) json file:
		request string canbe : 'foundation.config.scope.subscope.key.type=1'
		after success call, the json file willbe change to:
			{
				"scope" : {
					"key" : "value",
					"newkey" : "newvalue",
					"subscope" : {
						"key" : {
							"type" : "1"
						}
					}
				}
			}
	6. calling thread can add a new josn file, save at the directory which specified by  it's parent node(dir),
		request string canbe : 'foundation.newconfig.scope.key=value'
		after success call,
			/etc/agv/foundation/newconfig.json
		will be touch, and it's context should like:
			{
				"scope" : {
					"key" : "value"
				}
			}
	7. an attempt to add a new dir is NOT allowed.
		request string can NOT be : 'new-dir.file.scope.field=value'
	8. keyword 'deleted' use to delete a existing scope or field to top-node, for example, by using above(5) json file:
		request string canbe : 'foundation.config.scope.subscope=deleted'
		after success call,, the json file willbe change to:
			{
				"scope" : {
					"key" : "value",
					"newkey" : "newvalue"
				}
			}
		request string also canbe : 'driver.elmo130=deleted'
		after success call, the /etc/agv/driver/elmo130.json file and all inner memory will be delete
	9. array modification and deletion are allowed,
		the syntax of array node modify canbe : 'foundation.config.scope.array[1].field=value', for example test json file before update:
			{
				"scope" : {
					"array" : [
						{
							"filed" : "old1"
						}
						{
							"filed" : "old2"
						}
					]
				}
			}
		after success update, the json file like:
			{
				"scope" : {
					"array" : [
						{
							"filed" : "old1"
						}
						{
							"filed" : "value"
						}
					]
				}
			}
	10.keyword 'deleted' can be use to remove one item in array or entire array,
		the syntax canbe : 'foundation.config.scope.array[0]=deleted' to remove zero index of array, or
		'foundation.config.scope.array=deleted' to remove entire array
	11.adding nodes to an array is NOT allowed.
	*/
extern int rt_config_update(JRP *jrp, struct jessrt_configure_node **archiveptr);

/* create a new namesapce scope */
extern int rt_create_dir(JRP *jrp);
extern int rt_remove_dir(JRP *jrp);
extern int rt_suface_remove_dir(JRP *jrp);

#endif

#if !defined JESS_CONF_H
#define JESS_CONF_H

/*
 * current support configuration fields below:
 *	@logdir			change logger directory to value specified by this key. default value is [PEDIR]/log
 *  @fsdir			change database file storage directory to value specified by this key.default value is /var/jess/
 *	@pro-affinity	set affinity to CPU core specified by this key which @PRO thread running on. system dispatch by default.
 *  @dpc-affinity	set affinity to CPU core specified by this key which @DPC thread running on. system dispatch by default.
 */

extern void os_load_conf(const char *cfgfile);
extern const char *os_query_conf(const char *key);
extern const char *os_query_conf_integer(const char *key, int *intvalue);
extern void os_release_conf();

#endif

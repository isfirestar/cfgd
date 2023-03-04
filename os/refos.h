#if !defined JESSOS_H
#define JESSOS_H

#include "ifos.h"
#include "threading.h"

#if _WIN32
#include <windows.h>
#define os_strcasecmp		_stricmp
#define os_strdup			_strdup
#define os_strtok			strtok_s
#define os_strncasecmp		_strnicmp
#define os_vsnprintf 		vsprintf_s
#define os_snprintf			sprintf_s

#define os_atomic_get(ptr)					InterlockedExchangeAdd((volatile LONG *)ptr, 0)
#define os_atomic_set(ptr, value)       	InterlockedExchange(( LONG volatile *)ptr, (LONG)value)

#else
#define os_strcasecmp		strcasecmp
#define os_strdup			strdup
#define os_strtok			strtok_r
#define os_strncasecmp		strncasecmp
#define os_vsnprintf 		vsnprintf
#define os_snprintf			snprintf

#define os_atomic_get(ptr)					__atomic_load_n(ptr, __ATOMIC_ACQUIRE)
#define os_atomic_set(ptr,value) 			__atomic_store_n(ptr, value, __ATOMIC_RELEASE)
#endif

extern char *os_strtrim(char *str);

/* initial function use for choose directory for work on cross platform */
extern void os_choose_dir(char *etcdir, char *fsdir, char *cfgdir, char *tmpdir, char *bindir);

/* try strndup(3p) on windows */
extern char *os_strndup(const char *s, size_t n);

/* memory mapping functions on cross platform */
extern void *os_mmap(file_descriptor_t fd, int size);
extern void os_munmap(void *addr, int size);
extern int os_msync(void *addr, int size);
extern int os_errno();

/* dir functions for cross os platform */
extern void *os_create_dirview(const char *dir);
extern void *os_foreach_dir(void *dirview, const char *parentdir, char *nextdir);
extern void *os_foreach_file(void *dirview, const char *parentdir, char *file);
extern void os_close_dirview(void *dirview);

#if 0
/* truncate file by descriptor */
extern file_descriptor_t jessos_ftruncate(file_descriptor_t fd, int length);
extern int jessos_truncate(const char *file, int length);
#endif

extern int os_remove_file(const char *pathname);
extern int os_rename_file(const char *oldpath, const char *newpath);

extern int os_delete_file(const char * oldpath);

extern int os_set_affinity(const lwp_t *thread, int cpus);

#endif

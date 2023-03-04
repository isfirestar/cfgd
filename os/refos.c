#include "refos.h"

#if _WIN32
#include <Windows.h>
#else
#include <sys/mman.h>
#include <unistd.h>
#include <pthread.h>
#include <errno.h>
#include <dirent.h>
#include <sys/stat.h>
#include <sched.h>
#endif

#include <string.h>
#include <assert.h>
#include <ctype.h>

#include "runtime.h"
#include "conf.h"

#define JESSFS_CONFIG_FILE 						"configure.db"
#define JESSFS_BINARY_FILE 	        			"binary.db"
#define JESSFS_CONFIG_SWAP						"configure.db.swap"

char *os_strtrim(char *str)
{
	char *cursor;
	int i;

	cursor = str;
	if (!cursor) {
		return NULL;
	}

	/* automatic removal of invisible characters at the beginning of a string */
	while (*cursor && ( !isprint(*cursor) || (0x20 == *cursor) ) ) {
		++cursor;
	}

	if (0 == *cursor) {
		return NULL;
	}

	for (i = (int)strlen(cursor) - 1; i >= 0; i--) {
		/* when an invisible character or space is found,
			the following character of string is automatically ignored  */
		if (!isprint(cursor[i]) || 0x20 == cursor[i]) {
			cursor[i] = 0;
		} else {
			break;
		}
	}
	return cursor;
}

void os_choose_dir(char *etcdir, char *fsdir, char *cfgdir, char *tmpdir, char *bindir)
{
#if _WIN32
	char pedir[MAXPATH];

	posix__getpedir2(pedir, sizeof(pedir));

	if (etcdir) {
		sprintf(etcdir, "%s\\etc\\agv\\", pedir);
	}

	if (fsdir) {
		sprintf(fsdir, "%s\\var\\jess\\", pedir);
	}

	if (cfgdir) {
		sprintf(cfgdir, "%s\\var\\jess\\%s", pedir, JESSFS_CONFIG_FILE);
	}

	if (tmpdir) {
		sprintf(tmpdir, "%s\\var\\jess\\%s", pedir, JESSFS_CONFIG_SWAP);
	}

	if (bindir) {
		sprintf(bindir, "%s\\var\\jess\\%s", pedir, JESSFS_BINARY_FILE);
	}
#else
	const char *conf_fsdir;

	if (etcdir) {
		strcpy(etcdir, "/etc/agv/");
	}

	if (fsdir) {
		conf_fsdir = os_query_conf("fsdir");
		strcpy(fsdir, conf_fsdir ? conf_fsdir : "/var/jess/");
	}

	if (cfgdir) {
		conf_fsdir = os_query_conf("fsdir");
		sprintf(cfgdir, "%s/%s", conf_fsdir ? conf_fsdir : "/var/jess", JESSFS_CONFIG_FILE);
	}

	if (tmpdir) {
		conf_fsdir = os_query_conf("fsdir");
		sprintf(tmpdir, "%s/%s", conf_fsdir ? conf_fsdir : "/var/jess", JESSFS_CONFIG_SWAP);
	}

	if (bindir) {
		conf_fsdir = os_query_conf("fsdir");
		sprintf(bindir, "%s/%s", conf_fsdir ? conf_fsdir : "/var/jess", JESSFS_BINARY_FILE);
	}
#endif
}

#if _WIN32

char *os_strndup(const char *s, size_t n)
{
	char *dup;

	dup = (char *)malloc(n + 1);
	if (dup) {
		strncpy(dup, s, n);
		dup[n] = 0;
	}
	return dup;
}

void *os_mmap(file_descriptor_t fd, int size)
{
	HANDLE mpfd;
	void *addr;

	mpfd = CreateFileMappingA(fd, NULL, PAGE_READWRITE, 0, (DWORD)size, NULL);
	if (!mpfd) {
		return NULL;
	}

	addr = MapViewOfFile(mpfd, FILE_MAP_READ | FILE_MAP_WRITE, 0, 0, size);

	CloseHandle(mpfd);
	mpfd = NULL;
	return addr;
}

void os_munmap(void *addr, int size)
{
	if (addr) {
		UnmapViewOfFile(addr);
	}
}

int os_msync(void *addr, int size)
{
	int retval;

	/*  To flush all the dirty pages plus the metadata for the file and ensure that they are physically written to disk,
		call FlushViewOfFile and then call the FlushFileBuffers function. */
	retval = FlushViewOfFile(addr, size);
	if (!retval) {
		return posix__makeerror(GetLastError());
	}

	/* FlushFileBuffer(); */
	return 0;
}

int os_errno()
{
	return GetLastError();
}

void *os_create_dirview(const char *dir)
{
	WIN32_FIND_DATAA wfd;
	HANDLE find;
	char path[MAXPATH];

	os_snprintf(path, sizeof(path), "%s\\*.*", dir);

	find = FindFirstFileA(path, &wfd);
	if (INVALID_HANDLE_VALUE == find) {
		return NULL;
	}
	return (void *)find;
}

void *os_foreach_dir(void *dirview, const char *parentdir, char *nextdir)
{
	HANDLE find;
	char currentdir[1024];
	WIN32_FIND_DATAA wfd;

	if (!dirview || !parentdir || !nextdir) {
		return NULL;
	}

	find = (HANDLE)dirview;
	while (FindNextFileA(find, &wfd)) {
		if ('.' == wfd.cFileName[0]) {
			continue;
		}

		os_snprintf(currentdir, sizeof(currentdir), "%s%s", parentdir, wfd.cFileName);
		if (wfd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
			strcpy(nextdir, wfd.cFileName);
			return dirview;
		}
	}

	return NULL;
}

void *os_foreach_file(void *dirview, const char *parentdir, char *file)
{
	HANDLE find;
	char currentdir[1024];
	WIN32_FIND_DATAA wfd;

	if (!dirview || !parentdir || !file) {
		return NULL;
	}

	find = (HANDLE)dirview;
	while (FindNextFileA(find, &wfd)) {
		if ('.' == wfd.cFileName[0]) {
			continue;
		}

		os_snprintf(currentdir, sizeof(currentdir), "%s%s", parentdir, wfd.cFileName);
		if (!(wfd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) ) {
			strcpy(file, wfd.cFileName);
			return dirview;
		}
	}

	return NULL;
}

void os_close_dirview(void *dirview)
{
	FindClose((HANDLE)dirview);
}

#if 0
int jessos_truncate(const char *file, int length)
{
	HANDLE newfd;
	unsigned char *initial;
	DWORD written;

	if (!file) {
		return -EINVAL;
	}

	newfd = CreateFileA(file, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ, NULL, TRUNCATE_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
	if (INVALID_HANDLE_VALUE == newfd) {
		return INVALID_FILE_DESCRIPTOR;
	}

	SetFilePointer(newfd, 0, NULL, FILE_BEGIN);

	if (length > 0) {
		initial = (unsigned char *)malloc(length);
		if (!initial) {
			return INVALID_FILE_DESCRIPTOR;
		}
		memset(initial, 0, length);

		if (!WriteFile(newfd, initial, length, &written, NULL)) {
			CloseHandle(newfd);
			newfd = INVALID_FILE_DESCRIPTOR;
		}

		free(initial);
	}

	return newfd;
}

file_descriptor_t jessos_ftruncate(file_descriptor_t fd, int length)
{
	char index_file[MAXPATH];
	HANDLE newfd;
	unsigned char *initial;
	DWORD written;

	if (fd != INVALID_HANDLE_VALUE) {
		CloseHandle(fd);
	}

	os_choose_dir(NULL, NULL, NULL, index_file, NULL);
	newfd = CreateFileA(index_file, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ, NULL, TRUNCATE_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
	if (INVALID_HANDLE_VALUE == newfd) {
		return INVALID_FILE_DESCRIPTOR;
	}

	SetFilePointer(newfd, 0, NULL, FILE_BEGIN);

	if (length > 0) {
		initial = (unsigned char *)malloc(length);
		if (!initial) {
			return INVALID_FILE_DESCRIPTOR;
		}
		memset(initial, 0, length);

		if (!WriteFile(newfd, initial, length, &written, NULL)) {
			CloseHandle(newfd);
			newfd = INVALID_FILE_DESCRIPTOR;
		}

		free(initial);
	}
	return newfd;
}
#endif

int os_remove_file(const char *pathname)
{
	if (DeleteFileA(pathname)) {
		return 0;
	}

	return posix__makeerror(GetLastError());
}

int os_rename_file(const char *oldpath, const char *newpath)
{
	if (MoveFileExA(oldpath, newpath, MOVEFILE_REPLACE_EXISTING)) {
		return 0;
	}

	return posix__makeerror(GetLastError());
}


int os_delete_file(const char * oldpath){
	if (DeleteFileA(oldpath)){
		return 0;
	}
	return posix__makeerror(GetLastError());
}

int os_set_affinity(const lwp_t *thread, int cpus)
{
	DWORD e;

	if (!SetThreadAffinityMask(thread->pid_, cpus)) {
		e = GetLastError();
		rt_alert("failed binding navigation thread on CPU-%d, error=%d", cpus, e);
		return posix__makeerror(e);
	} else {
		rt_echo("success binding navigation thread on CPU-%d", cpus);
	}

	return 0;
}

#else

char *os_strndup(const char *s, size_t n)
{
	return strndup(s, n);
}

void *os_mmap(file_descriptor_t fd, int size)
{
	void *addr;

	/* pa_offset = offset & ~(sysconf(_SC_PAGE_SIZE) - 1) */
	addr = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
	if (MAP_FAILED == addr) {
		addr = NULL;
	}

	return addr;
}

void os_munmap(void *addr, int size)
{
	if (addr) {
		munmap(addr, size);
	}
}

int os_msync(void *addr, int size)
{
	int retval;

	/* using asynchronous operation corresponding to MS-windows implement */
	retval = msync(addr, size, MS_ASYNC);
	if (0 != retval) {
		return posix__makeerror(errno);
	}

	return 0;
}

int os_errno()
{
	return errno;
}

void *os_create_dirview(const char *dir)
{
	return (void *)opendir(dir);
}

void *os_foreach_dir(void *dirview, const char *parentdir, char *nextdir)
{
	struct dirent *ent;
	DIR *dirp;
	char currentdir[1024];
	struct stat st;

	if (!dirview || !parentdir || !nextdir) {
		return NULL;
	}

	dirp = (DIR *)dirview;
	while ( NULL != (ent = readdir(dirp)) ) {
		if ( !ent->d_name ) {
			continue;
		}
		if ('.' == ent->d_name[0]) {
			continue;
		}

 		if (strlen(ent->d_name) >= 128) {
 			continue;
 		}

		os_snprintf(currentdir, sizeof(currentdir), "%s%s", parentdir, ent->d_name);
		stat(currentdir, &st);
		if (S_ISDIR(st.st_mode)) {
			strcpy(nextdir, ent->d_name);
			return dirview;
		}
	}

	return NULL;
}

void *os_foreach_file(void *dirview, const char *parentdir, char *file)
{
	struct dirent *ent;
	DIR *dirp;
	char currentdir[1024];
	struct stat st;

	if (!dirview || !parentdir || !file) {
		return NULL;
	}

	dirp = (DIR *)dirview;
	while ( NULL != (ent = readdir(dirp)) ) {
		if ( !ent->d_name ) {
			continue;
		}
		if ('.' == ent->d_name[0]) {
			continue;
		}

		if (strlen(ent->d_name) >= 128) {
			continue;
		}

		os_snprintf(currentdir, sizeof(currentdir), "%s%s", parentdir, ent->d_name);
		stat(currentdir, &st);

		if (!S_ISREG(st.st_mode) ) {
			continue;
		}

		strcpy(file, ent->d_name);
		return dirview;
	}

	return NULL;
}

void os_close_dirview(void *dirview)
{
	closedir((DIR *)dirview);
}

#if 0
file_descriptor_t jessos_ftruncate(file_descriptor_t fd, int length)
{
	if (0 != ftruncate(fd, length)) {
		rt_fatal("syscall ftruncate error %d", errno);
	}
	return fd;
}

int jessos_truncate(const char *file, int length)
{
	if (!file) {
		return -EINVAL;
	}

	if ( 0 != truncate(file, length) ) {
		rt_fatal("syscall truncate error %d", errno);
		return -1;
	}

	return 0;
}
#endif

int os_remove_file(const char *pathname)
{
	if ( 0== remove(pathname) ) {
		return 0;
	}

	return posix__makeerror(errno);
}

int os_rename_file(const char *oldpath, const char *newpath)
{
	if ( 0== rename(oldpath, newpath) ) {
		return 0;
	}

	return posix__makeerror(errno);
}


int os_delete_file(const char * oldpath){
	if (remove(oldpath)){
		return 0;
	}
	return posix__makeerror(errno);
}

int os_set_affinity(const lwp_t *thread, int cpus)
{
	cpu_set_t set;
	int e;

	CPU_ZERO(&set);
	CPU_SET(cpus, &set);
	e = pthread_setaffinity_np(thread->pid, sizeof(set), &set);
	if (0 != e) {
		rt_alert("failed binding navigation thread on CPU-%d, error=%d", cpus, e);
	} else {
		rt_echo("success binding navigation thread on CPU-%d", cpus);
	}

	return posix__makeerror(e);
}

#endif

#if !defined EASYJESS_H
#define EASYJESS_H

#if _WIN32
	#include <windows.h>
	#if !defined posix__atomic_get
		#define posix__atomic_get(ptr) InterlockedExchangeAdd((volatile LONG *)ptr, 0)
		#define posix__atomic_get64(ptr) InterlockedExchangeAdd64((volatile LONG64 *)ptr, 0)
	#endif

	#if !defined posix__atomic_set
		#define posix__atomic_set(ptr,value) InterlockedExchange(( LONG volatile *)ptr, (LONG)value)
		#define posix__atomic_set64(ptr,value) InterlockedExchange64(( LONG64 volatile *)ptr, (LONG64)value)
	#endif
#else
	#if !defined posix__atomic_get
		#define posix__atomic_get(ptr) __atomic_load_n(ptr, __ATOMIC_ACQUIRE)
		#define posix__atomic_get64(ptr) __atomic_load_n(ptr, __ATOMIC_ACQUIRE)
	#endif

	#if !defined posix__atomic_set
		#define posix__atomic_set(ptr,value) __atomic_store_n(ptr, value, __ATOMIC_RELEASE)
		#define posix__atomic_set64(ptr,value) __atomic_store_n(ptr, value, __ATOMIC_RELEASE)
	#endif
#endif

#endif

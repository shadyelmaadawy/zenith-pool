#ifndef ZT_LOCK_INTERNAL_WIN32_H
#define ZT_LOCK_INTERNAL_WIN32_H

#include <windows.h>

/**
 * @file zt_lock_internal_win32.h
 * @brief PRIVATE: Win32 layout of ZTLock, shared by the lock and condition implementations. Never include it from public headers.
 */

 /// Win32 lock: a CRITICAL_SECTION plus an atomic owner counter.
typedef struct ZTLock {

	/// Number of threads currently holding the lock (not counting a thread asleep in a condition wait).
	volatile long    locked;

	/// The OS lock that provides the actual mutual exclusion.
	CRITICAL_SECTION section;

} ZTLock, *PZTLock;

#endif /* ZT_LOCK_INTERNAL_WIN32_H */

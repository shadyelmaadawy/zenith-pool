#include "memory/zt_memory.h"
#include "synchronization/zt_lock.h"
#include "synchronization/zt_lock_internal_win32.h"
#include "synchronization/zt_atomic.h"

bool zt_lock_init(PZTLock* lock) {

	if (lock == NULL || *lock != NULL)
		return false;

	bool init_state = false;
	PZTLock temp_lock = NULL;
	if (!zt_allocate(&temp_lock, sizeof(ZTLock)))
		goto cleanup;

	zt_memset(temp_lock, 0, sizeof(ZTLock));
	InitializeCriticalSection(&temp_lock->section);
	*lock = temp_lock;
	init_state = true;

cleanup:

	return init_state;

}

bool zt_lock_deinit(PZTLock* lock) {

	if (lock == NULL || *lock == NULL)
		return false;

	/* wait until nobody holds it */
	for (;;) {
		long is_locked = 0;
		zt_atomic_read(&(*lock)->locked, &is_locked);
		if (is_locked == 0)
			break;

		SwitchToThread();                      /* don't burn the core while spinning */
	}

	DeleteCriticalSection(&(*lock)->section);

	return zt_deallocate(lock);

}

void zt_lock_acquire(PZTLock lock) {

	if (lock == NULL)
		return;

	EnterCriticalSection(&lock->section);
	zt_atomic_increment(&lock->locked);

}

void zt_lock_release(PZTLock lock) {
	
	if (lock == NULL)
		return;

	LeaveCriticalSection(&lock->section);
	zt_atomic_decrement(&lock->locked);

}
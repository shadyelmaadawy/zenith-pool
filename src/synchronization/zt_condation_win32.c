#include <windows.h>
#include "memory/zt_memory.h"
#include "synchronization/zt_lock.h"
#include "synchronization/zt_atomic.h"
#include "synchronization/zt_condition.h"
#include "synchronization/zt_lock_internal_win32.h"

struct ZTCondation {

	CONDITION_VARIABLE condition;

};

/** @brief Creates a condition variable; *condation must be NULL on entry. */
/** @brief Creates a condition variable; *condation must be NULL on entry. */
bool zt_condation_init(PZTCondation* condation) {

	if (condation == NULL || *condation != NULL)
		return false;

	bool         init_state = false;
	PZTCondation temp_condation = NULL;

	if (!zt_allocate((void**)&temp_condation, sizeof(struct ZTCondation)))
		goto cleanup;

	zt_memset(temp_condation, 0, sizeof(struct ZTCondation));
	InitializeConditionVariable(&temp_condation->condition);

	*condation = temp_condation;
	init_state = true;

cleanup:
	return init_state;

}

/*
 * Win32 condition variables need no destroy call. The wake_all is a best-effort courtesy:
 * the real rule (no waiters left) is the caller's, see the header.
 */
bool zt_condation_deinit(PZTCondation* condation) {

	if (condation == NULL || *condation == NULL)
		return false;

	zt_condation_wake_all(*condation);

	return zt_deallocate(condation);

}

/*
 * SleepConditionVariableCS releases the critical section while sleeping, so the owner counter
 * must drop before and come back after, otherwise zt_lock_deinit would see a phantom owner
 * for as long as a worker sleeps. The section is re-acquired on every return, timeout included.
 */
bool zt_condation_sleep(PZTCondation condation, PZTLock lock, unsigned long millisecond) {

	if (!condation || !lock || condation == NULL || lock == NULL)
		return false;
 
	zt_atomic_decrement(&lock->locked);
	BOOL woken = SleepConditionVariableCS(&condation->condition, &lock->section, millisecond);
	zt_atomic_increment(&lock->locked);

	return woken != FALSE;

}

/** @brief Wakes one sleeper; a wake with no sleepers is lost. */
bool zt_condation_wake(PZTCondation condation) {

	if (!condation || condation == NULL)
		return false;

	WakeConditionVariable(&condation->condition);
	return true;

}

/** @brief Wakes every sleeper. */
bool zt_condation_wake_all(PZTCondation condation) {

	if (!condation || condation == NULL)
		return false;

	WakeAllConditionVariable(&condation->condition);
	return true;

}
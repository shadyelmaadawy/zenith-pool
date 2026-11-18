#ifndef ZT_CONDITION_H
#define ZT_CONDITION_H

#include <stdbool.h>

#include "synchronization/zt_lock.h"

/** @brief Condition variable: lets threads sleep until another thread signals; opaque. */
typedef struct ZTCondation* PZTCondation;

/**
 * @brief Creates a condition variable.
 *
 * @param[out] condation Pointer to a variable that must hold NULL. Receives the new object on success; left untouched on failure.
 *
 * @return true on success; false if @p condation is NULL, *@p condation is not NULL, or allocation failed.
 *
 * @note Release with zt_condation_deinit().
 */
bool zt_condation_init(PZTCondation* condation);

/**
 * @brief Releases the condition variable and nulls the pointer.
 *
 * @param[in,out] condation Pointer to the variable holding the object. Set to NULL on success.
 *
 * @return true on success; false if @p condation or *@p condation is NULL, or the free failed.
 *
 * @warning Call only when no thread is waiting on it (join every waiter first). Waking sleepers right before the free does not make it safe.
 */
bool zt_condation_deinit(PZTCondation* condation);

/**
 * @brief Releases @p lock and sleeps until woken or until the timeout, then re-acquires @p lock before returning.
 *
 * @param condation   Condition variable.
 * @param lock        Lock the caller holds exactly once.
 * @param millisecond Timeout in milliseconds; 0xFFFFFFFF waits forever.
 *
 * @return true if woken; false on timeout, on NULL arguments, or on an OS failure.
 *
 * @note The lock is held again when the call returns, even on timeout.
 * @warning Wakeups can be spurious: always call this in a loop that re-checks your condition.
 * @warning Wakes are not remembered. Change the shared state under the lock BEFORE calling wake, or a sleeper that is about to wait can miss it forever.
 */
bool zt_condation_sleep(PZTCondation condation, PZTLock lock, unsigned long millisecond);

/**
 * @brief Wakes at most one sleeping thread.
 *
 * @param condation Condition variable.
 *
 * @return true on success; false if @p condation is NULL.
 *
 * @note With no sleepers it does nothing and the wake is lost.
 */
bool zt_condation_wake(PZTCondation condation);

/**
 * @brief Wakes every sleeping thread.
 *
 * @param condation Condition variable.
 *
 * @return true on success; false if @p condation is NULL.
 */
bool zt_condation_wake_all(PZTCondation condation);

#endif /* ZT_CONDITION_H */
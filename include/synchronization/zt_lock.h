#ifndef ZT_LOCK_H
#define ZT_LOCK_H

#include <stdbool.h>

/** @brief Mutual-exclusion lock; opaque. */
typedef struct ZTLock* PZTLock;

/**
 * @brief Creates an unlocked lock.
 *
 * @param[out] lock Pointer to a variable that must hold NULL. Receives the new lock on success; left untouched on failure.
 *
 * @return true on success; false if @p lock is NULL, *@p lock is not NULL, or allocation failed.
 *
 * @note Release with zt_lock_deinit().
 */
bool zt_lock_init(PZTLock* lock);

/**
 * @brief Releases the lock and nulls the pointer.
 *
 * @param[in,out] lock Pointer to the variable holding the lock. Set to NULL on success.
 *
 * @return true on success; false if @p lock or *@p lock is NULL, or the free failed.
 *
 * @warning Call only after every thread that uses the lock has finished (joined). It waits until the lock is free, but that check is best-effort and cannot see a thread that is about to acquire.
 */
bool zt_lock_deinit(PZTLock* lock);

/**
 * @brief Blocks until the calling thread owns the lock.
 *
 * @param lock Lock from zt_lock_init(). A NULL lock makes the call a silent no-op.
 *
 * @note The same thread may acquire again, but then it must release the same number of times.
 * @warning Do not hold it recursively while calling zt_condation_sleep(): the sleep releases it only once.
 */
void zt_lock_acquire(PZTLock lock);

/**
 * @brief Releases one level of ownership.
 *
 * @param lock Lock from zt_lock_init(). A NULL lock makes the call a silent no-op.
 *
 * @warning Only the owning thread may release; releasing a lock you do not own is undefined behavior.
 */
void zt_lock_release(PZTLock lock);

#endif /* ZT_LOCK_H */
#ifndef ZT_ATOMIC_H
#define ZT_ATOMIC_H

/**
 * @file zt_atomic.h
 * @brief Atomic operations on a long.
 *
 * Every operation is sequentially consistent and acts as a full memory barrier.
 * Never mix atomic and plain access to the same variable: pick one style per variable.
 * An atomic protects ONE variable; a decision that combines several variables still needs a lock.
 */

 /** @brief Atomically adds 1 to *value. */
void zt_atomic_increment(volatile long* value);

/** @brief Atomically subtracts 1 from *value. */
void zt_atomic_decrement(volatile long* value);

/** @brief Atomically reads *value into *out_value. */
void zt_atomic_read(volatile long* value, long* out_value);

/** @brief Atomically stores new_value into *value. */
void zt_atomic_write(volatile long* value, long new_value);

#endif /* ZT_ATOMIC_H */

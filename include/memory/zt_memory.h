/**
 * @file zt_memory.h
 * @brief Platform-neutral memory management contract.
 *
 * This header defines the behavior every platform implementation must
 * provide. Failures are reported through the bool return value; no function
 * in this API aborts the process.
 *
 * Guarantees shared by all implementations:
 *  - Blocks returned by allocate() are zero-initialized.
 *  - Blocks are aligned to at least 16 bytes.
 */

#ifndef ZT_MEMORY_H
#define ZT_MEMORY_H

#include <stdbool.h>
#include <stddef.h>

 /**
  * @brief Allocates a zero-initialized block of memory.
  *
  * @param[out] address Receives the address of the new block on success.
  *                     Left untouched on failure.
  * @param[in]  size    Size of the block in bytes. Must be greater than 0.
  *
  * @return true on success; false if @p address is NULL, @p size is 0,
  *         or the request could not be satisfied.
  *
  * @note Release the block with deallocate().
  */
bool zt_allocate(void** address, size_t size);

/**
 * @brief Releases a block obtained from allocate() and nulls the pointer.
 *
 * @param[in,out] address Pointer to the variable holding the block address.
 *                        Set to NULL on success to guard against double
 *                        free and use-after-free.
 *
 * @return true on success; false if @p address or *@p address is NULL, or
 *         the release failed. On failure *@p address is left unchanged.
 *
 * @warning Passing a block that was not returned by allocate() results in
 *          undefined behavior.
 */
bool zt_deallocate(void** address);

/**
 * @brief Resizes a block obtained from allocate(), preserving its contents.
 *
 * @param[in,out] address  Pointer to the variable holding the block address.
 *                         If *@p address is NULL, behaves like allocate().
 *                         On success it receives the (possibly moved)
 *                         block address; the old address becomes invalid.
 * @param[in]     new_size New size in bytes. Must be greater than 0.
 *
 * @return true on success; false if @p address is NULL, @p new_size is 0,
 *         or the request could not be satisfied. On failure the original
 *         block remains valid and *@p address is left unchanged.
 *
 * @note Contents up to min(old size, @p new_size) are preserved.
 * @note When the block grows, the added bytes are zero-initialized.
 */
bool zt_reallocate(void** address, size_t new_size);

/**
 * @brief Copies @p size bytes from @p src to @p dest.
 *
 * @param[out] dest Destination buffer, at least @p size bytes.
 * @param[in]  src  Source buffer, at least @p size bytes.
 * @param[in]  size Number of bytes to copy.
 *
 * @warning The buffers must not overlap (same contract as memcpy).
 * @warning No NULL checks are performed; invalid pointers result in
 *          undefined behavior.
 */
void zt_memcpy(void* dest, const void* src, size_t size);

bool zt_equal(const void* a, const void* b, size_t size);

void zt_memset(void* dest, int value, size_t size);

#endif /* ZT_MEMORY_H */
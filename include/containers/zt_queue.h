#ifndef ZT_QUEUE_H
#define ZT_QUEUE_H

#include <stdbool.h>
#include <stddef.h>

/** @brief Creates a queue that stores copies of fixed-size elements. */
typedef struct ZTList* PZTQueue;

/**
 * @brief Creates an empty queue.
 *
 * @param[out] queue Pointer to a variable that must hold NULL. Receives the new queue on success; left untouched on failure.
 * @param[in]  size  Size in bytes of every element. Fixed for the life of the queue. Must be greater than 0.
 *
 * @return true on success; false if @p queue is NULL, *@p queue is not NULL, @p size is 0, or allocation failed.
 *
 * @note Release with zt_queue_deinit().
 */
bool zt_queue_init(PZTQueue* queue, size_t size);

/**
 * @brief Releases the queue and every element still inside it, then nulls the pointer.
 *
 * @param[in,out] queue Pointer to the variable holding the queue. Set to NULL on success.
 *
 * @return true on success; false if @p queue or *@p queue is NULL, or the final release failed.
 *
 * @note Elements are plain byte copies: anything they point to (handles, buffers) is NOT released. Dequeue and clean those up first.
 */
bool zt_queue_deinit(PZTQueue* queue);

/**
 * @brief Appends a copy of an element at the back.
 *
 * @param queue Queue from zt_queue_init().
 * @param value Pointer to the element. Exactly the queue's element size is copied from it.
 *
 * @return true if stored; false if @p queue or @p value is NULL, or allocation failed. On failure the queue is unchanged.
 */
bool zt_queue_enqueue(PZTQueue queue, void* value);

/**
 * @brief Removes the oldest element and copies it out.
 *
 * @param queue     Queue from zt_queue_init().
 * @param out_value Buffer of at least the queue's element size that receives the element.
 *
 * @return true if an element was removed; false if @p queue or @p out_value is NULL, or the queue is empty. On failure @p out_value is left untouched.
 */
bool zt_queue_dequeue(PZTQueue queue, void* out_value);

/**
 * @brief Returns the number of elements currently stored; 0 if @p queue is NULL.
 */
long zt_queue_count(PZTQueue queue);

/**
 * @brief Returns true if the queue holds no elements, or if @p queue is NULL.
 */
bool zt_queue_is_empty(PZTQueue queue);

#endif // !ZT_QUEUE_H
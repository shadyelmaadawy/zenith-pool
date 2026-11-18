#ifndef ZT_POOL_H
#define ZT_POOL_H

#include <stdbool.h>
#include <stddef.h>

/**
 * @file zt_pool.h
 * @brief Dynamic thread pool: a bounded set of worker threads that run submitted jobs.
 *
 * Lifecycle:
 *   zt_pool_create -> configure (zt_pool_set_*) -> zt_pool_bootstrap -> zt_pool_push ... -> zt_pool_teardown -> zt_pool_destroy
 *
 * Workers:
 *   - bootstrap starts the "minimum" workers; they stay alive for as long as the pool runs.
 *   - push starts one more worker (up to "maximum") when more jobs are queued than there are idle workers.
 *   - a worker above the minimum leaves after the keep-alive time without work.
 *
 * Thread safety:
 *   - zt_pool_push: any thread, at any time while the pool runs, including from inside a job.
 *   - zt_pool_set_*: any thread; a new value is used by the decisions taken after it.
 *   - zt_pool_bootstrap / zt_pool_teardown: one thread at a time. Never call them from inside a job.
 *   - zt_pool_destroy: needs exclusive access; no other call on the pool may run during or after it.
 *
 * Every function reports failure through its bool return value and never aborts the process.
 */

 /** @brief Thread pool; opaque. */
typedef struct ZTPool ZTPool;
typedef ZTPool* PZTPool;

/** @brief Work function of a job. Runs once, on one of the pool's worker threads. */
typedef void (*ZT_POOL_ROUTINE)(void* parameter);

/**
 * @brief Creates a stopped pool with default settings.
 *
 * @param[out] pool Pointer to a variable that must hold NULL. Receives the new pool on success; left untouched on failure.
 *
 * @return true on success; false if @p pool is NULL, *@p pool is not NULL, or allocation failed (nothing is leaked).
 *
 * @note Defaults: minimum = max(1, processors / 2), maximum = processors, keep-alive = 3000 ms, stack size = system default.
 * @note Release with zt_pool_destroy().
 */
bool zt_pool_create(PZTPool* pool);

/**
 * @brief Stops the pool if it is running, then releases it and nulls the pointer.
 *
 * @param[in,out] pool Pointer to the variable holding the pool. Set to NULL on return.
 *
 * @return true if released; false if @p pool or *@p pool is NULL, or the final free failed.
 *
 * @note A running pool is stopped exactly as zt_pool_teardown() does: accepted jobs finish first.
 * @warning Needs exclusive access: no other thread may use the pool during or after this call.
 */
bool zt_pool_destroy(PZTPool* pool);

/**
 * @brief Sets the stack size of the workers created from now on.
 *
 * @param pool  Pool from zt_pool_create().
 * @param value Size in bytes. 0 selects the system default (1 MB unless the executable says otherwise).
 *
 * @return true if stored; false if @p pool is NULL or @p value is negative.
 *
 * @note The size is a reservation, rounded up to the OS granularity (64 KB). Already running workers are not affected.
 */
bool zt_pool_set_stack_size(PZTPool pool, long value);

/**
 * @brief Sets how long a worker above the minimum may stay idle before it leaves the pool.
 *
 * @param pool  Pool from zt_pool_create().
 * @param value Time in milliseconds. 0 means extra workers never time out (they live until teardown).
 *
 * @return true if stored; false if @p pool is NULL or @p value is negative.
 */
bool zt_pool_set_keep_alive(PZTPool pool, long value);

/**
 * @brief Sets the number of workers that always stay alive while the pool runs.
 *
 * @param pool  Pool from zt_pool_create().
 * @param value Greater than 0.
 *
 * @return true if stored; false if @p pool is NULL or @p value is not positive.
 *
 * @note The relation minimum <= maximum is checked by zt_pool_bootstrap(), so the two setters may be called in any order.
 */
bool zt_pool_set_minimum_workers(PZTPool pool, long value);

/**
 * @brief Sets the largest number of workers the pool may run at once.
 *
 * @param pool  Pool from zt_pool_create().
 * @param value Greater than 0.
 *
 * @return true if stored; false if @p pool is NULL or @p value is not positive.
 *
 * @note The relation minimum <= maximum is checked by zt_pool_bootstrap().
 */
bool zt_pool_set_maximum_workers(PZTPool pool, long value);

/**
 * @brief Starts the pool: validates the settings and creates the minimum workers.
 *
 * @param pool Pool from zt_pool_create().
 *
 * @return true if the pool is running (also when it already was); false if @p pool is NULL, the settings are invalid
 *         (minimum is 0, maximum is 0, or minimum is greater than maximum), or a worker could not be created.
 *
 * @note On failure the pool stays stopped and no worker is left running.
 */
bool zt_pool_bootstrap(PZTPool pool);

/**
 * @brief Stops the pool gracefully and waits until every worker has exited.
 *
 * Stops accepting jobs, lets the workers run every job that was already accepted, then joins them.
 *
 * @param pool Pool from zt_pool_create().
 *
 * @return true if the pool was running and is now stopped; false if @p pool is NULL, or it was never started or is already stopped.
 *
 * @note Afterwards only zt_pool_destroy() is supported on the pool; zt_pool_push() returns false.
 * @warning Blocks until the running jobs return: a job that never returns blocks the teardown forever.
 * @warning Never call it from inside a job (a worker cannot wait for itself).
 */
bool zt_pool_teardown(PZTPool pool);

/**
 * @brief Queues a job. It runs exactly once on one of the workers.
 *
 * @param pool      Running pool.
 * @param routine   Function to run. Must not be NULL.
 * @param parameter Passed to @p routine unchanged, The pool never reads or frees it; it must stay valid until the job has run.
 *
 * @return true if the job was accepted; false if @p pool or @p routine is NULL, the pool is not running, or the queue could not grow.
 *
 * @note Jobs start in the order they were pushed, but run concurrently: there is no completion order.
 * @note May start one more worker (up to the maximum) when more jobs are queued than there are idle workers.
 * @note Every job accepted (true) is run even if teardown starts right afterwards.
 */
bool zt_pool_push(PZTPool pool, ZT_POOL_ROUTINE routine, void* parameter);

#endif /* ZT_POOL_H */
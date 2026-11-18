/*
 * @file zt_thread.h
 * @brief Platform-neutral worker thread contract.
 *
 * A ZTThread is a single OS thread with an explicit lifecycle:
 *
 *   PENDING --run--> RUNNING -- routine returns
 *                      |  ^
 *                suspend|  |resume
 *                      v  |
 *                    SUSPEND
 *   RUNNING / SUSPEND --terminate --> TERMINATED
 *
 * A thread object runs its routine once. It cannot be restarted after it
 * leaves PENDING.
 *
 * Thread safety:
 *  - State transitions are atomic. zt_thread_run() may be called
 *    concurrently on the same object; exactly one call succeeds.
 *  - zt_thread_destroy() requires exclusive ownership: no other thread may
 *    use the object during or after the call.
 *
 * All functions report failure through the bool return value and never
 * abort the process.
 */

#ifndef ZT_THREAD_H
#define ZT_THREAD_H

#include <stdbool.h>

typedef struct ZTThread ZTThread;
typedef ZTThread* PZTThread;
typedef PZTThread ZT_THREAD_HANDLE;

 /* @brief Function executed by the thread. Runs once, in the new thread. */
typedef void (*ZT_THREAD_ROUTINE)(void* parameter);

/* @brief Lifecycle state of a ZTThread. Read it with zt_thread_get_state(). */
typedef enum ZT_THREAD_STATE {

	/// Created, not started yet.
	ZT_THREAD_PENDING,

	/// Started; the routine is executing (or the OS is still starting it).
	ZT_THREAD_RUNNING,

	/// Routine returned normally, Derived from the OS handle, never stored.
	ZT_THREAD_COMPLETED,

	/// Paused by zt_thread_suspend().
	ZT_THREAD_SUSPENDED,

	/// Killed by zt_thread_terminate().
	ZT_THREAD_TERMINATED,

} ZT_THREAD_STATE;

/**
 * @brief Allocates a new thread object in the PENDING state.
 *
 * @param[out] handle Pointer to a variable that must hold NULL. Receives the new object on success; left untouched on failure.
 *
 * @return true on success; false if @p handle is NULL, *@p handle is not NULL (refused so an existing object is never leaked), or allocation failed.
 *
 * @note The object is zero-initialized: no routine is set and the stack size is the system default.
 * @note Release with zt_thread_destroy().
 */
bool zt_thread_create(ZT_THREAD_HANDLE* thread);

/**
 * @brief Closes the thread handle, frees the object and nulls the pointer. Like CloseHandle, it does NOT stop or wait for the thread.
 *
 * @param[in,out] handle Pointer to the variable holding the object. Set to NULL on success.
 *
 * @return true on success; false if @p handle or *@p handle is NULL, the OS handle could not be closed, or the free failed.
 *
 * @note Allowed in any state: the worker holds no pointer to the object, so a running thread keeps running after destroy.
 * @warning A SUSPENDED thread stays suspended forever once destroyed; resume it first.
 * @warning Do not use the handle after this call, and do not call destroy from two threads on the same object.
 */
bool zt_thread_destroy(ZT_THREAD_HANDLE* thread);

/**
 * @brief Sets the entry point the thread will execute.
 *
 * @param handle  Thread object from zt_thread_create().
 * @param routine Function to run on the new thread. Runs once.
 *
 * @return true if stored; false if @p handle is NULL or the state is not PENDING.
 *
 * @note Passing NULL is accepted, but zt_thread_run() then fails until a real routine is set.
 */
bool zt_thread_set_routine(ZT_THREAD_HANDLE handle, ZT_THREAD_ROUTINE routine);

/**
 * @brief Sets the stack size the thread will be started with.
 *
 * @param handle     Thread object from zt_thread_create().
 * @param stack_size Size in bytes. 0 selects the system default (1 MB unless the executable says otherwise).
 *
 * @return true if stored; false if @p handle is NULL or the state is not PENDING.
 *
 * @note The size is a reservation, not an upfront commit, and is rounded up to the OS granularity (64 KB).
 * @note No minimum is enforced here; a too-small value can make the routine overflow its stack and crash the process.
 */
bool zt_thread_set_stack_size(ZT_THREAD_HANDLE handle, size_t stack_size);

/**
 * @brief Returns the OS thread id.
 *
 * @param handle Thread object from zt_thread_create().
 *
 * @return The id after a successful zt_thread_run(); 0 if @p handle is NULL or the thread has not been started.
 *
 * @note The id is an OS value and may be reused by the system after the thread exits.
 */
unsigned long zt_thread_get_id(ZT_THREAD_HANDLE handle);

/**
 * @brief Returns the current lifecycle state.
 *
 * @param handle Thread object from zt_thread_create(). Must not be NULL.
 *
 * @return One of ZT_THREAD_STATE.
 *
 * @note The result is a snapshot; the thread can exit or change state right after the call returns.
 * @note TERMINATED stays TERMINATED; it never becomes COMPLETED.
 * @warning Do not call after zt_thread_destroy().
 */
ZT_THREAD_STATE zt_thread_get_state(ZT_THREAD_HANDLE handle);

/**
 * @brief Starts the thread: PENDING -> RUNNING.
 *
 * @param handle Thread object with a routine set.
 *
 * @return true if the thread was started; false if @p handle is NULL, no routine is set, the state is not PENDING, or allocation or the OS failed.
 *
 * @note Concurrent calls on the same object are safe: exactly one succeeds.
 * @note On failure after the state was claimed, it returns to PENDING and the call may be retried.
 * @note A thread runs its routine once; it cannot be restarted after leaving PENDING.
 */
bool zt_thread_run(ZT_THREAD_HANDLE handle, void* parameter);

/**
 * @brief Waits for the thread to exit, up to @p timeout_ms.
 * 
 * @param handle     Started thread (after zt_thread_run()).
 * 
 * @param timeout_ms Wait limit in milliseconds. 0 polls without blocking; 0xFFFFFFFF waits forever.
 * 
 * @return true if the thread has exited (normally or terminated); false on NULL, never started, timeout, or OS failure.
 * 
 * @warning Blocks forever on a SUSPENDED thread unless a timeout is given; resume it first.
 * @warning Never call from the thread being joined (self-wait), and never after zt_thread_destroy().
 */
bool zt_thread_join(ZT_THREAD_HANDLE handle, unsigned long timeout_ms);

/**
 * @brief Forcefully terminates the thread: RUNNING or SUSPENDED -> TERMINATED.
 *
 * @param handle Thread to terminate.
 *
 * @return true if terminated; false if @p handle is NULL, the thread was never started, it has already exited (COMPLETED or TERMINATED), or the OS failed.
 *
 * @note Blocks until the thread is really gone.
 * @warning DESTRUCTIVE AND IRREVERSIBLE. The thread stops at an arbitrary point without cleanup: locks it holds are never released (including the heap lock, which can freeze all allocations), its resources leak, and shared data may be left inconsistent.
 * @warning Not safe to call concurrently on the same thread. Use only when a cooperative stop is impossible.
 */
bool zt_thread_terminate(ZT_THREAD_HANDLE handle);

/**
 * @brief Suspends a running thread: RUNNING -> SUSPENDED.
 *
 * @param handle Thread to suspend.
 *
 * @return true on success; false if @p handle is NULL, the thread was not started, it has already exited, the state is not RUNNING or SUSPENDED, or the OS failed.
 *
 * @note Calls nest: each suspend needs a matching zt_thread_resume().
 * @warning The thread stops at an arbitrary point, possibly holding a lock. If it holds the heap lock, every allocation blocks until it is resumed.
 */
bool zt_thread_suspend(ZT_THREAD_HANDLE handle);


/**
 * @brief Undoes one suspend: SUSPENDED -> RUNNING once the last suspend is undone.
 *
 * @param handle Thread to resume.
 *
 * @return true on success; false if @p handle is NULL, the thread was not started, the state is not SUSPENDED, or the OS failed.
 *
 * @note With nested suspends the state stays SUSPENDED until the last matching resume.
 */
bool zt_thread_resume(ZT_THREAD_HANDLE handle);

#endif /* ZT_THREAD_H */
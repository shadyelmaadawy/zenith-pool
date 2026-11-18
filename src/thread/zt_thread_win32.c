#include <Windows.h>

#include "memory/zt_memory.h"
#include "thread/zt_thread.h"

#include "synchronization/zt_atomic.h"

/*
 * @brief Private hand-off from zt_thread_run() to the worker thread.
 *
 * Heap-allocated by zt_thread_run(). Once CreateThread succeeds, the worker
 * owns it and frees it itself, before running the user routine. If
 * CreateThread fails, zt_thread_run() frees it instead.
 *
 * The ZTThread object is never shared with the worker: everything the
 * worker needs is copied into this struct.
 */
typedef struct ZTThreadContext {
	// User parameter to pass on the new thread.
	void*			  parameter;
	/// User routine to execute on the new thread.
	ZT_THREAD_ROUTINE routine;

} ZTThreadContext, * PZTThreadContext;

/*
 * @brief Worker thread object: one OS thread running one routine, once.
 *
 * Configure it after zt_thread_create() and before zt_thread_run(), using
 * zt_thread_set_routine() and (optionally) zt_thread_set_stack_size().
 * Everything else is owned by the library.
 *
 * Lifecycle:
 *   PENDING -> RUNNING -> (exits) COMPLETED
 *   RUNNING <-> SUSPENDED          via suspend/resume
 *   RUNNING/SUSPENDED -> TERMINATED via terminate
 *
 * The object is separate from the running thread: the worker never touches
 * it, so zt_thread_destroy() does not wait for the worker.
 */
typedef struct ZTThread {

	/// OS thread id. Valid after zt_thread_run().
	unsigned long	  id;

	/// OS thread handle. Valid after zt_thread_run().
	void*			  handle;

	/// Entry point. Set while PENDING.
	ZT_THREAD_ROUTINE routine;

	/// Raw state. Library-owned; read it with zt_thread_get_state().
	volatile long     state;

	/// Stack size in bytes (reserve). 0 = system default. Set while PENDING.
	size_t            stack_size;

} ZTThread, * PZTThread;

/// Thread entry, copies the context, frees it, runs the routine. Never touches the ZTThread.
static DWORD WINAPI zt_thread_routine(LPVOID lpThreadParameter);

/// True once the OS thread has exited (handle signaled), Needs a valid handle.
static bool zt_thread_has_exited(PZTThread handle); 

/** @brief Allocates a new thread object in the PENDING state; *handle must be NULL on entry. */
bool zt_thread_create(PZTThread* handle) {

	if (handle == NULL || *handle != NULL)
		return false;

	bool create_state     = false;
	PZTThread temp_handle = NULL;

	if (!zt_allocate(&temp_handle, sizeof(ZTThread)))
		goto cleanup;

	zt_memset(temp_handle, 0, sizeof(ZTThread));
	temp_handle->state = ZT_THREAD_PENDING;

	*handle = temp_handle;
	create_state = true;

cleanup:
	if (!create_state && temp_handle != NULL)
		zt_deallocate(&temp_handle);

	return create_state;

}

/** @brief Closes the handle and frees the object; does NOT stop or wait for the thread. */
bool zt_thread_destroy(PZTThread* handle) {

	if (handle == NULL || *handle == NULL)
		return false;

	PZTThread dereferenced_handle = *handle;
	bool destroy_state	         = false;
	long current_state			 = 0;
	
	zt_atomic_read(&dereferenced_handle->state, &current_state);
	if (current_state != ZT_THREAD_PENDING) {

		if (!dereferenced_handle->handle)
			return false;

		if (!CloseHandle(dereferenced_handle->handle))
			goto cleanup;

		dereferenced_handle->handle = NULL;

	}
	if (!zt_deallocate(handle))
		goto cleanup;

	destroy_state = true;
cleanup:

	return destroy_state;

}

/** @brief Sets the entry point the thread runs; fails unless the state is PENDING. */
bool zt_thread_set_routine(ZT_THREAD_HANDLE handle, ZT_THREAD_ROUTINE routine) {

	if (handle == NULL)
		return false;
	
	bool set_routine_state = false;
	long current_state     = 0;
	
	zt_atomic_read(&handle->state, &current_state);
	if (current_state != ZT_THREAD_PENDING)
		goto cleanup;


	handle->routine	  = routine;
	set_routine_state = true;

cleanup:

	return set_routine_state;

}

/** @brief Sets the stack reserve in bytes (0 = system default); fails unless the state is PENDING. */
bool zt_thread_set_stack_size(ZT_THREAD_HANDLE handle, size_t stack_size) {

	if (handle == NULL)
		return false;

	bool set_stack_size_state = false;
	long current_state		  = 0;

	zt_atomic_read(&handle->state, &current_state);
	if (current_state != ZT_THREAD_PENDING)
		goto cleanup;


	handle->stack_size = stack_size;
	set_stack_size_state = true;

cleanup:

	return set_stack_size_state;

}

/** @brief Returns the OS thread id, or 0 if the thread was never started. */
unsigned long zt_thread_get_id(ZT_THREAD_HANDLE handle) {

	if (handle == NULL)
		return 0;

	long current_state = 0;

	zt_atomic_read(&handle->state, &current_state);
	if (current_state == ZT_THREAD_PENDING)
		return 0;


	return handle->id;

}

/** @brief Returns the current state; COMPLETED is derived from the OS handle, not stored. */
ZT_THREAD_STATE zt_thread_get_state(ZT_THREAD_HANDLE handle) {

	if (handle == NULL)
		return false;

	long current_state = 0;
	zt_atomic_read(&handle->state, &current_state);

	if (current_state == ZT_THREAD_RUNNING && WaitForSingleObject(handle->handle, 0) == WAIT_OBJECT_0)
		return ZT_THREAD_COMPLETED;

	return (ZT_THREAD_STATE)current_state;

}
/** @brief Starts the thread (PENDING -> RUNNING), Fails if not PENDING. */
bool zt_thread_run(PZTThread handle, void* parameter) {
	
	if (!handle)
		return false;

	bool			 run_state	   = false;
	bool             claimed	   = false;

	HANDLE			 thread_handle = NULL;
	DWORD		     thread_id	   = 0;
	PZTThreadContext context	   = NULL;
	
	if (handle->routine == NULL)
		goto cleanup;

	if (InterlockedCompareExchange(&handle->state,
		ZT_THREAD_RUNNING,
		ZT_THREAD_PENDING) != ZT_THREAD_PENDING)
		goto cleanup;
	claimed = true;

	if (!zt_allocate(&context, sizeof(ZTThreadContext)))
		goto cleanup;

	context->routine = handle->routine;
	if (parameter != NULL)
		context->parameter = parameter;

	if ((thread_handle = CreateThread(
		NULL,
		handle->stack_size,
		zt_thread_routine,
		context,
		STACK_SIZE_PARAM_IS_A_RESERVATION,
		&thread_id
	)) == NULL) {
		goto cleanup;
	}

	handle->handle = thread_handle;
	handle->id	   = thread_id;

	run_state = true;

cleanup:

	if (!run_state) {
		
		if (context) 
			zt_deallocate(&context);
		
		/* Roll back only if we were the one who claimed the state. */
		if (claimed)
			InterlockedExchange(&handle->state, ZT_THREAD_PENDING);

	}

	return run_state;

}

#include "terminal/zt_terminal.h"
/** @brief Waits for the thread to exit, up to timeout_ms; false on timeout or if never started. */
bool zt_thread_join(PZTThread handle, unsigned long timeout_ms) {

	if (handle == NULL || handle->handle == NULL)
		return false;

	return WaitForSingleObject(handle->handle, timeout_ms) == WAIT_OBJECT_0;

}

/** @brief DESTRUCTIVE, kills the thread without cleanup; held locks are never released. */
bool zt_thread_terminate(PZTThread handle) {

	bool terminate_state = false;
	LONG previous		 = ZT_THREAD_RUNNING;

	if (handle == NULL || handle->handle == NULL)
		return false;

	if (zt_thread_has_exited(handle))
		return false;

	previous = InterlockedExchange(&handle->state, ZT_THREAD_TERMINATED);
	
	if (!TerminateThread(handle->handle, 0)) {
		InterlockedExchange(&handle->state, previous);
		goto cleanup;
	}

	WaitForSingleObject(handle->handle, INFINITE);
	terminate_state = true;

cleanup:
	return terminate_state;
}

/** @brief Pauses the thread at an arbitrary point; DANGEROUS if it holds a lock (even the heap's). Calls nest. */
bool zt_thread_suspend(PZTThread handle) {

	if (handle == NULL || handle->handle == NULL)
		return false;

	if (zt_thread_has_exited(handle))
		return false;

	long current_state = 0;
	zt_atomic_write(&handle->state, current_state);

	bool is_valid_state = current_state == ZT_THREAD_SUSPENDED || current_state == ZT_THREAD_RUNNING;
	if (!is_valid_state)
		return false;

	bool suspend_state		    = false;
	unsigned long suspend_count = 0;

	suspend_count = SuspendThread(handle->handle);

	if (suspend_count == (DWORD)-1)
		goto cleanup;


	suspend_state = true;
	InterlockedCompareExchange(&handle->state, ZT_THREAD_SUSPENDED, ZT_THREAD_RUNNING);
	

cleanup:
	return suspend_state;

}

/** @brief Undoes one suspend; state returns to RUNNING only when the suspend count reaches 0. */
bool zt_thread_resume(PZTThread handle) {

	if (!handle)
		return false;

	if (!handle->handle)
		return false;

	long current_state = 0;
	zt_atomic_write(&handle->state, current_state);

	bool is_valid_state = current_state == ZT_THREAD_SUSPENDED;
	if (!is_valid_state)
		return false;

	bool resume_state		   = false;
	unsigned long resume_count = 0;

	resume_count = ResumeThread(handle->handle);

	if (resume_count == (DWORD)-1)
		goto cleanup;

	/*
	 * ResumeThread returns the PREVIOUS suspend count:
	 *   1   -> count is now 0, the thread really runs again: SUSPENDED -> RUNNING.
	 *   > 1 -> still suspended by an earlier nested suspend: keep SUSPENDED.
	 */
	if(resume_count == 1) 
		InterlockedCompareExchange(&handle->state, ZT_THREAD_RUNNING, ZT_THREAD_SUSPENDED);

	resume_state = true;

cleanup:
	return resume_state;

}

/** @brief True once the OS thread has exited(handle signaled).Needs a valid handle. */
static bool zt_thread_has_exited(PZTThread handle)
{
	return WaitForSingleObject(handle->handle, 0) == WAIT_OBJECT_0;
}

static DWORD WINAPI zt_thread_routine(LPVOID lpThreadParameter) {

	PZTThreadContext context = (PZTThreadContext)lpThreadParameter;
	if (!context)
		return ERROR_INVALID_PARAMETER;

	ZT_THREAD_ROUTINE routine   = context->routine;
	void*             parameter = context->parameter;
	zt_deallocate(&context);

	if (!routine)
		return ERROR_INVALID_ADDRESS;

	routine(parameter);

	return 0;

}
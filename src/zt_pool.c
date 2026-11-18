#include "zt_pool.h"
#include "cpu/zt_cpu.h"

#include "memory/zt_memory.h"
#include "thread/zt_thread.h"
#include "random/zt_random.h"

#include "containers/zt_list.h"
#include "containers/zt_queue.h"

#include "synchronization/zt_lock.h"
#include "synchronization/zt_atomic.h"
#include "synchronization/zt_condition.h"

#include "terminal/zt_terminal.h"

/* How long an EXTRA worker (above the minimum) waits idle before leaving the pool. */
#define ZT_POOL_DEFAULT_KEEP_ALIVE_MS (3 * 1000)

struct ZTPool {

	/// 1 while the pool accepts jobs. Written under the lock (and atomically).
	volatile long		  is_running;

	PZTLock		     	  lock;
	PZTList				  workers;           /* ZT_THREAD_HANDLE values, one per live worker */

	PZTQueue			  jobs;              /* ZTJob values, FIFO */
	PZTCondation		  jobs_condition;

	long volatile thread_stack_size;
	long volatile keep_alive_timeout;

	long volatile idle_workers;              /* counted from the moment a worker is created */
	long volatile active_workers;            /* workers running a job right now */
	long volatile minimum_workers;
	long volatile maximum_workers;

	long volatile next_job_id;

};

/// A queued job. Stored BY VALUE in the queue, so a push needs no allocation of its own.
typedef struct ZTJob {

	long id;                                 /* unique per pool */
	void* parameter;
	ZT_POOL_ROUTINE routine;

} ZTJob;

/// Hand-off to a new worker. The worker owns it and frees it as its first action.
typedef struct ZTPoolContext {

	PZTPool			 pool;
	ZT_THREAD_HANDLE handle;

} ZTPoolContext, * PZTPoolContext;


void zt_pool_routine(void* parameter);

bool zt_pool_create_thread(PZTPool pool, long stack_size);
void zt_pool_release_internals(PZTPool pool);

/** @brief Creates a stopped pool with default settings; *pool must be NULL on entry. */
bool zt_pool_create(PZTPool* pool) {
	if (pool == NULL || *pool != NULL)
		return false;

	PZTPool temp_pool = NULL;
	bool create_state = false;
	if (!zt_allocate(&temp_pool, sizeof(ZTPool)))
		goto cleanup;

	zt_memset(temp_pool, 0, sizeof(ZTPool));
	if (!zt_list_init(&temp_pool->workers, sizeof(ZT_THREAD_HANDLE)))
		goto cleanup;
	if (!zt_queue_init(&temp_pool->jobs, sizeof(ZTJob)))
		goto cleanup;
	if (!zt_lock_init(&temp_pool->lock))
		goto cleanup;
	if (!zt_condation_init(&temp_pool->jobs_condition))
		goto cleanup;
	short cpu_processors = zt_cpu_processors();

	temp_pool->keep_alive_timeout = ZT_POOL_DEFAULT_KEEP_ALIVE_MS;

	temp_pool->minimum_workers	  = cpu_processors > 1 ? cpu_processors / 2 : 1; 
	temp_pool->maximum_workers	  = cpu_processors;

	*pool = temp_pool;
	
	create_state = true;

cleanup:
	if (!create_state && temp_pool) {
		zt_pool_release_internals(temp_pool);     /* a failure half-way must not leak what was built */
		zt_deallocate(&temp_pool);
	}

	return create_state;

}

/*
 * teardown is a no-op when the pool is not running, so destroy is correct for a pool that was never
 * started, one that failed to start, and one that was already torn down. The internals live until here,
 * which is what lets a late zt_pool_push() safely return false after teardown.
 */
 /** @brief Stops the pool if needed, frees everything and nulls the pointer. */
bool zt_pool_destroy(PZTPool* pool) {

	if (pool == NULL || *pool == NULL)
		return false;

	zt_pool_teardown(*pool);
	zt_pool_release_internals(*pool);
	return zt_deallocate(pool);
}

/** @brief Stores the stack size (bytes, 0 = system default) for workers created from now on. */
bool zt_pool_set_stack_size(PZTPool pool, long value) {

	if (!pool || pool == NULL)
		return false;
	if (value < 0)
		return false;

	zt_atomic_write(&pool->thread_stack_size, value);

	return true;
}

/** @brief Stores the idle time in milliseconds after which an extra worker leaves (0 = never). */
bool zt_pool_set_keep_alive(PZTPool pool, long value) {

	if (!pool || pool == NULL)
		return false;
	if (value < 0)
		return false;

	zt_atomic_write(&pool->keep_alive_timeout, value);

	return true;

}

/** @brief Stores the minimum worker count; it must be positive and not above the current maximum. */
bool zt_pool_set_minimum_workers(PZTPool pool, long value) {

	if (!pool || pool == NULL)
		return false;

	if (value <= 0)
		return false;
	long maximum_workers = 0;
	zt_atomic_read(&pool->maximum_workers, &maximum_workers);
	if (value > pool->maximum_workers)
		return false;

	zt_atomic_write(&pool->minimum_workers, value);

	return true;

}

/** @brief Stores the maximum worker count; it is validated against the minimum at bootstrap. */
bool zt_pool_set_maximum_workers(PZTPool pool, long value) {

	if (!pool)
		return false;
	if (value <= 0)
		return false;

	zt_atomic_write(&pool->maximum_workers, value);

	return true;

}

/*
 * The "already running" check is done UNDER the lock, so two concurrent bootstraps cannot both
 * create the minimum set of workers. On any failure the cleanup calls teardown, which stops and joins
 * whatever was started (and does nothing if nothing was).
 */
 /** @brief Starts the minimum number of workers; true also if the pool was already running. */
bool zt_pool_bootstrap(PZTPool pool) {

	if (!pool || pool == NULL)
		return false;

	zt_lock_acquire(pool->lock);
	long is_running = 0;
	zt_atomic_read(&pool->is_running, &is_running);

	if (is_running == 1)  // already running
	{
		zt_lock_release(pool->lock);
		return true;
	}

	bool bootstrap_state   = false;
	long thread_stack_size = 0; 
	long minimum_workers   = 0;
	long maximum_workers   = 0;

	zt_atomic_read(&pool->thread_stack_size, &thread_stack_size);
	zt_atomic_read(&pool->minimum_workers, &minimum_workers);
	zt_atomic_read(&pool->maximum_workers, &maximum_workers);

	if (minimum_workers > maximum_workers || minimum_workers == 0 || maximum_workers == 0)
		goto cleanup;

	/* Set BEFORE creating workers, otherwise the first worker sees "stopped, queue empty" and exits. */
	zt_atomic_write(&pool->is_running, 1);
	for (int i = 0; i < minimum_workers; i++) {
		
		if (!zt_pool_create_thread(pool, thread_stack_size))
			goto cleanup;

	}

	bootstrap_state = true;

cleanup:
	zt_lock_release(pool->lock);
	if (!bootstrap_state)
		zt_pool_teardown(pool);

	return bootstrap_state;

}

/*
 * Order matters:
 *  1. Flip is_running and wake everybody UNDER the lock. A worker that already checked the flag cannot
 *     miss the wake-up, because it still holds the lock until it sleeps.
 *  2. Join WITHOUT the lock: a worker needs the lock to leave its wait, so holding it here deadlocks.
 *     Once is_running is 0 no worker removes itself any more, so pop_front sees every remaining worker.
 *  3. The pool object (lock, condition, queue, list) is NOT freed here. That is zt_pool_destroy's job,
 *     so a push racing with teardown still finds a valid lock and gets false.
 */
 /** @brief Graceful stop: refuses new jobs, drains the queue, joins every worker. */
bool zt_pool_teardown(PZTPool pool) {

	if (pool == NULL)
		return false;

	long is_running		= 0;
	bool teardown_state = false;
	/* Flip the flag UNDER the lock, a worker that already checked it cannot miss the wake-up. */
	zt_lock_acquire(pool->lock);

	zt_atomic_read(&pool->is_running, &is_running);
	if (is_running == 0) {
		zt_lock_release(pool->lock);
		goto cleanup;
	}

	zt_atomic_write(&pool->is_running, 0);
	zt_condation_wake_all(pool->jobs_condition);

	zt_lock_release(pool->lock);

	for (;;) {

		ZT_THREAD_HANDLE handle = NULL;
		bool             popped = false;

		zt_lock_acquire(pool->lock);
		popped = zt_list_pop_front(pool->workers, &handle);
		zt_lock_release(pool->lock);

		if (!popped)
			break;

		if (!zt_thread_join(handle, 0xFFFFFFFF)) {
#ifdef _DEBUG
			write_to_std("[zenith-pool] tid: %lu, could not join\r\n", zt_thread_get_id(handle));
#endif
		}

		if (!zt_thread_destroy(&handle)) {
#ifdef _DEBUG
			write_to_std("[zenith-pool] could not destroy a worker thread object\r\n");
#endif
		}

	}

	zt_lock_acquire(pool->lock);

	/* Workers drain the queue before leaving, so this only discards jobs when there was no worker at all. */
	while (!zt_queue_is_empty(pool->jobs)) {

		ZTJob job_context;
		zt_memset(&job_context, 0, sizeof(ZTJob));
		if (!zt_queue_dequeue(pool->jobs, &job_context)) {
			continue;
		}

	}

	zt_lock_release(pool->lock);

	teardown_state = true;

cleanup:
	
	return teardown_state;

}

/*
 * The job is stored by value in the queue (no per-job allocation here). 
 */
 /** @brief Queues a job; false if the pool is not running, routine is NULL or allocation failed. */
bool zt_pool_push(PZTPool pool, ZT_POOL_ROUTINE routine, void* parameter) {

	if (!pool || pool == NULL || routine == NULL)
		return false;

	zt_lock_acquire(pool->lock);

	bool push_state		   = false;
	long is_running	       = 0;

	long idle_workers      = 0;
	long total_workers	   = 0;
	long active_workers	   = 0;
	long maximum_workers   = 0;

	long stack_size		   = 0;
	long pending_jobs	   = 0;
	ZTJob context;

	zt_atomic_read(&pool->is_running, &is_running);
	if (is_running == 0)
		goto cleanup;
	zt_memset(&context, 0, sizeof(ZTJob));

	zt_atomic_read(&pool->active_workers, &active_workers);
	zt_atomic_read(&pool->idle_workers, &idle_workers);
	zt_atomic_read(&pool->maximum_workers, &maximum_workers);
	zt_atomic_read(&pool->thread_stack_size, &stack_size);
	total_workers = zt_list_count(pool->workers);
		

	if (!fill(&context.id, sizeof(long)))
		goto cleanup;

	context.routine = routine;
	context.parameter = parameter;

	if (!zt_queue_enqueue(pool->jobs, &context))
		goto cleanup;

	pending_jobs = zt_queue_count(pool->jobs);

	if (idle_workers < pending_jobs && total_workers < maximum_workers)
		zt_pool_create_thread(pool, stack_size);     /* best effort, the job is queued either way */

	zt_condation_wake(pool->jobs_condition);

	push_state = true;

cleanup:
	zt_lock_release(pool->lock);
	
	return push_state;

}

/*
 * Worker loop. The pool lock is held the whole time except while a job runs or while sleeping.
 *
 * Accounting contract (set up by zt_pool_create_thread, NOT here):
 *   - a new worker is already counted in idle_workers before it starts,
 *     so this routine does not do idle++ on entry.
 *
 * Exit paths:
 *   - shutdown : is_running == 0 and the queue is empty. The worker stays in pool->workers
 *                (the teardown joins and destroys it) and only uncounts itself.
 *   - timeout  : an extra worker. It removes ITSELF from pool->workers in the same locked step
 *                that checks "total > minimum", so the minimum can never be violated by several
 *                workers deciding at once. It destroys its own thread object after the lock is
 *                released.
 * After the final lock release the worker touches nothing that belongs to the pool.
 *
 * Every worker wakes up once per keep-alive period; only an extra worker acts on the timeout.
 */
void zt_pool_routine(void* parameter) {

	PZTPoolContext pool_context = (PZTPoolContext)parameter;
	if (pool_context == NULL)
		return;

	PZTPool          pool		   = pool_context->pool;
	ZT_THREAD_HANDLE self		   = pool_context->handle;
#ifdef _DEBUG
	unsigned long	 self_id	   = zt_thread_get_id(self);
#endif
	long minimum_workers	       = 0;
	long worker_count			   = 0;
	long keep_alive				   = 0;
	unsigned long wait_ms		   = 0;

	long is_running				   = 0;
	bool leave_pool				   = false;


	zt_deallocate(&pool_context);	/* everything needed was copied, free it on every path */
	if (pool == NULL)
		return;

	zt_lock_acquire(pool->lock);

	for (;;) {


		/* sleep while there is nothing to do AND the pool is alive */

		for (;;) {

			zt_atomic_read(&pool->is_running, &is_running);
			if (!zt_queue_is_empty(pool->jobs) || is_running == 0)
				break;

			zt_atomic_read(&pool->minimum_workers, &minimum_workers);
			zt_atomic_read(&pool->keep_alive_timeout, &keep_alive);
			wait_ms = keep_alive > 0 ? (unsigned long)keep_alive : 0xFFFFFFFF;

			/* only an extra worker may time out; the minimum workers wait forever */
			worker_count = zt_list_count(pool->workers);

			if (zt_condation_sleep(pool->jobs_condition, pool->lock, wait_ms))
				continue;	/* woken (or spurious): re-check the predicate */
			/* timed out and the lock is held again, decide and remove in ONE locked step */

			zt_atomic_read(&pool->is_running, &is_running);
			zt_atomic_read(&pool->minimum_workers, &minimum_workers);
			worker_count = zt_list_count(pool->workers);

			if (is_running && // Extra worker, destroy
				zt_queue_is_empty(pool->jobs) &&
				worker_count > minimum_workers) {

				zt_list_remove(pool->workers, &self);
				zt_thread_destroy(&self);

				leave_pool = true;

				break;
			}
		}

		if (leave_pool)
			break;

		if (zt_queue_is_empty(pool->jobs))  /* stopped and nothing left */
			break;

		ZTJob context;
		ZT_POOL_ROUTINE routine = NULL;
		void* job_parameter		= NULL;

		zt_memset(&context, 0, sizeof(ZTJob));
		if (!zt_queue_dequeue(pool->jobs, &context))
			continue;

		routine		  = context.routine;
		job_parameter = context.parameter;

		if (routine == NULL) 
			continue;

		zt_atomic_decrement(&pool->idle_workers);
		zt_atomic_increment(&pool->active_workers);
		zt_lock_release(pool->lock);

		routine(job_parameter);

		zt_lock_acquire(pool->lock);
		zt_atomic_decrement(&pool->active_workers);
		zt_atomic_increment(&pool->idle_workers);
	}


	zt_atomic_decrement(&pool->idle_workers); /* no longer counted as idle */
	zt_lock_release(pool->lock);
	
#ifdef _DEBUG
	write_to_std("[zenith-pool] worker [%ld] is leaving the pool, Goodbye!\r\n", self_id);
#endif

}

/*
 * Called with the pool lock held. Order: count idle, create, register in the list, THEN run. A worker
 * therefore never runs while missing from the list, and a failed run is undone with a remove.
 * On failure after the worker started nothing can happen here, because run is the last step.
 */
bool zt_pool_create_thread(PZTPool pool, long stack_size) {

	bool			 create_state = false;
	ZT_THREAD_HANDLE handle		  = NULL;
	PZTPoolContext   context	  = NULL;

	/* counted idle from the moment it exists */
	zt_atomic_increment(&pool->idle_workers);
	
	if (zt_thread_create(&handle) &&
		zt_thread_set_routine(handle, zt_pool_routine) &&
		zt_thread_set_stack_size(handle, stack_size))
	{
		if (!zt_allocate(&context, sizeof(ZTPoolContext)))
			goto cleanup;

		zt_memset(context, 0, sizeof(ZTPoolContext));
		context->pool   = pool;
		context->handle = handle;

		if (!zt_list_push_back(pool->workers, &handle))
			goto cleanup;

		if (!zt_thread_run(handle, context)) {
			zt_list_remove(pool->workers, &handle);
			goto cleanup;
		}

		create_state = true;
	}

cleanup:

	if (!create_state) {

		zt_atomic_decrement(&pool->idle_workers);   /* it never started, take it back */
		if (handle != NULL)
			zt_thread_destroy(&handle);
		if (context != NULL)
			zt_deallocate(&context);

	}

	return create_state;

}

/*
 * Frees the lock, condition, queue and list in the reverse order of creation. Safe on a pool that
 * is only partly built (every member starts NULL because the pool is zeroed).
 */
void zt_pool_release_internals(PZTPool pool) {

	if (!zt_condation_deinit(&pool->jobs_condition)) {
#ifdef _DEBUG
		write_to_std("[zenith-pool] could not deinit jobs condition variable\r\n");
#endif
	}


	if (!zt_lock_deinit(&pool->lock)) {
#ifdef _DEBUG
		write_to_std("[zenith-pool] could not deinit pool lock\r\n");
#endif
	}
	if (!zt_queue_deinit(&pool->jobs)) {
#ifdef _DEBUG
		write_to_std("[zenith-pool] could not deinit jobs queue\r\n");
#endif
	}
	if (!zt_list_deinit(&pool->workers)) {
#ifdef _DEBUG
		write_to_std("[zenith-pool] could not deinit workers list\r\n");
#endif
	}

}

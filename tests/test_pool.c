/*
 * Aggressive tests for zt_pool, using ONLY the public API (zt_pool.h).
 *
 * CONTRACT USED (matches the current zt_pool.h / zt_pool.c):
 *   - The setters validate: min > 0 and <= the CURRENT max (so the max is set first), max > 0,
 *     stack size and keep-alive >= 0. set_keep_alive takes MILLISECONDS.
 *   - zt_pool_teardown is graceful: queued jobs still run, then workers exit. It returns false if the
 *     pool was never started or is already stopped. The pool object stays valid until destroy,
 *     so a late push returns false.
 *   - zt_pool_bootstrap on a running pool returns true and changes nothing.
 *   - zt_pool_destroy stops a running pool itself (same as teardown) and frees everything.
 *   - zt_pool_push refuses a NULL routine.
 *
 * Observability without internals: counters updated by the jobs themselves, plus the process
 * thread count (Toolhelp) and handle count, which expose worker growth, shrink and leaks.
 *
 * A watchdog aborts the process when a test hangs (deadlock / lost wake-up) instead of freezing.
 */

#include <windows.h>
#include <tlhelp32.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "zt_pool.h"

 /* ====================== tiny test framework + watchdog ====================== */

static volatile LONG g_checks = 0;
static volatile LONG g_failed = 0;

#define CHECK(cond)                                                        \
	do {                                                                   \
		InterlockedIncrement(&g_checks);                                   \
		if (!(cond)) {                                                     \
			InterlockedIncrement(&g_failed);                               \
			printf("  FAIL  %s:%d  %s\n", __FILE__, __LINE__, #cond);      \
		}                                                                  \
	} while (0)

#define CHECK_EQ(actual, expected)                                         \
	do {                                                                   \
		long long a_ = (long long)(actual), e_ = (long long)(expected);    \
		InterlockedIncrement(&g_checks);                                   \
		if (a_ != e_) {                                                    \
			InterlockedIncrement(&g_failed);                               \
			printf("  FAIL  %s:%d  %s = %lld (expected %lld)\n",           \
			       __FILE__, __LINE__, #actual, a_, e_);                   \
		}                                                                  \
	} while (0)

static HANDLE      g_wd_event = NULL;
static HANDLE      g_wd_thread = NULL;
static const char* g_wd_name = "";
static DWORD       g_wd_ms = 0;

static DWORD WINAPI watchdog_proc(LPVOID unused) {
	(void)unused;
	if (WaitForSingleObject(g_wd_event, g_wd_ms) == WAIT_TIMEOUT) {
		printf("\n  WATCHDOG: '%s' did not finish in %lu ms (deadlock or lost wake-up). Aborting.\n",
			g_wd_name, (unsigned long)g_wd_ms);
		fflush(stdout);
		ExitProcess(2);
	}
	return 0;
}

static void watchdog_arm(const char* name, DWORD ms) {
	g_wd_name = name;
	g_wd_ms = ms;
	g_wd_event = CreateEventW(NULL, TRUE, FALSE, NULL);
	g_wd_thread = CreateThread(NULL, 0, watchdog_proc, NULL, 0, NULL);
}

static void watchdog_disarm(void) {
	SetEvent(g_wd_event);
	WaitForSingleObject(g_wd_thread, INFINITE);
	CloseHandle(g_wd_thread);
	CloseHandle(g_wd_event);
}

#define RUN_TEST(fn)                                                       \
	do {                                                                   \
		LONG before_ = g_failed;                                           \
		ULONGLONG t0_ = GetTickCount64();                                  \
		printf("[ RUN  ] %s\n", #fn);                                      \
		watchdog_arm(#fn, 180000);                                         \
		fn();                                                              \
		watchdog_disarm();                                                 \
		printf("[ %s ] %s (%llu ms)\n",                                    \
		       g_failed == before_ ? " OK " : "FAIL", #fn,                 \
		       (unsigned long long)(GetTickCount64() - t0_));              \
	} while (0)

/* ====================== counters, jobs, helpers ====================== */

static volatile LONG g_executed = 0;     /* jobs that finished */
static volatile LONG g_running = 0;     /* jobs running right now */
static volatile LONG g_peak = 0;     /* highest value g_running ever had */
static volatile LONG g_rejected = 0;     /* pushes refused by the pool */

static LONG get(volatile LONG* v) {
	return InterlockedCompareExchange(v, 0, 0);
}

static void reset_counters(void) {
	g_executed = 0;
	g_running = 0;
	g_peak = 0;
	g_rejected = 0;
}

static bool wait_at_least(volatile LONG* v, LONG target, DWORD timeout_ms) {
	ULONGLONG start = GetTickCount64();
	while (get(v) < target) {
		if (GetTickCount64() - start > timeout_ms)
			return false;
		Sleep(1);
	}
	return true;
}

static void job_count(void* p) {
	(void)p;
	InterlockedIncrement(&g_executed);
}

/* parameter = pointer to a LONG slot that this job must bump exactly once */
static void job_slot(void* p) {
	InterlockedIncrement((volatile LONG*)p);
	InterlockedIncrement(&g_executed);
}

/* parameter = sleep time in ms; also tracks the peak concurrency */
static void job_track(void* p) {
	LONG now = InterlockedIncrement(&g_running);
	LONG peak;
	do {
		peak = g_peak;
		if (now <= peak)
			break;
	} while (InterlockedCompareExchange(&g_peak, now, peak) != peak);

	Sleep((DWORD)(uintptr_t)p);

	InterlockedDecrement(&g_running);
	InterlockedIncrement(&g_executed);
}

static void job_sleep(void* p) {
	Sleep((DWORD)(uintptr_t)p);
	InterlockedIncrement(&g_executed);
}

/* process-wide thread count: shows worker growth, shrink and leaks */
static long process_thread_count(void) {
	DWORD pid = GetCurrentProcessId();
	HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
	long n = 0;

	if (snap == INVALID_HANDLE_VALUE)
		return -1;

	THREADENTRY32 te;
	te.dwSize = sizeof(te);
	if (Thread32First(snap, &te)) {
		do {
			if (te.th32OwnerProcessID == pid)
				n++;
		} while (Thread32Next(snap, &te));
	}
	CloseHandle(snap);
	return n;
}

static long process_handle_count(void) {
	DWORD n = 0;
	GetProcessHandleCount(GetCurrentProcess(), &n);
	return (long)n;
}

/*
 * Creates, configures and starts a pool. keep_alive_ms == 0 keeps the pool's default.
 * The MAXIMUM is set first: the minimum setter rejects a value above the current maximum, and every
 * setter result is checked, so a pool that is not configured as asked is never silently used.
 */
static PZTPool make_pool(long minimum, long maximum, long keep_alive_ms) {
	PZTPool p = NULL;

	if (!zt_pool_create(&p))
		return NULL;

	if (!zt_pool_set_maximum_workers(p, maximum) ||
		!zt_pool_set_minimum_workers(p, minimum) ||
		(keep_alive_ms > 0 && !zt_pool_set_keep_alive(p, keep_alive_ms)) ||
		!zt_pool_bootstrap(p)) {

		zt_pool_destroy(&p);
		return NULL;
	}

	return p;
}

static void finish_pool(PZTPool* p) {
	CHECK(zt_pool_teardown(*p));
	CHECK(zt_pool_destroy(p));
	CHECK(*p == NULL);
}

/* ====================== 1. arguments, setters and state machine ====================== */

static void test_null_and_invalid_args(void) {
	PZTPool p = NULL;

	CHECK(!zt_pool_create(NULL));
	CHECK(!zt_pool_destroy(NULL));
	CHECK(!zt_pool_destroy(&p));                    /* *pool == NULL */
	CHECK(!zt_pool_set_stack_size(NULL, 65536));
	CHECK(!zt_pool_set_keep_alive(NULL, 100));
	CHECK(!zt_pool_set_minimum_workers(NULL, 1));
	CHECK(!zt_pool_set_maximum_workers(NULL, 1));
	CHECK(!zt_pool_bootstrap(NULL));
	CHECK(!zt_pool_teardown(NULL));
	CHECK(!zt_pool_push(NULL, job_count, NULL));

	CHECK(zt_pool_create(&p));
	CHECK(p != NULL);
	if (p == NULL)
		return;

	/* the setters validate their input */
	CHECK(!zt_pool_set_stack_size(p, -1));
	CHECK(!zt_pool_set_keep_alive(p, -1));
	CHECK(!zt_pool_set_minimum_workers(p, 0));
	CHECK(!zt_pool_set_maximum_workers(p, 0));
	CHECK(!zt_pool_set_maximum_workers(p, -5));

	CHECK(zt_pool_set_maximum_workers(p, 4));
	CHECK(!zt_pool_set_minimum_workers(p, 5));      /* above the current maximum */
	CHECK(zt_pool_set_minimum_workers(p, 4));       /* equal is fine */

	CHECK(!zt_pool_push(p, job_count, NULL));       /* not running yet */
	CHECK(!zt_pool_teardown(p));                    /* never started */

	/* minimum > maximum can only happen by lowering the maximum afterwards: bootstrap must refuse it */
	CHECK(zt_pool_set_maximum_workers(p, 2));
	CHECK(!zt_pool_bootstrap(p));
	CHECK(!zt_pool_push(p, job_count, NULL));       /* a failed bootstrap leaves the pool stopped */

	CHECK(zt_pool_destroy(&p));
}

static void test_bootstrap_twice_and_teardown_twice(void) {
	reset_counters();
	long baseline = process_thread_count();

	PZTPool p = make_pool(3, 6, 0);
	CHECK(p != NULL);
	if (p == NULL)
		return;

	long after_first = process_thread_count();
	CHECK_EQ(after_first - baseline, 3);

	CHECK(zt_pool_bootstrap(p));                    /* already running: ok and idempotent */
	CHECK_EQ(process_thread_count(), after_first);  /* no extra workers were created */

	CHECK(zt_pool_push(p, job_count, NULL));
	CHECK(wait_at_least(&g_executed, 1, 5000));

	CHECK(zt_pool_teardown(p));
	CHECK(!zt_pool_teardown(p));                    /* already stopped */
	CHECK(!zt_pool_push(p, job_count, NULL));       /* a stopped pool refuses work */
	CHECK(zt_pool_destroy(&p));
}

static void test_push_null_routine(void) {
	reset_counters();
	PZTPool p = make_pool(2, 4, 0);
	CHECK(p != NULL);
	if (p == NULL)
		return;

	CHECK(!zt_pool_push(p, NULL, NULL));            /* refused, not queued as a no-op */
	CHECK(zt_pool_push(p, job_count, NULL));        /* and the pool still works */
	CHECK(wait_at_least(&g_executed, 1, 5000));
	CHECK_EQ(g_executed, 1);

	finish_pool(&p);
}

/* ====================== 2. basic lifecycle and the destroy paths ====================== */

static void test_lifecycle_basic(void) {
	reset_counters();
	long baseline = process_thread_count();

	PZTPool p = make_pool(2, 4, 0);
	CHECK(p != NULL);
	if (p == NULL)
		return;

	CHECK(zt_pool_push(p, job_count, NULL));
	CHECK(wait_at_least(&g_executed, 1, 5000));
	CHECK_EQ(g_executed, 1);

	finish_pool(&p);

	/* every worker thread must be gone after teardown */
	ULONGLONG t0 = GetTickCount64();
	while (process_thread_count() > baseline && GetTickCount64() - t0 < 2000)
		Sleep(10);
	CHECK(process_thread_count() <= baseline + 1);
}

static void test_destroy_paths(void) {
	enum { N = 100 };
	PZTPool p = NULL;

	/* never started: destroy must release everything it built */
	CHECK(zt_pool_create(&p));
	CHECK(zt_pool_destroy(&p));
	CHECK(p == NULL);

	/* running, destroyed WITHOUT an explicit teardown: destroy stops it gracefully */
	reset_counters();
	p = make_pool(2, 4, 0);
	CHECK(p != NULL);
	if (p == NULL)
		return;

	for (int i = 0; i < N; ++i)
		CHECK(zt_pool_push(p, job_sleep, (void*)(uintptr_t)1));

	CHECK(zt_pool_destroy(&p));
	CHECK(p == NULL);
	CHECK_EQ(g_executed, N);                        /* every accepted job ran before destroy returned */
}

/* ====================== 3. exactly-once execution ====================== */

static void test_every_job_runs_exactly_once(void) {
	enum { N = 200000 };

	reset_counters();
	volatile LONG* slots = (volatile LONG*)calloc(N, sizeof(LONG));
	CHECK(slots != NULL);
	if (slots == NULL)
		return;

	PZTPool p = make_pool(4, 16, 0);
	CHECK(p != NULL);
	if (p == NULL) {
		free((void*)slots);
		return;
	}

	long accepted = 0;
	for (int i = 0; i < N; ++i)
		if (zt_pool_push(p, job_slot, (void*)&slots[i]))
			accepted++;
	CHECK_EQ(accepted, N);

	finish_pool(&p);                                /* teardown drains the queue */

	long missing = 0, duplicated = 0;
	for (int i = 0; i < N; ++i) {
		if (slots[i] == 0)
			missing++;
		else if (slots[i] > 1)
			duplicated++;
	}
	CHECK_EQ(missing, 0);                           /* a lost job */
	CHECK_EQ(duplicated, 0);                        /* a job that ran twice */
	CHECK_EQ(g_executed, N);

	free((void*)slots);
}

/* ====================== 4. teardown drains queued work ====================== */

static void test_drain_on_teardown(void) {
	enum { N = 3000 };

	reset_counters();
	PZTPool p = make_pool(2, 8, 0);
	CHECK(p != NULL);
	if (p == NULL)
		return;

	long accepted = 0;
	for (int i = 0; i < N; ++i)
		if (zt_pool_push(p, job_sleep, (void*)(uintptr_t)1))
			accepted++;
	CHECK_EQ(accepted, N);

	finish_pool(&p);                                /* right away, most jobs are still queued */
	CHECK_EQ(g_executed, N);
}

static void test_push_after_teardown_rejected(void) {
	reset_counters();
	PZTPool p = make_pool(2, 4, 0);
	CHECK(p != NULL);
	if (p == NULL)
		return;

	for (int i = 0; i < 10; ++i)
		CHECK(zt_pool_push(p, job_count, NULL));
	CHECK(wait_at_least(&g_executed, 10, 5000));

	CHECK(zt_pool_teardown(p));
	CHECK(!zt_pool_push(p, job_count, NULL));       /* safe on a stopped pool: it only returns false */
	Sleep(50);
	CHECK_EQ(g_executed, 10);                       /* the rejected job never ran */
	CHECK(zt_pool_destroy(&p));
}

/* ====================== 5. concurrency stays inside [min, max] ====================== */

static void test_concurrency_bound(void) {
	enum { MIN = 2, MAX = 8, JOBS = 64 };

	reset_counters();
	PZTPool p = make_pool(MIN, MAX, 0);
	CHECK(p != NULL);
	if (p == NULL)
		return;

	for (int i = 0; i < JOBS; ++i)
		CHECK(zt_pool_push(p, job_track, (void*)(uintptr_t)50));

	CHECK(wait_at_least(&g_executed, JOBS, 30000));
	CHECK_EQ(g_executed, JOBS);
	CHECK(g_peak <= MAX);                           /* never more concurrent jobs than max workers */
	CHECK(g_peak >= MIN);                           /* and the minimum really worked in parallel */
	printf("  peak concurrency: %ld (min %d, max %d)\n", (long)g_peak, MIN, MAX);

	finish_pool(&p);
}

/* ====================== 6. one blocked job must not stall the others ====================== */

static HANDLE g_block_event = NULL;

static void job_block(void* p) {
	(void)p;
	WaitForSingleObject(g_block_event, INFINITE);
	InterlockedIncrement(&g_running);               /* reuse: counts released blockers */
}

static void test_no_head_of_line_blocking(void) {
	reset_counters();
	g_block_event = CreateEventW(NULL, TRUE, FALSE, NULL);

	PZTPool p = make_pool(2, 8, 0);
	CHECK(p != NULL);
	if (p == NULL) {
		CloseHandle(g_block_event);
		return;
	}

	CHECK(zt_pool_push(p, job_block, NULL));        /* occupies one worker until released */
	for (int i = 0; i < 200; ++i)
		CHECK(zt_pool_push(p, job_count, NULL));

	CHECK(wait_at_least(&g_executed, 200, 10000));  /* the quick jobs finish while it is blocked */
	CHECK_EQ(g_executed, 200);

	SetEvent(g_block_event);
	CHECK(wait_at_least(&g_running, 1, 5000));      /* the blocker finishes once released */

	finish_pool(&p);
	CloseHandle(g_block_event);
}

/* ====================== 7. many producers ====================== */

typedef struct Producer {
	PZTPool        pool;
	long           count;
	volatile LONG* accepted;
} Producer;

static DWORD WINAPI producer_fixed(LPVOID p) {
	Producer* pr = (Producer*)p;
	for (long i = 0; i < pr->count; ++i) {
		if (zt_pool_push(pr->pool, job_count, NULL))
			InterlockedIncrement(pr->accepted);
		else
			InterlockedIncrement(&g_rejected);
	}
	return 0;
}

static void test_concurrent_producers(void) {
	enum { PRODUCERS = 16, PER_PRODUCER = 20000 };

	reset_counters();
	volatile LONG accepted = 0;
	Producer prod[PRODUCERS];
	HANDLE threads[PRODUCERS];

	PZTPool p = make_pool(4, 16, 0);
	CHECK(p != NULL);
	if (p == NULL)
		return;

	for (int i = 0; i < PRODUCERS; ++i) {
		prod[i].pool = p;
		prod[i].count = PER_PRODUCER;
		prod[i].accepted = &accepted;
		threads[i] = CreateThread(NULL, 0, producer_fixed, &prod[i], 0, NULL);
	}
	WaitForMultipleObjects(PRODUCERS, threads, TRUE, INFINITE);
	for (int i = 0; i < PRODUCERS; ++i)
		CloseHandle(threads[i]);

	CHECK_EQ(accepted, PRODUCERS * PER_PRODUCER);
	CHECK_EQ(g_rejected, 0);

	finish_pool(&p);
	CHECK_EQ(g_executed, PRODUCERS * PER_PRODUCER);
}

/* ====================== 8. teardown while producers are still pushing ====================== */

static DWORD WINAPI producer_until_rejected(LPVOID p) {
	Producer* pr = (Producer*)p;
	for (long i = 0; i < 5000000; ++i) {
		if (!zt_pool_push(pr->pool, job_count, NULL))
			break;
		InterlockedIncrement(pr->accepted);
		if ((i & 1023) == 0)
			Sleep(1);                               /* keep the queue from growing without bound */
	}
	return 0;
}

static void test_teardown_while_producing(void) {
	enum { PRODUCERS = 8 };

	reset_counters();
	volatile LONG accepted = 0;
	Producer prod[PRODUCERS];
	HANDLE threads[PRODUCERS];

	PZTPool p = make_pool(4, 16, 0);
	CHECK(p != NULL);
	if (p == NULL)
		return;

	for (int i = 0; i < PRODUCERS; ++i) {
		prod[i].pool = p;
		prod[i].count = 0;
		prod[i].accepted = &accepted;
		threads[i] = CreateThread(NULL, 0, producer_until_rejected, &prod[i], 0, NULL);
	}

	Sleep(150);
	CHECK(zt_pool_teardown(p));                     /* races with every producer */

	WaitForMultipleObjects(PRODUCERS, threads, TRUE, INFINITE);
	for (int i = 0; i < PRODUCERS; ++i)
		CloseHandle(threads[i]);

	/* the core invariant: every push that returned true must have run */
	CHECK_EQ(g_executed, accepted);
	printf("  accepted and executed: %ld\n", (long)accepted);

	CHECK(zt_pool_destroy(&p));                     /* only now is the pool object released */
}

/* ====================== 9. jobs that push jobs ====================== */

enum { FAN_DEPTH = 12 };
static PZTPool g_fan_pool = NULL;

static void job_fanout(void* p) {
	uintptr_t depth = (uintptr_t)p;

	InterlockedIncrement(&g_executed);
	if (depth < FAN_DEPTH) {
		if (!zt_pool_push(g_fan_pool, job_fanout, (void*)(depth + 1)))
			InterlockedIncrement(&g_rejected);
		if (!zt_pool_push(g_fan_pool, job_fanout, (void*)(depth + 1)))
			InterlockedIncrement(&g_rejected);
	}
}

static void test_job_pushes_jobs(void) {
	const LONG expected = (1 << (FAN_DEPTH + 1)) - 1;       /* a full binary tree of jobs */

	reset_counters();
	g_fan_pool = make_pool(2, 8, 0);
	CHECK(g_fan_pool != NULL);
	if (g_fan_pool == NULL)
		return;

	CHECK(zt_pool_push(g_fan_pool, job_fanout, (void*)0));
	CHECK(wait_at_least(&g_executed, expected, 60000));
	CHECK_EQ(g_executed, expected);
	CHECK_EQ(g_rejected, 0);

	finish_pool(&g_fan_pool);
}

/* ====================== 10. grow, shrink, and the minimum never dies ====================== */

static void test_shrink_and_minimum_survive(void) {
	enum { MIN = 4, MAX = 32, KEEP_ALIVE = 300, JOBS = 400 };

	reset_counters();
	long baseline = process_thread_count();

	PZTPool p = make_pool(MIN, MAX, KEEP_ALIVE);
	CHECK(p != NULL);
	if (p == NULL)
		return;

	CHECK_EQ(process_thread_count() - baseline, MIN);       /* bootstrap creates exactly the minimum */

	for (int i = 0; i < JOBS; ++i)
		CHECK(zt_pool_push(p, job_track, (void*)(uintptr_t)100));

	/* burst: sample the thread count while the jobs run */
	long peak_threads = 0;
	ULONGLONG start = GetTickCount64();
	while (get(&g_executed) < JOBS && GetTickCount64() - start < 60000) {
		long n = process_thread_count() - baseline;
		if (n > peak_threads)
			peak_threads = n;
		Sleep(10);
	}
	CHECK_EQ(g_executed, JOBS);
	CHECK(g_peak <= MAX);
	CHECK(g_peak >= MIN);
	CHECK(peak_threads <= MAX);

	/* idle: the extra workers must leave, down to exactly the minimum */
	ULONGLONG t0 = GetTickCount64();
	bool shrunk = false;
	while (GetTickCount64() - t0 < (ULONGLONG)KEEP_ALIVE + 15000) {
		if (process_thread_count() - baseline <= MIN) {
			shrunk = true;
			break;
		}
		Sleep(20);
	}
	ULONGLONG shrink_ms = GetTickCount64() - t0;
	CHECK(shrunk);
	printf("  workers: peak +%ld, back to +%d after %llu ms (keep-alive %d ms)\n",
		peak_threads, MIN, (unsigned long long)shrink_ms, KEEP_ALIVE);

	/* the minimum workers must survive ... */
	Sleep(KEEP_ALIVE * 4);
	CHECK_EQ(process_thread_count() - baseline, MIN);

	/* ... and the pool must still serve work after it shrank */
	g_executed = 0;
	for (int i = 0; i < 200; ++i)
		CHECK(zt_pool_push(p, job_count, NULL));
	CHECK(wait_at_least(&g_executed, 200, 10000));
	CHECK_EQ(g_executed, 200);

	finish_pool(&p);
}

/* ====================== 11. create / destroy storm: thread and handle leaks ====================== */

static void test_stress_create_destroy(void) {
	enum { CYCLES = 300, JOBS = 50 };

	long threads_before = process_thread_count();
	long handles_before = process_handle_count();

	for (int c = 0; c < CYCLES; ++c) {
		reset_counters();

		PZTPool p = make_pool(2, 4, 0);
		if (p == NULL) {
			CHECK(p != NULL);
			return;
		}

		for (int i = 0; i < JOBS; ++i)
			CHECK(zt_pool_push(p, job_count, NULL));

		CHECK(zt_pool_teardown(p));
		CHECK_EQ(g_executed, JOBS);                 /* teardown drained everything */
		CHECK(zt_pool_destroy(&p));
	}

	ULONGLONG t0 = GetTickCount64();
	while (process_thread_count() > threads_before && GetTickCount64() - t0 < 2000)
		Sleep(10);

	long threads_after = process_thread_count();
	long handles_after = process_handle_count();
	printf("  threads %ld -> %ld, handles %ld -> %ld after %d cycles\n",
		threads_before, threads_after, handles_before, handles_after, CYCLES);

	CHECK(threads_after <= threads_before + 1);     /* a leaked worker thread */
	CHECK(handles_after <= handles_before + 8);     /* a leaked handle (small slack for the runtime) */
}

/* ====================== 12. throughput by worker count (informational table) ====================== */

static void test_throughput_by_worker_count(void) {
	enum { JOBS = 300000 };
	static const long workers[] = { 1, 2, 4, 8, 16, 64, 256 };
	LARGE_INTEGER freq, a, b;

	QueryPerformanceFrequency(&freq);
	printf("  %d trivial jobs from one producer (minimum = maximum = workers):\n", JOBS);

	for (size_t w = 0; w < sizeof(workers) / sizeof(workers[0]); ++w) {

		reset_counters();
		PZTPool p = make_pool(workers[w], workers[w], 0);     /* fixed size: now really configured */
		if (p == NULL) {
			CHECK(p != NULL);
			return;
		}

		QueryPerformanceCounter(&a);
		for (int i = 0; i < JOBS; ++i)
			zt_pool_push(p, job_count, NULL);
		wait_at_least(&g_executed, JOBS, 120000);
		QueryPerformanceCounter(&b);

		CHECK_EQ(g_executed, JOBS);

		double seconds = (double)(b.QuadPart - a.QuadPart) / (double)freq.QuadPart;
		printf("    workers=%4ld  %8.0f jobs/s  (%.2f s)\n",
			workers[w], seconds > 0 ? JOBS / seconds : 0.0, seconds);

		finish_pool(&p);
	}
}

/* ====================== main ====================== */

int main(void) {
	ULONGLONG start = GetTickCount64();

	setvbuf(stdout, NULL, _IONBF, 0);

	printf("=== zt_pool test suite ===\n\n");

	RUN_TEST(test_null_and_invalid_args);
	RUN_TEST(test_bootstrap_twice_and_teardown_twice);
	RUN_TEST(test_push_null_routine);
	RUN_TEST(test_lifecycle_basic);
	RUN_TEST(test_destroy_paths);
	RUN_TEST(test_every_job_runs_exactly_once);
	RUN_TEST(test_drain_on_teardown);
	RUN_TEST(test_push_after_teardown_rejected);
	RUN_TEST(test_concurrency_bound);
	RUN_TEST(test_no_head_of_line_blocking);
	RUN_TEST(test_concurrent_producers);
	RUN_TEST(test_teardown_while_producing);
	RUN_TEST(test_job_pushes_jobs);
	RUN_TEST(test_shrink_and_minimum_survive);
	RUN_TEST(test_stress_create_destroy);
	RUN_TEST(test_throughput_by_worker_count);

	printf("\n=== %ld checks, %ld failed, %llu ms ===\n",
		(long)g_checks, (long)g_failed, (unsigned long long)(GetTickCount64() - start));
	printf(g_failed == 0 ? "ALL TESTS PASSED\n" : "SOME TESTS FAILED\n");

	return g_failed == 0 ? 0 : 1;
}
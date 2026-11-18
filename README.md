# Zenith Pool

A dynamic thread pool for Windows, written in C, with no dependency on the C runtime.

Workers are created on demand between a minimum and a maximum, idle workers above the minimum leave after a
keep-alive time, and shutdown is graceful: every job that was accepted runs before `zt_pool_teardown()` returns.

> **Status: 0.1.** The pool core is complete. Job handles and cancellation are the next milestone (see
> [Roadmap](#roadmap)). Windows is the only supported platform for now.

## Features

- Dynamic size: `minimum` workers always alive, grows up to `maximum` when jobs queue up.
- Keep-alive: workers above the minimum leave after being idle (configurable, 3 s by default).
- Graceful shutdown: accepted jobs always run, then workers exit and are joined.
- Thread-safe `zt_pool_push()` from any thread, including from inside a job.
- FIFO job start order.
- No C runtime: it only needs the Windows system libraries `kernel32` and `bcrypt` (see [No C runtime](#no-c-runtime)).
- Small C API. Every function reports failure through its `bool` result and never aborts the process.

## Requirements

| | |
|---|---|
| OS | Windows Vista or later (uses `CONDITION_VARIABLE`) |
| Toolchain | MSVC (`cl.exe`) or `clang-cl`, CMake 3.15+ |
| Architecture | Developed and tested on x64. x86 builds are not tested. |

## Build

Open a **terminal session**, go to the repository root and run:

```cmake
cmake -S . -B build -A x64
cmake --build build --config Release
```

## Using the library

Add the include folder and link `zenith-pool.lib` plus the system libraries `kernel32.lib` (an MSVC default) and `bcrypt.lib`.

Your own program is free to use the C runtime as usual. The library does not care which one you pick.

## Quick start

```c
#include <stdint.h>
#include <stdio.h>

#include "zt_pool.h"

static void work(void* parameter) {
	printf("job %ld\n", (long)(intptr_t)parameter);
}

int main(void) {

	PZTPool pool = NULL;

	if (!zt_pool_create(&pool))
		return 1;

	zt_pool_set_maximum_workers(pool, 8);      /* raise the maximum first ... */
	zt_pool_set_minimum_workers(pool, 4);      /* ... then the minimum        */
	zt_pool_set_keep_alive(pool, 5000);        /* extra workers leave after 5 s idle */

	if (!zt_pool_bootstrap(pool)) {
		zt_pool_destroy(&pool);
		return 1;
	}

	for (long i = 0; i < 100; ++i)
		zt_pool_push(pool, work, (void*)(intptr_t)i);

	zt_pool_teardown(pool);                    /* waits until all 100 jobs have run */
	zt_pool_destroy(&pool);

	return 0;
}
```

## API

| Function | What it does |
|---|---|
| `zt_pool_create(&pool)` | Creates a stopped pool with defaults. `*pool` must be `NULL`. |
| `zt_pool_destroy(&pool)` | Stops the pool if it is running, frees everything, nulls the pointer. |
| `zt_pool_set_stack_size(pool, bytes)` | Stack size for workers created from now on. `0` = system default. |
| `zt_pool_set_keep_alive(pool, ms)` | Idle time before an extra worker leaves. `0` = never leaves. |
| `zt_pool_set_minimum_workers(pool, n)` | Workers that always stay alive. `1 <= n <= current maximum`. |
| `zt_pool_set_maximum_workers(pool, n)` | Highest worker count. `n >= 1`. |
| `zt_pool_bootstrap(pool)` | Starts the minimum number of workers. `true` if already running. |
| `zt_pool_teardown(pool)` | Graceful stop: drains the queue, joins every worker. |
| `zt_pool_push(pool, routine, parameter)` | Queues a job. Never waits for it. |

Defaults: `minimum = max(1, processors / 2)`, `maximum = processors`, keep-alive 3000 ms, stack size = system default.

Full contracts are documented in [`zt_pool.h`](include/zt_pool.h).

## How it works

### Worker lifecycle

1. A worker is counted as *idle* from the moment it is created, before its thread starts.
2. It takes the lock and waits on the condition variable while the queue is empty and the pool is running.
3. When there is a job it marks itself active, **releases the lock**, runs the job, takes the lock again and marks itself idle.
4. It leaves in one of two ways:
   - **Shutdown:** the pool is stopped and the queue is empty. The worker stays in the worker list; `zt_pool_teardown()`
     joins and destroys it.
   - **Idle timeout:** an *extra* worker (above the minimum) that waited a whole keep-alive period removes itself from the
     list **in the same locked step** in which it checks that the worker count is still above the minimum. Because the check
     and the removal are one step, several workers can never drive the pool below its minimum.

### Growing

`zt_pool_push()` queues the job and then, if *idle workers < waiting jobs* and the pool is below its maximum, starts one more worker. 

This is a heuristic: a worker that was woken but has not yet taken its job still counts as idle, so a burst of
very short jobs can start a few more workers than strictly needed. 

For short CPU-bound jobs keep `maximum` close to the number of processors; large maximums are meant for jobs that spend their time blocked.

### Shutdown

`zt_pool_teardown()` sets the stopped flag and wakes every worker **under the lock** (so no worker can check the flag and then
miss the wake-up), then joins the workers **without** the lock (a worker needs the lock to leave its wait).

#### Guarantees:
- Every job accepted by `zt_pool_push()` runs exactly once, even if the teardown starts right after the push.
- `zt_pool_push()` returns `false` once the pool is stopped.
- The pool object stays valid after the teardown, so a late push safely returns `false` until `zt_pool_destroy()`.
- It blocks until all workers have exited. 
- A job that never returns therefore blocks it forever.

### Rules for jobs

- A job must return. Never call `ExitThread` or `TerminateThread` from it.
- A job may call `zt_pool_push()`.
- A job must **not** call `zt_pool_teardown()` or `zt_pool_destroy()`: a worker cannot wait for itself.
- The `parameter` must stay valid until the job has finished. The pool never copies or frees it.
- A routine that recurses deeply or uses large local buffers needs a bigger stack (`zt_pool_set_stack_size`).

### Thread safety

| Call | From other threads |
|---|---|
| `zt_pool_push` | Safe at any time, also from jobs. |
| `zt_pool_set_*` | Safe at any time. They take effect for workers created later, or the next time an idle worker waits. |
| `zt_pool_bootstrap`, `zt_pool_teardown`, `zt_pool_destroy` | Must not run concurrently with each other. |
| Any call during or after `zt_pool_destroy` | Not allowed. |

Several pools can exist in one process; there is no global state.

### Memory

The library allocates from the process heap through its own small allocator (zero-initialized blocks, no `malloc`). 

A queued job costs the queue nodes (two small allocations) and no job object of its own.

All allocations of a push happen while the pool lock is held.

## No C runtime

The library is built without the C runtime, so it can be linked into programs that do not use one.
System libraries: `kernel32` and `bcrypt` only, There is no C runtime import library.

What the build does:

| Flag | Why |
|---|---|
| `/Zl` | Do not record a default C runtime library name in the object files. |
| `/GS-` | No stack cookies (they need `__security_cookie` from the runtime). |
| `/RTC1` removed in Debug | The Debug runtime checks call into the runtime. |
| `/Gy` | One function per COMDAT, so unused code is dropped at link time. |

What the code must obey to stay free of the runtime:

- No `<stdio.h>`, `<string.h>` or `<stdlib.h>` functions. Use the `zt_*` memory functions instead.
- Do not initialize aggregates with `= { 0 }` or copy large structs by assignment: the compiler may turn them into calls to
  `memset`/`memcpy`, especially in Debug builds. Use `zt_memset`.
- Keep stack frames under 4 KB (bigger frames call `__chkstk`).
- No floating-point globals (`_fltused`).

The test programs under `tests/` **do** use the C runtime (`printf`, `calloc`), because they are programs, not part of the library.

## Testing

The tests live in `tests/` and are built separately (they need the C runtime and a few internal headers):

```powershell
cl /W4 /I include /I src tests\test_pool.c build\zenith-pool.lib bcrypt.lib
.\test_pool.exe
```

## Limitations

- Windows only (x64 tested).
- One shared lock: throughput of very short jobs is bounded by it. There are no per-worker queues or work stealing yet.
- No priorities, no job timeouts, no way to cancel a job yet.

## Roadmap

 -  **Jobs and cancellation:** 
	- a job handle with a state and a reference count, `cancel` / `wait` / `state`, and a
	- cooperative cancel flag the routine can check. Pending jobs cancel immediately; running jobs are asked to stop, and any
 - More platforms: Linux and macOS backends as `*_linux.c` / `*_posix.c` files. The CMake file already selects backends by suffix. 
 - A spawn  policy that reacts to queue growth instead of idle count, and cheaper pushes.

## To Infinity and beyond!

Bug reports, security reviews, and feature requests are highly appreciated. 

Feel free to open an issue or submit a pull request on GitHub.

## License

Distributed under the MIT License. See ``LICENSE`` for more information.

## Credits

### Copyright (©) 2024, Shady K. Maadawy, All rights reserved. 
 [@shadudiix](https://github.com/shadyelmaadawy)

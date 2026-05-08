# MicroThread v0.6

This is a minimal, stackful, cooperative green-thread runtime in C.

The current implemented mainstream version is **v0.6**. It intentionally keeps the core runtime focused on goroutine-like execution, timers, channels, task handles, join, cooperative cancellation, nonblocking channel operations, channel-only select, and a multi-worker OS-thread scheduler. Network/socket I/O is not part of the core.

Implemented features:

- cooperative scheduler with a shared, thread-safe run queue
- multi-worker OS-thread execution through `mt_runtime_start(worker_count)` / `mt_run_workers(worker_count)` on Unix-like platforms
- `mt_go(fn, arg)` task creation
- `mt_go_with_stack(fn, arg, stack_size)` task creation with explicit stack size
- `mt_yield()` cooperative yielding
- `mt_sleep_ms(ms)` cooperative sleep/timer parking
- `mt_chan_create`, `mt_chan_send`, `mt_chan_recv`, `mt_chan_close`, and `mt_chan_destroy`
- `mt_chan_try_send` and `mt_chan_try_recv` nonblocking channel operations
- `mt_select()` for channel send/receive, default, and timeout cases
- buffered and unbuffered cooperative channels
- sender/receiver parking on channel wait queues
- task handles through `mt_go_handle()` and `mt_go_handle_with_stack()`
- `mt_join()` for waiting on handled task completion from another microthread
- cooperative cancellation through `mt_task_cancel()` and `mt_task_cancelled()`
- task status queries through `mt_task_status()`
- explicit handle release through `mt_task_handle_release()`
- `mt_run()` run loop
- `mt_runtime_start(worker_count)`, `mt_runtime_workers()`, and `mt_run_workers(worker_count)` worker runtime API
- configurable stack sizes with a minimum stack-size check
- guarded stack mappings on Unix-like platforms using `mmap`/`mprotect`
- Windows Fiber stack sizing through `CreateFiber`
- timer heap for sleeping tasks
- Unix `ucontext` backend for non-macOS Unix-like platforms
- macOS x86_64/aarch64 assembly context backend, avoiding deprecated Apple `ucontext` APIs
- Windows Fibers backend
- basic, sleep, channel, handle, select, nonblocking-channel, and advanced-select examples
- comprehensive v0.1/v0.2/v0.3/v0.4/v0.5 test suites mapped to the versioned test plans
- checked-in v0.6 multi-worker test coverage for lifecycle, parallel execution, concurrent task creation, channels, select, join/cancel, fault hooks, memory counters, and ThreadSanitizer

## Build

```sh
make
```

## Run tests

```sh
make test
```

Run the larger practical stress profile:

```sh
make stress
```

Run sanitizer checks, where supported by your compiler/platform:

```sh
make sanitize
```

Run Valgrind leak/resource checks on Unix-like systems when Valgrind is installed:

```sh
make valgrind
```

The v0.1/v0.2/v0.3/v0.4/v0.5/v0.6 test-plan mapping is documented in `tests/TEST_COVERAGE.md`.

The v0.2 test plan is documented in `v0_2_test_plan.md`.

The v0.3 channel test plan is documented in `v0_3_test_plan.md`.

The v0.4 core-runtime test plan is documented in `v0_4_test_plan.md` and covered by `tests/test_v0_4.c`.

The v0.5 implementation is present and `tests/test_v0_5.c` is mapped to the v0.5 test plan, including nonblocking channel operations, immediate/blocking select, default/timeout cases, close/destroy lifecycle behavior, scheduler/lifecycle interactions, fault-injection, memory-counter checks, and practical stress coverage. The v0.6 multi-worker scheduler implementation is present and `tests/test_v0_6.c` covers the required shared-queue multi-worker behavior from the v0.6 plan; work-stealing-specific cases are not applicable to this shared-queue implementation.

Run the expected-crash guard-page overflow test on Unix-like platforms:

```sh
make guard-test
```

Run the guard-page-disabled fallback configuration test:

```sh
make guard-disabled-test
```

## Run examples

```sh
make example
make sleep-example
make channels-example
make handles-example
make select-example
make try-example
make select-advanced-example
make examples
```

## API

```c
typedef void (*mt_fn)(void *arg);

typedef struct mt_chan mt_chan_t;
typedef struct mt_task_handle mt_task_handle_t;

typedef enum mt_task_status {
    MT_TASK_STATUS_READY,
    MT_TASK_STATUS_RUNNING,
    MT_TASK_STATUS_SLEEPING,
    MT_TASK_STATUS_WAITING_CHAN,
    MT_TASK_STATUS_WAITING_JOIN,
    MT_TASK_STATUS_DONE,
    MT_TASK_STATUS_CANCELLED
} mt_task_status_t;

int  mt_init(void);
int  mt_go(mt_fn fn, void *arg);
int  mt_go_with_stack(mt_fn fn, void *arg, size_t stack_size);
mt_task_handle_t *mt_go_handle(mt_fn fn, void *arg);
mt_task_handle_t *mt_go_handle_with_stack(mt_fn fn, void *arg, size_t stack_size);
int  mt_run(void);
int  mt_runtime_start(size_t worker_count);
int  mt_runtime_workers(void);
int  mt_run_workers(size_t worker_count);
void mt_yield(void);
void mt_sleep_ms(uint64_t ms);
void mt_shutdown(void);

int  mt_join(mt_task_handle_t *task);
int  mt_task_cancel(mt_task_handle_t *task);
int  mt_task_cancelled(void);
int  mt_task_status(mt_task_handle_t *task, mt_task_status_t *out_status);
void mt_task_handle_release(mt_task_handle_t *task);

mt_chan_t *mt_chan_create(size_t elem_size, size_t capacity);
int        mt_chan_send(mt_chan_t *ch, const void *value);
int        mt_chan_recv(mt_chan_t *ch, void *out);
int        mt_chan_try_send(mt_chan_t *ch, const void *value);
int        mt_chan_try_recv(mt_chan_t *ch, void *out);
int        mt_chan_close(mt_chan_t *ch);
int        mt_chan_destroy(mt_chan_t *ch);
size_t     mt_chan_len(const mt_chan_t *ch);
size_t     mt_chan_capacity(const mt_chan_t *ch);
int        mt_chan_is_closed(const mt_chan_t *ch);

typedef enum mt_select_op {
    MT_SELECT_RECV,
    MT_SELECT_SEND,
    MT_SELECT_DEFAULT,
    MT_SELECT_TIMEOUT
} mt_select_op_t;

typedef struct mt_select_case {
    mt_select_op_t op;
    mt_chan_t *ch;
    void *value;
    uint64_t timeout_ms;
} mt_select_case_t;

int mt_select(mt_select_case_t *cases, size_t count, size_t *selected_index);
```

## Notes

This is intentionally early runtime code. It does not yet include preemption. On Unix-like platforms, `mt_runtime_start(n)` / `mt_run_workers(n)` run microthreads across multiple pthread-backed OS workers using a shared, mutex-protected run queue, timer heap, channel queues, select waiters, and task-handle state. The implementation is thread-safe at the runtime API boundary, but it is still cooperative: a running microthread keeps its OS worker until it yields, sleeps, parks, or returns.

The current multi-worker scheduler uses a single global run queue rather than per-worker local queues and work stealing. This satisfies true parallel worker execution and cross-worker wakeups, but strict work-stealing-specific v0.6 tests remain future work unless the plan is updated to accept the simpler shared-queue scheduler.

Cancellation is cooperative. `mt_task_cancel()` sets a cancellation request and wakes sleeping/channel/join waiters where possible, but it does not asynchronously kill a running task. Running tasks should periodically call `mt_task_cancelled()` or return from interrupted runtime operations.

`mt_join()` is a green-thread operation. Calling it outside a running microthread returns `MT_ERR_STATE` instead of blocking the owner OS thread.

`mt_chan_try_send()` and `mt_chan_try_recv()` never park the current task. They return `MT_ERR_WOULD_BLOCK` when an open channel operation cannot complete immediately.

`mt_select()` is channel-only. It supports send, receive, one default case, and one timeout case. When no channel case is immediately ready, default fires immediately; a zero timeout behaves like default; otherwise the current microthread parks until a channel case becomes ready, the timeout expires, cancellation wakes it, or shutdown occurs.

Task handles are user-owned. Release each handle with `mt_task_handle_release()` when you no longer need to join/query/cancel that task. Releasing a handle does not kill the task.

`mt_sleep_ms(0)` is documented as yield-like behavior. Calling `mt_sleep_ms()` outside a running microthread is a safe no-op.

The scheduler is portable C. Platform-specific context switching is isolated behind `src/context.h`. Non-macOS Unix-like builds currently use `ucontext`, macOS x86_64/aarch64 builds use the assembly backend, and Windows builds use Fibers.

For platforms or embedders that cannot use guard pages, build with `-DMT_DISABLE_GUARD_PAGES`. In that mode, the runtime still supports stackful microthreads and debug metadata reports a guard size of zero, but stack overflow protection is intentionally unavailable.

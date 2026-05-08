# Green Threads v0.6

This is a minimal, stackful, cooperative green-thread runtime in C.

The current implemented mainstream version is **v0.6**. It intentionally keeps the core runtime focused on goroutine-like execution, timers, channels, task handles, join, cooperative cancellation, nonblocking channel operations, channel-only select, and a multi-worker OS-thread scheduler. Network/socket I/O is not part of the core.

Implemented features:

- cooperative scheduler with a shared, thread-safe run queue
- multi-worker OS-thread execution through `gt_runtime_start(worker_count)` / `gt_run_workers(worker_count)` on Unix-like platforms
- `gt_go(fn, arg)` task creation
- `gt_go_with_stack(fn, arg, stack_size)` task creation with explicit stack size
- `gt_yield()` cooperative yielding
- `gt_sleep_ms(ms)` cooperative sleep/timer parking
- `gt_chan_create`, `gt_chan_send`, `gt_chan_recv`, `gt_chan_close`, and `gt_chan_destroy`
- `gt_chan_try_send` and `gt_chan_try_recv` nonblocking channel operations
- `gt_select()` for channel send/receive, default, and timeout cases
- buffered and unbuffered cooperative channels
- sender/receiver parking on channel wait queues
- task handles through `gt_go_handle()` and `gt_go_handle_with_stack()`
- `gt_join()` for waiting on handled task completion from another green thread
- cooperative cancellation through `gt_task_cancel()` and `gt_task_cancelled()`
- task status queries through `gt_task_status()`
- explicit handle release through `gt_task_handle_release()`
- `gt_run()` run loop
- `gt_runtime_start(worker_count)`, `gt_runtime_workers()`, and `gt_run_workers(worker_count)` worker runtime API
- configurable stack sizes with a minimum stack-size check
- guarded stack mappings on Unix-like platforms using `mmap`/`mprotect`
- Windows Fiber stack sizing through `CreateFiber`
- timer heap for sleeping tasks
- Unix `ucontext` backend
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
typedef void (*gt_fn)(void *arg);

typedef struct gt_chan gt_chan_t;
typedef struct gt_task_handle gt_task_handle_t;

typedef enum gt_task_status {
    GT_TASK_STATUS_READY,
    GT_TASK_STATUS_RUNNING,
    GT_TASK_STATUS_SLEEPING,
    GT_TASK_STATUS_WAITING_CHAN,
    GT_TASK_STATUS_WAITING_JOIN,
    GT_TASK_STATUS_DONE,
    GT_TASK_STATUS_CANCELLED
} gt_task_status_t;

int  gt_init(void);
int  gt_go(gt_fn fn, void *arg);
int  gt_go_with_stack(gt_fn fn, void *arg, size_t stack_size);
gt_task_handle_t *gt_go_handle(gt_fn fn, void *arg);
gt_task_handle_t *gt_go_handle_with_stack(gt_fn fn, void *arg, size_t stack_size);
int  gt_run(void);
int  gt_runtime_start(size_t worker_count);
int  gt_runtime_workers(void);
int  gt_run_workers(size_t worker_count);
void gt_yield(void);
void gt_sleep_ms(uint64_t ms);
void gt_shutdown(void);

int  gt_join(gt_task_handle_t *task);
int  gt_task_cancel(gt_task_handle_t *task);
int  gt_task_cancelled(void);
int  gt_task_status(gt_task_handle_t *task, gt_task_status_t *out_status);
void gt_task_handle_release(gt_task_handle_t *task);

gt_chan_t *gt_chan_create(size_t elem_size, size_t capacity);
int        gt_chan_send(gt_chan_t *ch, const void *value);
int        gt_chan_recv(gt_chan_t *ch, void *out);
int        gt_chan_try_send(gt_chan_t *ch, const void *value);
int        gt_chan_try_recv(gt_chan_t *ch, void *out);
int        gt_chan_close(gt_chan_t *ch);
int        gt_chan_destroy(gt_chan_t *ch);
size_t     gt_chan_len(const gt_chan_t *ch);
size_t     gt_chan_capacity(const gt_chan_t *ch);
int        gt_chan_is_closed(const gt_chan_t *ch);

typedef enum gt_select_op {
    GT_SELECT_RECV,
    GT_SELECT_SEND,
    GT_SELECT_DEFAULT,
    GT_SELECT_TIMEOUT
} gt_select_op_t;

typedef struct gt_select_case {
    gt_select_op_t op;
    gt_chan_t *ch;
    void *value;
    uint64_t timeout_ms;
} gt_select_case_t;

int gt_select(gt_select_case_t *cases, size_t count, size_t *selected_index);
```

## Notes

This is intentionally early runtime code. It does not yet include preemption. On Unix-like platforms, `gt_runtime_start(n)` / `gt_run_workers(n)` run green threads across multiple pthread-backed OS workers using a shared, mutex-protected run queue, timer heap, channel queues, select waiters, and task-handle state. The implementation is thread-safe at the runtime API boundary, but it is still cooperative: a running green thread keeps its OS worker until it yields, sleeps, parks, or returns.

The current multi-worker scheduler uses a single global run queue rather than per-worker local queues and work stealing. This satisfies true parallel worker execution and cross-worker wakeups, but strict work-stealing-specific v0.6 tests remain future work unless the plan is updated to accept the simpler shared-queue scheduler.

Cancellation is cooperative. `gt_task_cancel()` sets a cancellation request and wakes sleeping/channel/join waiters where possible, but it does not asynchronously kill a running task. Running tasks should periodically call `gt_task_cancelled()` or return from interrupted runtime operations.

`gt_join()` is a green-thread operation. Calling it outside a running green thread returns `GT_ERR_STATE` instead of blocking the owner OS thread.

`gt_chan_try_send()` and `gt_chan_try_recv()` never park the current task. They return `GT_ERR_WOULD_BLOCK` when an open channel operation cannot complete immediately.

`gt_select()` is channel-only. It supports send, receive, one default case, and one timeout case. When no channel case is immediately ready, default fires immediately; a zero timeout behaves like default; otherwise the current green thread parks until a channel case becomes ready, the timeout expires, cancellation wakes it, or shutdown occurs.

Task handles are user-owned. Release each handle with `gt_task_handle_release()` when you no longer need to join/query/cancel that task. Releasing a handle does not kill the task.

`gt_sleep_ms(0)` is documented as yield-like behavior. Calling `gt_sleep_ms()` outside a running green thread is a safe no-op.

The scheduler is portable C. Platform-specific context switching is isolated behind `src/context.h` so later versions can replace `ucontext` or Fibers with assembly or a proven context library.

For platforms or embedders that cannot use guard pages, build with `-DGT_DISABLE_GUARD_PAGES`. In that mode, the runtime still supports stackful green threads and debug metadata reports a guard size of zero, but stack overflow protection is intentionally unavailable.

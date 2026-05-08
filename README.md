# MicroThread

MicroThread is a small stackful user-space threading runtime for C. It gives you lightweight tasks, cooperative scheduling, sleeps/timers, task handles, channels, `select`-style waiting, nonblocking file-descriptor waits, and an optional multi-worker runtime backed by OS threads.

The public API lives in:

```c
#include "microthread.h"
```

The library target is:

```text
build/libmicrothread.a
```

## What MicroThread is

MicroThread lets many user-space tasks share one or more OS threads. A task keeps running until it returns or calls a runtime operation that cooperatively yields, sleeps, or parks:

```text
microthread runs
  -> calls mt_yield(), mt_sleep_ms(), mt_chan_recv(), mt_select(), mt_join(), ...
  -> runtime saves the task state
  -> another ready microthread runs
  -> the parked task is resumed when its event happens
```

This is useful for learning and experimenting with runtime internals: scheduling, timers, channels, task handles, cancellation, `select`, stack management, and cross-worker wakeups.

## What MicroThread is not

MicroThread is not an HTTP framework or preemptive thread library. It does not replace pthreads. It runs on top of OS threads and schedules MicroThread tasks in user space.

A running microthread is cooperative: it keeps its current OS worker until it yields, sleeps, blocks on a runtime primitive, or returns.

Do not call blocking OS I/O directly inside a microthread unless you intentionally want to block that OS worker. Use the `mt_fd_*` / `mt_net_*` helpers for descriptor readiness I/O.

## Quick example

```c
#include "microthread.h"

#include <stdio.h>

static void worker(void *arg) {
    const char *name = arg;

    for (int i = 0; i < 3; ++i) {
        printf("%s: %d\n", name, i);
        mt_yield();
    }
}

int main(void) {
    mt_init();

    mt_go(worker, "a");
    mt_go(worker, "b");

    mt_run();
    mt_shutdown();
    return 0;
}
```

Build with:

```sh
make
```

Run examples with:

```sh
make examples
```

## Main features

- Stackful microthreads with cooperative context switching.
- `mt_go()` and `mt_go_with_stack()` for task creation.
- `mt_run()` for single-worker execution.
- `mt_runtime_start(worker_count)` / `mt_run_workers(worker_count)` for multi-worker execution.
- `mt_yield()` and `mt_sleep_ms()`.
- Buffered and unbuffered channels.
- Blocking and nonblocking channel send/receive.
- Channel close/destroy lifecycle handling.
- `mt_select()` over channel send/receive cases, default, and timeout.
- Task handles, `mt_join()`, task status queries, and explicit handle release.
- Cooperative cancellation through `mt_task_cancel()` and `mt_task_cancelled()`.
- Nonblocking fd readiness waits with `mt_fd_wait_read()`, `mt_fd_wait_write()`, and `mt_fd_wait()`.
- Nonblocking fd read/write helpers and small TCP listen/accept wrappers.
- Guarded stacks on supported Unix-like platforms, with an opt-out build flag.
- macOS assembly context backend to avoid Apple’s deprecated `ucontext` APIs.
- Unix `ucontext` backend for non-macOS Unix-like platforms.
- Windows Fiber backend.

## Build

```sh
make
```

Clean build artifacts:

```sh
make clean
```

Use the produced static library:

```text
build/libmicrothread.a
```

## Tests

Run the default suite:

```sh
make test
```

The default suite includes the v0.7 I/O tests twice: once with the platform's
native backend when available, and once with `MT_FORCE_POLL_BACKEND` so the
portable `poll` fallback stays covered.

Run sanitizer builds where supported:

```sh
make sanitize
```

Run the v0.6 ThreadSanitizer suite where supported:

```sh
make tsan
```

Run Valgrind checks when Valgrind is installed:

```sh
make valgrind
```

Run the larger practical stress profile:

```sh
make stress
```

Run guard-page specific checks:

```sh
make guard-test
make guard-disabled-test
```

The versioned test plans are stored under `tests/`:

```text
tests/v0_1_test_plan.md
tests/v0_2_test_plan.md
tests/v0_3_test_plan.md
tests/v0_4_test_plan.md
tests/v0_5_test_plan.md
tests/v0_6_test_plan.md
tests/v0_7_test_plan.md
```

## Examples

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

The example sources are in `examples/`.

## API overview

```c
typedef void (*mt_fn)(void *arg);

typedef struct mt_chan mt_chan_t;
typedef struct mt_task_handle mt_task_handle_t;

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
```

`mt_select()` uses `mt_select_case_t` entries:

```c
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

See `include/microthread.h` for the full public API and error codes.

## Runtime model

MicroThread has two layers:

```text
OS workers       real OS threads created by the platform
Microthreads     user-space tasks scheduled by this runtime
```

Single-worker mode:

```text
one OS thread
  -> many microthreads
```

Multi-worker mode:

```text
worker 1 ----\
worker 2 ----- shared runtime queues, timers, channels, task state
worker 3 ----/
```

The current multi-worker implementation uses a shared mutex-protected run queue and condition-variable wakeups. This gives true parallel execution across OS workers while keeping scheduling behavior simple. It does not currently use per-worker local queues or work stealing.

## Channels

A buffered channel stores values in a queue:

```text
sender -> [ buffer ] -> receiver
```

An unbuffered channel performs a direct rendezvous:

```text
sender waits <-> receiver arrives
```

or:

```text
receiver waits <-> sender arrives
```

Blocking channel operations park only the current microthread. They do not intentionally block the whole runtime unless there is no other runnable work.

Nonblocking operations never park:

```c
mt_chan_try_send(...);
mt_chan_try_recv(...);
```

They return `MT_ERR_WOULD_BLOCK` when the operation cannot complete immediately on an open channel.

## Select

`mt_select()` waits for one of several channel operations:

```text
receive from channel A
send to channel B
default case
timeout case
```

Rules:

- If a channel case is ready immediately, one ready case is selected.
- If no channel case is ready and a default case exists, default is selected immediately.
- A zero timeout behaves like default.
- Otherwise, the current microthread parks until a channel case becomes ready, the timeout expires, cancellation wakes it, shutdown occurs, or the involved channel is closed/destroyed.
- When one case wins, the runtime unregisters the losing cases so the same task is not woken twice.

## File-descriptor and socket I/O

MicroThread includes a v0.7 I/O foundation for descriptors and sockets. These APIs are lower-level than HTTP: they let a microthread park on descriptor readiness without parking the whole OS worker thread.

Core APIs:

```c
mt_fd_set_nonblocking(fd);
mt_fd_wait_read(fd, timeout_ms);
mt_fd_wait_write(fd, timeout_ms);
mt_fd_wait(fd, MT_FD_READ | MT_FD_WRITE, timeout_ms, &ready_events);

mt_fd_read(fd, buf, len, timeout_ms);
mt_fd_write(fd, buf, len, timeout_ms);
mt_fd_close(fd);
mt_io_backend_name();
```

Socket convenience APIs:

```c
int listen_fd = mt_net_listen_tcp("127.0.0.1", "8080", 128);
int client_fd = mt_net_accept(listen_fd, NULL, NULL, 5000);
```

Current implementation notes:

- The public semantics are readiness-based and nonblocking for the current microthread.
- The runtime selects a native readiness backend when available: `epoll` on Linux, `kqueue` on macOS/BSD, and `poll` as the portable Unix fallback.
- Build with `-DMT_FORCE_POLL_BACKEND`, or run `make force-poll-build`, to force the portable `poll` backend on platforms that would normally use `epoll` or `kqueue`.
- `mt_io_backend_name()` reports the active backend as `"epoll"`, `"kqueue"`, `"poll"`, `"none"`, or `"unsupported"`.
- Backend resources are initialized once with the runtime and cleaned up by `mt_shutdown()`.
- `timeout_ms == 0` means a nonblocking readiness poll. The fd APIs use finite millisecond timeouts; pass a very large value when a practical indefinite wait is desired.
- `MT_ERR_TIMEOUT`, `MT_ERR_CANCELLED`, `MT_ERR_CLOSED`, and `MT_ERR_INVALID` are distinct statuses.
- `mt_fd_close(fd)` closes the descriptor and wakes MicroThread waiters for that fd.
- `mt_net_write()` avoids `SIGPIPE` where the platform provides `MSG_NOSIGNAL` or `SO_NOSIGPIPE`; `mt_fd_write()` is the lower-level descriptor helper and may inherit normal platform `write()` behavior.
- A single active waiter per descriptor is supported; overlapping waits on the same fd return `MT_ERR_STATE`.
- Descriptor reuse is guarded for descriptors closed through `mt_fd_close(fd)`. Raw `close(fd)` while a microthread is waiting on the same fd is unsupported; use `mt_fd_close(fd)`.

This is enough to build echo-server-style examples. Full HTTP parsing, routing, request limits, and production server behavior are intentionally separate future layers.

Build the echo server example with:

```sh
make build/echo_server
```

Run it with:

```sh
./build/echo_server 8080
```

## Task handles and cancellation

`mt_go_handle()` creates a task and returns a user-owned handle. Use the handle to join, cancel, or query the task:

```c
mt_task_handle_t *h = mt_go_handle(worker, arg);
mt_join(h);
mt_task_handle_release(h);
```

Cancellation is cooperative. `mt_task_cancel()` requests cancellation and wakes tasks parked in supported runtime operations. It does not asynchronously kill a task that is currently running C code. Long-running tasks should periodically call `mt_task_cancelled()`.

`mt_join()` is a microthread operation. Calling it outside a running microthread returns `MT_ERR_STATE` instead of blocking the owner OS thread.

## Platform notes

Context switching is isolated behind `src/context.h`.

Current backends:

- macOS x86_64/aarch64: assembly backend.
- non-macOS Unix-like platforms: `ucontext` backend.
- Windows: Fiber backend.

For platforms or embedders that cannot use guard pages, build with:

```sh
-DMT_DISABLE_GUARD_PAGES
```

In that mode, MicroThread still uses stackful tasks, but stack overflow guard pages are disabled.

## Current scope

MicroThread currently focuses on the core runtime:

```text
tasks
scheduler
timers
channels
select
task handles
join
cooperative cancellation
multi-worker execution
fd/socket readiness I/O
```

Out of scope for now:

```text
HTTP server framework
preemptive scheduling
work stealing
async file I/O
production-grade ABI stability
```

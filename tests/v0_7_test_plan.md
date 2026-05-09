# v0.7 Test Plan — Nonblocking File-Descriptor and Socket I/O

v0.7 turns MicroThread from a pure task/channel runtime into a runtime that can
park microthreads on operating-system file-descriptor readiness without blocking
an OS worker thread.

This release should **not** implement a full HTTP server framework. It should add
the lower-level I/O readiness layer that makes HTTP servers possible later.

The implementation should support:

- Linux: `epoll`
- macOS/BSD: `kqueue`
- portable fallback: `poll`

The fallback may be less scalable, but must preserve the same public semantics.

Assumed previous implemented/core features:

- v0.1: microthreads, yield, run queue
- v0.2: sleep/timers and guarded/custom stacks
- v0.3: buffered/unbuffered channels
- v0.4: task handles, join, cooperative cancellation
- v0.5: nonblocking channel operations and channel select
- v0.6: multi-worker OS-thread scheduler and thread-safe runtime core

If a platform cannot support a specific backend, tests must skip with a clear
message rather than silently passing without coverage.

## Proposed v0.7 API Scope

Exact names may still change, but the test plan assumes an API in this shape:

```c
int mt_fd_set_nonblocking(int fd);
int mt_fd_adopt(int fd);
int mt_fd_release(int fd);
int mt_fd_wait_read(int fd, uint64_t timeout_ms);
int mt_fd_wait_write(int fd, uint64_t timeout_ms);
int mt_fd_wait(int fd, int events, uint64_t timeout_ms, int *ready_events);

ssize_t mt_fd_read(int fd, void *buf, size_t len, uint64_t timeout_ms);
ssize_t mt_fd_write(int fd, const void *buf, size_t len, uint64_t timeout_ms);
int mt_fd_close(int fd);
const char *mt_task_status_name(mt_task_status_t status);
int mt_last_os_error(void);
```

Socket convenience wrappers may be added in the same release or a follow-up:

```c
int mt_net_listen_tcp(const char *host, const char *port, int backlog);
int mt_net_accept(int listen_fd, struct sockaddr *addr, socklen_t *addrlen,
                  uint64_t timeout_ms);
ssize_t mt_net_read(int fd, void *buf, size_t len, uint64_t timeout_ms);
ssize_t mt_net_write(int fd, const void *buf, size_t len, uint64_t timeout_ms);
int mt_net_close(int fd);
```

The important semantic contract is:

```text
If a descriptor is not ready, only the current microthread parks.
The OS worker thread remains free to run other microthreads.
```

## Error and Timeout Semantics

The exact numeric constants should match the project style. Tests should require
these categories even if names change:

- success when the descriptor becomes ready
- `MT_ERR_TIMEOUT` when the timeout expires first
- `MT_ERR_CANCELLED` when the waiting task is cancelled first
- `MT_ERR_CLOSED` or documented close/error status when the descriptor is closed
  while a task waits
- `MT_ERR_INVALID` for invalid descriptors, invalid event masks, null out
  pointers, and invalid argument combinations
- `MT_ERR_STATE` when a wait API is called from outside a running microthread and
  the API requires a microthread context

For `mt_fd_read()` / `mt_fd_write()`:

- positive return values are byte counts
- `0` from read means EOF when the peer closed cleanly
- negative returns are MicroThread status values; `mt_last_os_error()` should
  expose the thread-local OS/backend error captured by the failing MicroThread
  path when that failure came from the operating system/backend
- timeout/cancel/closed statuses must be distinguishable from normal EOF

## 0. Test Harness Requirements

- **TC-V07-HARNESS-001**: tests can create local connected file descriptors
  without external network access. Prefer `socketpair()` where available.
- **TC-V07-HARNESS-002**: tests can create a loopback TCP listener/client for
  accept/read/write integration tests.
- **TC-V07-HARNESS-003**: tests can run the same scenario with 1, 2, and 4
  workers.
- **TC-V07-HARNESS-004**: all blocking I/O tests have bounded deadlines so missed
  wakeups fail instead of hanging.
- **TC-V07-HARNESS-005**: tests can detect whether a worker stayed productive
  while another microthread waited on I/O.
- **TC-V07-HARNESS-006**: backend-specific tests can report the active backend:
  epoll, kqueue, or poll.
- **TC-V07-HARNESS-007**: tests can skip unsupported platform-specific assertions
  with a clear message.
- **TC-V07-HARNESS-008**: sanitizer and stress targets reuse checked-in tests, not
  private one-off smoke programs.
- **TC-V07-HARNESS-009**: tests avoid relying on public internet, DNS, fixed
  ports, or wall-clock timing tighter than the runtime can reliably guarantee.

## 1. Regression Tests

- **TC-V07-REG-001**: all v0.1 tests pass after adding the I/O backend.
- **TC-V07-REG-002**: all v0.2 tests pass after adding the I/O backend.
- **TC-V07-REG-003**: all v0.3 tests pass after adding the I/O backend.
- **TC-V07-REG-004**: all v0.4 tests pass after adding the I/O backend.
- **TC-V07-REG-005**: all v0.5 tests pass after adding the I/O backend.
- **TC-V07-REG-006**: all v0.6 tests pass after adding the I/O backend.
- **TC-V07-REG-007**: existing examples compile unchanged.
- **TC-V07-REG-008**: the library still builds on Linux and macOS.
- **TC-V07-REG-009**: Windows builds either keep a documented no-v0.7 fallback or
  implement a Windows-specific readiness backend; unsupported APIs fail clearly.

## 2. Backend Initialization and Lifecycle

- **TC-IO-INIT-001**: `mt_init()` initializes the fd-event backend lazily or
  eagerly according to documented behavior.
- **TC-IO-INIT-002**: `mt_shutdown()` closes backend resources without leaking
  file descriptors.
- **TC-IO-INIT-003**: repeated `mt_init()` / run / `mt_shutdown()` cycles do not
  leak backend resources.
- **TC-IO-INIT-004**: backend initialization failure returns a documented error and
  leaves the runtime reusable.
- **TC-IO-INIT-005**: backend resources are created once per runtime, not once per
  fd wait.
- **TC-IO-INIT-006**: backend wake mechanism, such as pipe/eventfd/kqueue user
  event, wakes idle workers when needed.
- **TC-IO-INIT-007**: backend shutdown wakes all fd waiters.
- **TC-IO-INIT-008**: `mt_runtime_start()` with no fd waiters behaves exactly as
  in v0.6.
- **TC-IO-INIT-009**: fd backend remains usable after a previous runtime run ended
  due to no runnable tasks.
- **TC-IO-INIT-010**: backend works in single-worker and multi-worker modes.

## 3. Nonblocking Mode and Descriptor Validation

- **TC-FD-NONBLOCK-001**: `mt_fd_set_nonblocking(fd)` makes a blocking socket
  nonblocking.
- **TC-FD-NONBLOCK-002**: calling `mt_fd_set_nonblocking(fd)` twice is harmless.
- **TC-FD-NONBLOCK-003**: invalid fd returns `MT_ERR_INVALID` or an errno-backed
  documented error.
- **TC-FD-NONBLOCK-003A**: `mt_fd_adopt(fd)` makes a descriptor nonblocking and
  prepares descriptor-generation metadata for MicroThread I/O.
- **TC-FD-NONBLOCK-003B**: `mt_fd_release(fd)` removes MicroThread descriptor
  metadata without closing the descriptor, rejects release while a waiter is active,
  and restores the descriptor flags captured when the fd was first adopted when possible.
- **TC-FD-NONBLOCK-004**: fd wait APIs reject negative descriptors.
- **TC-FD-NONBLOCK-005**: fd wait APIs reject unsupported event masks.
- **TC-FD-NONBLOCK-006**: fd wait APIs validate null output pointers where output
  pointers are required.
- **TC-FD-NONBLOCK-007**: fd wait APIs called outside a microthread return the
  documented error rather than parking the owner thread accidentally.
- **TC-FD-NONBLOCK-008**: regular files either report immediately ready or return
  a clearly documented unsupported status.
- **TC-FD-NONBLOCK-009**: duplicate fd numbers after close/reopen do not wake the
  wrong waiter.
- **TC-FD-NONBLOCK-010**: descriptor ownership is documented: APIs that close fds
  close exactly once; wait APIs do not take ownership.

## 4. Read Readiness Waits

- **TC-FD-READ-001**: waiting for read on a socketpair returns ready when peer
  writes data.
- **TC-FD-READ-002**: waiting for read on an already-readable fd returns
  immediately without parking.
- **TC-FD-READ-003**: waiting for read on an empty nonblocking socket parks only
  the current microthread.
- **TC-FD-READ-004**: another runnable microthread continues executing while one
  microthread waits for read.
- **TC-FD-READ-005**: peer close wakes a read waiter and reports EOF/closed using
  documented semantics.
- **TC-FD-READ-006**: multiple read waiters on the same fd are rejected according to the documented one-read-waiter policy.
- **TC-FD-READ-007**: readiness is not lost if the peer writes just before the
  waiter registers.
- **TC-FD-READ-008**: readiness is not lost if the peer writes concurrently with
  waiter registration.
- **TC-FD-READ-009**: read readiness waiter unregisters after wake.
- **TC-FD-READ-010**: repeated read wait / consume / read wait cycles work.
- **TC-FD-READ-011**: read wait on a closed local fd returns a documented error.
- **TC-FD-READ-012**: read wait can be cancelled safely.

## 5. Write Readiness Waits

- **TC-FD-RW-000**: one read waiter and one write waiter may coexist on the same fd; duplicate read/write waiters or overlapping combined waits still return `MT_ERR_STATE`.

- **TC-FD-WRITE-001**: waiting for write on a writable socket returns ready
  immediately.
- **TC-FD-WRITE-002**: after filling a nonblocking socket send buffer,
  `mt_fd_wait_write()` parks until the peer drains data.
- **TC-FD-WRITE-003**: another runnable microthread continues executing while one
  microthread waits for write.
- **TC-FD-WRITE-004**: peer close wakes a write waiter with a documented error.
- **TC-FD-WRITE-005**: write wait unregisters after wake.
- **TC-FD-WRITE-006**: repeated write wait / write / drain cycles work.
- **TC-FD-WRITE-007**: multiple write waiters on the same fd are rejected or
  handled according to a documented policy.
- **TC-FD-WRITE-008**: write readiness is not lost if the peer drains just before
  the waiter registers.
- **TC-FD-WRITE-009**: write readiness is not lost if the peer drains concurrently
  with waiter registration.
- **TC-FD-WRITE-010**: write wait can be cancelled safely.

## 6. Combined Event Waits

- **TC-FD-WAIT-001**: waiting for read or write returns the event that became
  ready.
- **TC-FD-WAIT-002**: when both read and write are ready, ready event mask reports
  both if the API supports masks.
- **TC-FD-WAIT-003**: waiting for read+write can be woken by read readiness alone.
- **TC-FD-WAIT-004**: waiting for read+write can be woken by write readiness alone.
- **TC-FD-WAIT-005**: invalid event masks are rejected.
- **TC-FD-WAIT-006**: event flags map consistently across epoll, kqueue, and poll.
- **TC-FD-WAIT-007**: error/hangup events wake waiters even when the requested
  event was only read or only write.
- **TC-FD-WAIT-008**: spurious backend events do not corrupt waiter state.
- **TC-FD-WAIT-009**: edge-triggered backends, if used, fully preserve documented
  level-triggered API semantics.
- **TC-FD-WAIT-010**: duplicate registration for the same fd/event pair follows a
  documented policy.

## 7. Timeout Semantics

- **TC-FD-TIMEOUT-001**: read wait with no peer activity times out.
- **TC-FD-TIMEOUT-002**: write wait times out when send buffer remains full.
- **TC-FD-TIMEOUT-003**: zero timeout behaves as a nonblocking readiness poll.
- **TC-FD-TIMEOUT-004**: ready fd wins over timeout when already ready.
- **TC-FD-TIMEOUT-005**: readiness before deadline wins over timeout.
- **TC-FD-TIMEOUT-006**: timeout before readiness returns timeout and unregisters
  fd interest.
- **TC-FD-TIMEOUT-007**: event arriving immediately after timeout does not wake the
  timed-out task twice.
- **TC-FD-TIMEOUT-008**: repeated timeout waits do not leak timer nodes or fd
  waiters.
- **TC-FD-TIMEOUT-009**: timeout works while other microthreads are running on
  other workers.
- **TC-FD-TIMEOUT-010**: fd wait timeout and `mt_sleep_ms()` timers coexist in the
  same runtime.
- **TC-FD-TIMEOUT-011**: very large timeout values do not overflow deadline math.
- **TC-FD-TIMEOUT-012**: timeout wait prevents false deadlock while pending.

## 8. Read and Write Convenience Wrappers

- **TC-FD-RW-001**: `mt_fd_read()` returns available bytes without blocking the OS
  worker.
- **TC-FD-RW-002**: `mt_fd_read()` parks until data is readable when the fd would
  otherwise return `EAGAIN`/`EWOULDBLOCK`.
- **TC-FD-RW-003**: `mt_fd_read()` returns EOF when peer closes cleanly.
- **TC-FD-RW-004**: `mt_fd_read()` distinguishes timeout from EOF.
- **TC-FD-RW-005**: `mt_fd_write()` writes immediately when fd is writable.
- **TC-FD-RW-006**: `mt_fd_write()` parks and resumes after partial writes when the
  socket buffer fills.
- **TC-FD-RW-007**: `mt_fd_write()` handles short writes correctly.
- **TC-FD-RW-008**: `mt_fd_write()` returns timeout when progress cannot be made
  before deadline.
- **TC-FD-RW-009**: read/write wrappers preserve `errno` or document how errors
  are reported.
- **TC-FD-RW-010**: zero-length reads/writes follow POSIX-compatible documented
  behavior.
- **TC-FD-RW-011**: large transfer over socketpair completes without data
  corruption.
- **TC-FD-RW-012**: concurrent reader and writer microthreads transfer exact byte
  counts.
- **TC-FD-RW-013**: wrappers reject invalid fds and null buffers where applicable.
- **TC-FD-RW-014**: wrappers remain safe when the waiting task is cancelled.

## 9. Accept and TCP Socket Integration

- **TC-NET-ACCEPT-001**: TCP listener creation succeeds on loopback with port 0.
- **TC-NET-ACCEPT-002**: listener socket is nonblocking.
- **TC-NET-ACCEPT-003**: `mt_net_accept()` parks when no client is pending.
- **TC-NET-ACCEPT-004**: `mt_net_accept()` wakes when a loopback client connects.
- **TC-NET-ACCEPT-005**: accepting many clients does not lose or duplicate
  connections.
- **TC-NET-ACCEPT-006**: accept timeout works.
- **TC-NET-ACCEPT-007**: accept waiter can be cancelled.
- **TC-NET-ACCEPT-008**: closing listener wakes accept waiter.
- **TC-NET-ACCEPT-009**: accepted socket is nonblocking.
- **TC-NET-ACCEPT-010**: listener cleanup closes the fd exactly once.
- **TC-NET-ACCEPT-011**: multiple workers can accept from the same listener if the
  API documents that as supported; otherwise duplicate accept waiters are rejected.
- **TC-NET-ACCEPT-012**: failed bind/listen paths clean up sockets and preserve
  errno/documented error codes.

## 10. Cross-Worker I/O Behavior

- **TC-IO-MT-001**: microthread waiting for read on worker A is woken by an event
  processed by worker B or the backend polling worker.
- **TC-IO-MT-002**: microthread waiting for write on worker A is woken while other
  workers continue executing tasks.
- **TC-IO-MT-003**: fd readiness wake queues the task exactly once.
- **TC-IO-MT-004**: cancelled fd waiter is not later woken by readiness.
- **TC-IO-MT-005**: timed-out fd waiter is not later woken by readiness.
- **TC-IO-MT-006**: fd close/shutdown racing with readiness does not double-wake.
- **TC-IO-MT-007**: external OS thread may submit new microthreads while fd waiters
  are pending.
- **TC-IO-MT-008**: many fds becoming ready concurrently are all processed.
- **TC-IO-MT-009**: idle workers wake when the only pending event is fd readiness.
- **TC-IO-MT-010**: runtime does not report deadlock while fd waiters are pending.
- **TC-IO-MT-011**: runtime exits cleanly after the last fd waiter completes.
- **TC-IO-MT-012**: fd backend does not hold runtime locks while making blocking
  system calls.

## 11. Interaction With Channels, Select, Join, and Cancellation

- **TC-IO-INTEG-001**: channel send/recv continues working while fd waiters are
  pending.
- **TC-IO-INTEG-002**: `mt_select()` over channels continues working while fd
  waiters are pending.
- **TC-IO-INTEG-003**: fd wait timeout and channel select timeout use compatible
  timer cleanup.
- **TC-IO-INTEG-004**: joining a task blocked on fd readiness works after the task
  completes.
- **TC-IO-INTEG-005**: cancelling a task blocked on fd readiness wakes joiners.
- **TC-IO-INTEG-006**: a task blocked on fd readiness can hold a task handle
  without leaking it.
- **TC-IO-INTEG-007**: channel close/destroy does not affect unrelated fd waiters.
- **TC-IO-INTEG-008**: fd close does not affect unrelated channel waiters.
- **TC-IO-INTEG-009**: task cancellation around fd wait is cooperative and returns
  a documented status.
- **TC-IO-INTEG-010**: `mt_task_cancelled()` remains observable after fd wait
  cancellation.
- **TC-IO-INTEG-011**: shutdown wakes fd waiters, channel waiters, select waiters,
  sleep waiters, and join waiters safely.
- **TC-IO-INTEG-012**: one task can coordinate fd readiness with channels, such as
  reader task sends bytes into a channel.

## 12. Descriptor Close and Lifetime Races

- **TC-FD-CLOSE-001**: `mt_fd_close(fd)` closes the descriptor and unregisters any
  backend interest.
- **TC-FD-CLOSE-002**: closing an fd with a read waiter wakes the waiter.
- **TC-FD-CLOSE-003**: closing an fd with a write waiter wakes the waiter.
- **TC-FD-CLOSE-004**: closing an fd with both read and write waiters follows the
  documented duplicate-waiter policy.
- **TC-FD-CLOSE-005**: closing an fd that has no waiters succeeds.
- **TC-FD-CLOSE-006**: closing the same fd twice returns a documented error and
  does not corrupt backend state.
- **TC-FD-CLOSE-007**: raw `close(fd)` by user code while a task is waiting is
  documented as unsupported or handled safely if supported.
- **TC-FD-CLOSE-008**: fd number reuse after close does not deliver old readiness
  to a new owner.
- **TC-FD-CLOSE-009**: close racing with timeout wakes exactly once.
- **TC-FD-CLOSE-010**: close racing with cancellation wakes exactly once.
- **TC-FD-CLOSE-011**: close racing with readiness wakes exactly once.
- **TC-FD-CLOSE-012**: `mt_shutdown()` closes backend internals without closing
  user-owned fds unless documented.

## 13. Backend-Specific Tests

### Linux epoll

- **TC-IO-EPOLL-001**: epoll backend is selected on Linux when available.
- **TC-IO-EPOLL-002**: epoll add/modify/delete paths work for read waiters, write waiters, and a coexisting read+write waiter pair on one fd.
- **TC-IO-EPOLL-003**: epoll add/modify/delete paths work for write waiters.
- **TC-IO-EPOLL-004**: `EPOLLERR` and `EPOLLHUP` wake waiters.
- **TC-IO-EPOLL-005**: eventfd/pipe wakeup mechanism interrupts epoll wait during
  shutdown or new timer/work submission.
- **TC-IO-EPOLL-006**: epoll backend handles many fds without O(n) polling in the
  common readiness path.
- **TC-IO-EPOLL-007**: epoll fd is closed during shutdown.

### macOS/BSD kqueue

- **TC-IO-KQUEUE-001**: kqueue backend is selected on macOS/BSD when available.
- **TC-IO-KQUEUE-002**: EVFILT_READ registration/deletion works.
- **TC-IO-KQUEUE-003**: EVFILT_WRITE registration/deletion works.
- **TC-IO-KQUEUE-004**: EOF/error flags wake waiters.
- **TC-IO-KQUEUE-005**: EVFILT_USER or pipe wakeup interrupts kqueue wait during
  shutdown or new timer/work submission.
- **TC-IO-KQUEUE-006**: kqueue backend works with the macOS assembly context
  backend.
- **TC-IO-KQUEUE-007**: kqueue descriptor is closed during shutdown.

### poll fallback

- **TC-IO-POLL-001**: poll backend can be forced at build time for portability
  tests.
- **TC-IO-POLL-002**: poll backend handles read readiness.
- **TC-IO-POLL-003**: poll backend handles write readiness.
- **TC-IO-POLL-004**: poll backend handles hangup/error readiness.
- **TC-IO-POLL-005**: poll backend rebuilds its fd array safely when waiters are
  added/removed concurrently.
- **TC-IO-POLL-006**: poll backend does not busy-spin when no fds are ready.
- **TC-IO-POLL-007**: poll backend documents lower scalability than epoll/kqueue.

## 14. Fault Injection and Cleanup

- **TC-IO-ERR-001**: backend allocation failure during init cleans up and returns
  `MT_ERR_BACKEND`.
- **TC-IO-ERR-002**: waiter allocation failure leaves the task runnable or returns
  a documented error without leaking.
- **TC-IO-ERR-003**: timer allocation failure during fd wait with timeout cleans
  up fd interest.
- **TC-IO-ERR-004**: backend registration failure cleans up waiter state and
  returns `MT_ERR_BACKEND`.
- **TC-IO-ERR-005**: backend deregistration failure is handled or documented.
- **TC-IO-ERR-006**: socketpair/listener helper failure paths close all fds.
- **TC-IO-ERR-007**: partial write followed by error returns documented partial or
  error semantics.
- **TC-IO-ERR-008**: EINTR from backend wait is retried or handled according to
  documented policy.
- **TC-IO-ERR-009**: EAGAIN/EWOULDBLOCK loops eventually park instead of spinning.
- **TC-IO-ERR-010**: fault injection counters reset between runtime cycles.

## 15. Memory, Descriptor, and Resource Counters

- **TC-IO-MEM-001**: fd waiter allocation/free counters balance after read wake.
- **TC-IO-MEM-002**: fd waiter allocation/free counters balance after write wake.
- **TC-IO-MEM-003**: counters balance after timeout.
- **TC-IO-MEM-004**: counters balance after cancellation.
- **TC-IO-MEM-005**: counters balance after close.
- **TC-IO-MEM-006**: counters balance after shutdown with pending fd waiters.
- **TC-IO-MEM-007**: backend registration counters balance after repeated waits.
- **TC-IO-MEM-008**: backend file descriptors are not leaked across repeated
  init/shutdown cycles.
- **TC-IO-MEM-009**: large transfer tests leave task/channel/timer/select/fd
  counters balanced.
- **TC-IO-MEM-010**: sanitizer/Valgrind builds report no leaks for v0.7 tests.

## 16. Stress and Race Tests

- **TC-IO-STRESS-001**: many socketpairs each have one reader and one writer
  microthread across multiple workers.
- **TC-IO-STRESS-002**: many clients connect to a loopback listener concurrently.
- **TC-IO-STRESS-003**: random read/write/cancel/timeout/close operations do not
  hang or leak under bounded stress.
- **TC-IO-STRESS-004**: repeated short runtime cycles with fd waiters do not leak
  descriptors.
- **TC-IO-STRESS-005**: ThreadSanitizer reports no data races in fd wait tests
  on context backends that ThreadSanitizer can model. On Linux `ucontext`, the
  checked-in `make io-tsan` target must skip with a clear message because TSan is
  known to be unreliable with `makecontext`/`swapcontext`; ASan/UBSan and stress
  targets still cover the fd I/O path in that configuration.
- **TC-IO-STRESS-006**: AddressSanitizer reports no memory errors in fd wait tests.
- **TC-IO-STRESS-007**: Valgrind reports no definite leaks where available.
- **TC-IO-STRESS-008**: no busy-spin under idle fd waits; CPU use remains bounded
  in practical idle tests.
- **TC-IO-STRESS-009**: transferring many megabytes over socketpairs preserves
  byte order and data integrity.
- **TC-IO-STRESS-010**: stress tests are available from a documented `make` target
  separate from the fast default suite if they are too slow for default testing.

## 17. Minimal Server Examples Before HTTP

- **TC-IO-EXAMPLE-001**: echo-server example compiles.
- **TC-IO-EXAMPLE-002**: echo-server example accepts at least one loopback client.
- **TC-IO-EXAMPLE-003**: echo-server example handles multiple concurrent clients
  with one microthread per connection.
- **TC-IO-EXAMPLE-004**: echo-server example uses MicroThread fd/socket wrappers,
  not raw blocking socket calls inside microthreads.
- **TC-IO-EXAMPLE-005**: example documents graceful shutdown limitations.
- **TC-IO-EXAMPLE-006**: example documents that HTTP parsing is intentionally not
  part of v0.7.

## 18. Documentation and API Contract Tests

- **TC-IO-DOC-001**: README clearly states MicroThread has fd/socket readiness I/O
  only after v0.7 is implemented.
- **TC-IO-DOC-002**: README warns users not to call blocking OS I/O directly inside
  microthreads unless they understand it blocks an OS worker.
- **TC-IO-DOC-003**: API docs state descriptor ownership rules.
- **TC-IO-DOC-004**: API docs state duplicate waiter policy for same fd/event.
- **TC-IO-DOC-005**: API docs state timeout units and special values.
- **TC-IO-DOC-006**: API docs state platform backend selection and fallback.
- **TC-IO-DOC-007**: API docs state cancellation semantics for fd waiters.
- **TC-IO-DOC-008**: examples avoid misleading users into writing blocking HTTP
  servers on top of raw sockets.
- **TC-IO-DOC-009**: changelog/release notes explain that v0.7 is a network I/O
  foundation, not an HTTP server framework.
- **TC-IO-DOC-010**: debug counters are documented as an opt-in diagnostics API
  through `<microthread_debug.h>`, not as the default stable application surface.
- **TC-IO-DOC-011**: API docs expose readable status/error helpers such as
  `mt_task_status_name()` and `mt_strerror()`.

## Acceptance Criteria

v0.7 is complete only when:

- fd readiness wait APIs exist and are documented
- Linux epoll backend works or a documented fallback is selected
- macOS/BSD kqueue backend works or a documented fallback is selected
- poll fallback can be built/tested
- waiting on fd readiness parks only the current microthread, not the OS worker
- read/write/accept wrappers use nonblocking descriptors and runtime fd waits
- timeouts, cancellation, close, shutdown, and descriptor reuse are safe
- fd waiters interact correctly with timers, channels, select, join, and v0.6
  multi-worker scheduling
- resource counters or equivalent tests show no leaked waiters/timers/backend fds
- sanitizer/stress targets cover the fd I/O path
- a minimal echo server example demonstrates the intended usage pattern
- README clearly says HTTP itself is still a later layer


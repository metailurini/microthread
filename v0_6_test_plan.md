# v0.6 Test Plan — Multi-Worker Scheduler and Thread-Safe Core Runtime

v0.6 is a core-runtime release. It adds multiple OS worker threads, thread-safe
scheduling, cross-worker wakeups, and thread-safe runtime entry points. It should
not add networking to the core.

This plan is intentionally stricter than the current implementation. It should be
used as the source of truth for the v0.6 test suite, bug triage, and acceptance.

Assumed previous implemented/core features:

- v0.1: green threads, yield, run queue
- v0.2: sleep/timers and guarded/custom stacks
- v0.3: buffered/unbuffered channels
- v0.4: task handles, join, cooperative cancellation
- v0.5: nonblocking channel operations and channel select

If v0.4 or v0.5 is not implemented when v0.6 starts, those integration cases remain conditional.

## Proposed v0.6 API Scope

```c
int gt_runtime_start(size_t worker_count);
int gt_runtime_workers(void);
int gt_run_workers(size_t worker_count); /* optional convenience */
```

Existing APIs such as `gt_go`, `gt_go_handle`, `gt_yield`, `gt_sleep_ms`,
channels, join, cancellation, and select must work safely across workers.

## Scheduler Policy Under Test

The preferred v0.6 design is local worker queues plus work stealing. A simpler
shared global run queue may be accepted only if documentation explicitly states
that v0.6 implements a shared-queue scheduler rather than work stealing.

Tests are grouped as:

- **Required**: must pass for any v0.6 multi-worker implementation.
- **Policy-specific**: required only when that scheduling policy is claimed.
- **Stress/sanitizer**: may live in slow/manual targets, but must exist before
  declaring full v0.6 acceptance.

## 0. Test Harness Requirements

- **TC-V06-HARNESS-001**: test helpers can run the same scenario with 1, 2, 4,
  and, where practical, 8 workers.
- **TC-V06-HARNESS-002**: test helpers can record the OS thread/worker that ran a
  task without exposing unstable internal implementation details to user code.
- **TC-V06-HARNESS-003**: tests can distinguish task completion, runtime
  deadlock, timeout, invalid state, and cancellation errors.
- **TC-V06-HARNESS-004**: tests have bounded timeouts so a missed wakeup fails the
  test instead of hanging forever.
- **TC-V06-HARNESS-005**: multi-worker tests are repeatable in a loop to catch
  scheduling races.
- **TC-V06-HARNESS-006**: slow/stress tests are separated from the fast default
  suite, but remain easy to run from `make`.
- **TC-V06-HARNESS-007**: sanitizer builds use the same public tests rather than
  private one-off smoke programs.

## 1. Regression Tests

- **TC-V06-REG-001**: all v0.1 tests pass in single-worker mode.
- **TC-V06-REG-002**: all v0.2 tests pass in single-worker mode.
- **TC-V06-REG-003**: all v0.3 tests pass in single-worker mode.
- **TC-V06-REG-004**: implemented v0.4 handle/join/cancellation tests pass in
  single-worker mode.
- **TC-V06-REG-005**: implemented v0.5 select tests pass in single-worker mode.
- **TC-V06-REG-006**: public API preserves old single-worker behavior when worker
  count is 1.
- **TC-V06-REG-007**: old `gt_run()` still works after adding `gt_runtime_start()`.
- **TC-V06-REG-008**: `gt_run_workers(1)` and `gt_runtime_start(1)` behave like
  the documented single-worker runner.
- **TC-V06-REG-009**: all public examples still compile and run.

## 2. Worker Initialization and Lifecycle Tests

- **TC-WORKER-INIT-001**: starting with 1 worker succeeds.
- **TC-WORKER-INIT-002**: starting with N workers succeeds for N > 1.
- **TC-WORKER-INIT-003**: `worker_count == 0` returns `GT_ERR_INVALID` or maps to
  a documented default; undocumented behavior fails.
- **TC-WORKER-INIT-004**: repeated start/shutdown cycles work with 1, 2, and 4
  workers.
- **TC-WORKER-INIT-005**: starting while already running returns `GT_ERR_STATE`.
- **TC-WORKER-INIT-006**: starting from inside a green thread returns
  `GT_ERR_STATE` and does not corrupt the active runtime.
- **TC-WORKER-INIT-007**: worker allocation failure cleans up partially allocated
  worker state.
- **TC-WORKER-INIT-008**: worker thread creation failure cleans up already started
  workers and leaves the runtime reusable.
- **TC-WORKER-INIT-009**: mutex/condition-variable initialization failure cleans
  up and leaves the runtime reusable.
- **TC-WORKER-INIT-010**: `gt_runtime_workers()` returns 0 before start, the
  configured count while running, and 0 again after shutdown/run completion.
- **TC-WORKER-INIT-011**: worker count is stable and visible to all workers while
  the runtime is running.
- **TC-WORKER-INIT-012**: extremely large `worker_count` fails cleanly rather than
  overflowing allocation sizes.
- **TC-WORKER-INIT-013**: starting with no tasks returns the documented success or
  no-work status without leaking worker state.

## 3. Parallel Execution Tests

- **TC-PAR-001**: tasks run across multiple OS worker threads.
- **TC-PAR-002**: each worker can run at least one task when there is enough work.
- **TC-PAR-003**: a broad CPU-yielding workload completes faster or with more
  observed worker participation with multiple workers than with one worker; avoid
  strict timing-only assertions.
- **TC-PAR-004**: no task runs simultaneously on two workers.
- **TC-PAR-005**: task state transitions are atomic and valid under contention.
- **TC-PAR-006**: completed task cleanup happens exactly once.
- **TC-PAR-007**: detached task completion and handled task completion both clean
  up correctly.
- **TC-PAR-008**: a task may yield many times while other workers continue running
  independent tasks.
- **TC-PAR-009**: long-running non-yielding task on one worker does not prevent
  other workers from executing runnable tasks.
- **TC-PAR-010**: runtime does not report completion until all runnable/running
  tasks finish.
- **TC-PAR-011**: runtime does not report deadlock while any worker is actively
  running a task that may make progress.

## 4. Run Queue and Scheduling Policy Tests

### Required for any scheduling policy

- **TC-RUNQ-MT-001**: tasks submitted from the owner thread enter a runnable queue.
- **TC-RUNQ-MT-002**: tasks submitted from worker threads are runnable.
- **TC-RUNQ-MT-003**: tasks submitted while workers are idle wake at least one idle
  worker.
- **TC-RUNQ-MT-004**: run queue operations do not lose tasks under concurrent
  push/pop.
- **TC-RUNQ-MT-005**: run queue operations do not run tasks twice under concurrent
  push/pop.
- **TC-RUNQ-MT-006**: run queue counters balance under stress.
- **TC-RUNQ-MT-007**: a task that yields is requeued exactly once.
- **TC-RUNQ-MT-008**: a task woken from sleep/channel/join/select is requeued
  exactly once.
- **TC-RUNQ-MT-009**: idle workers block on a condition variable or equivalent;
  they do not busy-spin when no work or timers exist.
- **TC-RUNQ-MT-010**: wake broadcasts/signals are not lost when work is submitted
  concurrently with workers going idle.
- **TC-RUNQ-MT-011**: a timer becoming ready wakes workers even if no task submits
  work.
- **TC-RUNQ-MT-012**: scheduling remains fair enough that many ready tasks all
  make progress; no starvation under bounded stress.

### Policy-specific: shared global run queue

- **TC-RUNQ-SHARED-001**: a shared-queue implementation documents that work
  stealing is not used.
- **TC-RUNQ-SHARED-002**: all workers pop from the shared queue under contention
  without losing or duplicating tasks.
- **TC-RUNQ-SHARED-003**: shared queue FIFO behavior is reasonable for tasks that
  do not block, without requiring a strict cross-worker ordering guarantee.
- **TC-RUNQ-SHARED-004**: shared queue lock contention does not deadlock when tasks
  perform nested runtime operations such as `gt_go()` or channel wakeups.

### Policy-specific: local queues plus work stealing

- **TC-RUNQ-STEAL-001**: local run queues preserve documented local FIFO/LIFO
  behavior.
- **TC-RUNQ-STEAL-002**: idle worker steals from busy worker.
- **TC-RUNQ-STEAL-003**: global queue fallback works when local queues are empty.
- **TC-RUNQ-STEAL-004**: work stealing does not lose tasks.
- **TC-RUNQ-STEAL-005**: work stealing does not run tasks twice.
- **TC-RUNQ-STEAL-006**: stealing from a worker concurrently pushing/yielding is
  race-free.
- **TC-RUNQ-STEAL-007**: stealing policy avoids starvation across workers.

## 5. Concurrent `gt_go()` and Task Creation Tests

- **TC-GO-MT-001**: many external OS threads call `gt_go()` concurrently before
  the runtime starts.
- **TC-GO-MT-002**: many external OS threads call `gt_go()` while the runtime is
  already running.
- **TC-GO-MT-003**: many green threads call `gt_go()` concurrently from different
  workers.
- **TC-GO-MT-004**: task IDs remain unique under concurrent creation.
- **TC-GO-MT-005**: allocation failure under concurrent creation does not corrupt
  queues.
- **TC-GO-MT-006**: detached and handled task creation coexist under concurrency.
- **TC-GO-MT-007**: failed task creation does not leak stacks, guards, task
  objects, handles, or queue nodes.
- **TC-GO-MT-008**: task creation after runtime completion behaves according to
  the documented lifecycle policy.
- **TC-GO-MT-009**: task creation racing with runtime shutdown/completion either
  succeeds and runs or fails with documented error; no lost tasks.
- **TC-GO-MT-010**: recursive task creation from many workers terminates and
  preserves exact task counts.

## 6. Yield and Context Safety Tests

- **TC-YIELD-MT-001**: tasks yield repeatedly across multiple workers.
- **TC-YIELD-MT-002**: yielded task resumes on same or different worker according
  to documented policy.
- **TC-YIELD-MT-003**: stack locals survive yield across workers.
- **TC-YIELD-MT-004**: floating-point/register state survives context switches
  under multi-worker load.
- **TC-YIELD-MT-005**: task migration does not invalidate stack metadata or guard
  metadata.
- **TC-YIELD-MT-006**: yield outside a green thread remains safe and returns the
  documented status.
- **TC-YIELD-MT-007**: yielding while holding no runtime lock cannot deadlock other
  workers.
- **TC-YIELD-MT-008**: cancellation observed around yield leaves the task in a
  valid terminal state.
- **TC-YIELD-MT-009**: stack overflow guard behavior still works in a worker thread
  when guard stacks are enabled.
- **TC-YIELD-MT-010**: custom stack allocator/free hooks remain thread-safe or are
  documented as externally synchronized.

## 7. Timer Tests Under Multiple Workers

- **TC-TIMER-MT-001**: sleeping tasks wake on time with multiple workers.
- **TC-TIMER-MT-002**: many workers sleep/wake tasks concurrently.
- **TC-TIMER-MT-003**: timer heap or timer queues are thread-safe.
- **TC-TIMER-MT-004**: earliest timer wakes an idle worker.
- **TC-TIMER-MT-005**: cancelling a sleeping task removes/wakes it safely.
- **TC-TIMER-MT-006**: shutdown with many sleeping tasks is safe.
- **TC-TIMER-MT-007**: multiple sleeping tasks with the same deadline all wake.
- **TC-TIMER-MT-008**: staggered timers wake in nondecreasing deadline order where
  the API promises ordering, or at least no earlier-than-deadline wake where it
  does not.
- **TC-TIMER-MT-009**: zero-duration sleep yields or returns immediately according
  to documented policy under multiple workers.
- **TC-TIMER-MT-010**: sleeping tasks racing with external task submission do not
  cause false deadlock.
- **TC-TIMER-MT-011**: timer allocation failure leaves the task runnable or fails
  with documented error without leaking.
- **TC-TIMER-MT-012**: timer removal for cancellation/select timeout cleanup is
  idempotent under races.

## 8. Channel Tests Under Multiple Workers

- **TC-CHAN-MT-001**: sender on worker A wakes receiver on worker B.
- **TC-CHAN-MT-002**: receiver on worker A wakes sender on worker B.
- **TC-CHAN-MT-003**: buffered channel operations are thread-safe.
- **TC-CHAN-MT-004**: unbuffered rendezvous completes exactly once.
- **TC-CHAN-MT-005**: closing a channel wakes send and receive waiters on all
  workers.
- **TC-CHAN-MT-006**: destroying a channel with select waiters on multiple workers
  is safe and wakes them with documented status.
- **TC-CHAN-MT-007**: destroying a channel with plain blocked send/recv waiters
  preserves the documented v0.3/v0.6 policy.
- **TC-CHAN-MT-008**: channel memory counters balance under multi-worker stress.
- **TC-CHAN-MT-009**: many producers/many consumers preserve data integrity.
- **TC-CHAN-MT-010**: buffered channel close allows receivers to drain buffered
  values across workers before reporting closed.
- **TC-CHAN-MT-011**: send racing with close returns success only if the value is
  actually delivered/buffered; otherwise returns closed.
- **TC-CHAN-MT-012**: receive racing with close returns a value only if a value was
  actually available/delivered; otherwise returns closed.
- **TC-CHAN-MT-013**: try-send/try-recv remain nonblocking and thread-safe under
  concurrent senders/receivers.
- **TC-CHAN-MT-014**: FIFO order is preserved for buffered channel values under
  concurrent producers where the API promises FIFO by enqueue order.
- **TC-CHAN-MT-015**: waiter queues are not corrupted when close/cancel/destroy
  races with wakeups.
- **TC-CHAN-MT-016**: no lost wakeup when a sender/receiver arrives concurrently
  with a peer parking.
- **TC-CHAN-MT-017**: channel operations from outside green threads return
  documented errors and do not corrupt channel state.
- **TC-CHAN-MT-018**: channel destroy racing with concurrent try operations is safe
  under the documented ownership/lifetime policy.

## 9. Join and Cancellation Under Multiple Workers

- **TC-JOIN-MT-001**: joiner on worker A wakes when target completes on worker B.
- **TC-JOIN-MT-002**: multiple joiners on different workers all wake.
- **TC-JOIN-MT-003**: cancellation requested from worker A is observed by task on
  worker B.
- **TC-JOIN-MT-004**: cancelling sleeping/channel-waiting/select-waiting tasks
  across workers is safe.
- **TC-JOIN-MT-005**: handle release from different workers is safe under the
  reference-counting policy.
- **TC-JOIN-MT-006**: join/cancel stress has no leaks or double-frees.
- **TC-JOIN-MT-007**: joining an already-completed task from another worker returns
  immediately with the stored result/status.
- **TC-JOIN-MT-008**: cancelling an already-completed task is a documented no-op or
  returns documented status.
- **TC-JOIN-MT-009**: cancelling the same task concurrently from many workers is
  idempotent.
- **TC-JOIN-MT-010**: releasing the last handle reference while another worker is
  completing the task is race-free.
- **TC-JOIN-MT-011**: join waiters are cleaned up if the joining task is cancelled.
- **TC-JOIN-MT-012**: join cycles or self-join return documented errors and do not
  deadlock.
- **TC-JOIN-MT-013**: task return values/results are visible to joiners on other
  workers with correct memory ordering.

## 10. Select Under Multiple Workers

- **TC-SELECT-MT-001**: select receive case wakes when sender runs on another
  worker.
- **TC-SELECT-MT-002**: select send case wakes when receiver runs on another
  worker.
- **TC-SELECT-MT-003**: select timeout wakes while all workers are otherwise idle.
- **TC-SELECT-MT-004**: default case remains immediate and does not park.
- **TC-SELECT-MT-005**: exactly one selected case wins under racing workers.
- **TC-SELECT-MT-006**: losing select registrations are removed safely.
- **TC-SELECT-MT-007**: channel close wakes select waiters on other workers.
- **TC-SELECT-MT-008**: channel destroy wakes select waiters on other workers with
  documented status.
- **TC-SELECT-MT-009**: select stress does not leak waiter objects.
- **TC-SELECT-MT-010**: select send case reports success only when the value was
  actually accepted.
- **TC-SELECT-MT-011**: select receive case reports closed correctly for closed and
  drained channels.
- **TC-SELECT-MT-012**: timeout losing to a channel wake is cancelled/ignored and
  cannot later wake the same task again.
- **TC-SELECT-MT-013**: channel wake losing to timeout is unregistered and cannot
  later wake the same task again.
- **TC-SELECT-MT-014**: cancellation of a task blocked in select unregisters all
  channel and timer waiters.
- **TC-SELECT-MT-015**: many selectors waiting on overlapping channel sets wake at
  most one selector per matching operation unless buffered data permits more.
- **TC-SELECT-MT-016**: immediate ready scan under concurrent close/send/recv
  returns one valid result without corrupting queues.
- **TC-SELECT-MT-017**: duplicate default/timeout validation remains thread-safe.
- **TC-SELECT-MT-018**: select called outside a green thread returns documented
  status and does not allocate waiters.

## 11. Deadlock and Idle Worker Detection

- **TC-DEADLOCK-MT-001**: all live tasks channel-waiting with no possible wake
  returns documented deadlock.
- **TC-DEADLOCK-MT-002**: timers prevent false deadlock.
- **TC-DEADLOCK-MT-003**: select timeouts prevent false deadlock.
- **TC-DEADLOCK-MT-004**: idle workers sleep efficiently instead of busy-spinning.
- **TC-DEADLOCK-MT-005**: new task submission wakes an idle worker.
- **TC-DEADLOCK-MT-006**: runtime does not declare deadlock while external OS
  threads are concurrently submitting tasks under the documented lifecycle policy.
- **TC-DEADLOCK-MT-007**: runtime detects deadlock only after all workers agree no
  runnable, running, or timer-backed tasks exist.
- **TC-DEADLOCK-MT-008**: a blocked join with a runnable target does not count as
  deadlock.
- **TC-DEADLOCK-MT-009**: a blocked select with a future timeout does not count as
  deadlock.
- **TC-DEADLOCK-MT-010**: condition variable wait deadlines track the nearest timer
  and are recomputed after timer insertion/cancellation.

## 12. Shutdown and Runtime Reuse Tests

- **TC-SHUT-MT-001**: shutdown/run completion stops all workers.
- **TC-SHUT-MT-002**: shutdown waits for currently running tasks according to
  documented policy.
- **TC-SHUT-MT-003**: shutdown with runnable tasks pending is safe.
- **TC-SHUT-MT-004**: shutdown with sleeping tasks pending is safe.
- **TC-SHUT-MT-005**: shutdown with channel waiters pending is safe.
- **TC-SHUT-MT-006**: shutdown with join/select waiters pending is safe.
- **TC-SHUT-MT-007**: repeated multi-worker init/run/shutdown cycles do not leak.
- **TC-SHUT-MT-008**: shutdown while external OS threads are attempting `gt_go()`
  follows a documented success/failure policy and does not leak.
- **TC-SHUT-MT-009**: runtime can be reused after a deadlock result.
- **TC-SHUT-MT-010**: runtime can be reused after an error result from worker
  startup failure.
- **TC-SHUT-MT-011**: runtime can be reused after cancellation-heavy workloads.
- **TC-SHUT-MT-012**: all condition variables, mutexes, worker arrays, queues,
  stacks, handles, timers, channels, and select waiters are released or reusable
  after shutdown/run completion.

## 13. Fault-Injection Tests

- **TC-ERR-MT-001**: worker allocation failure cleans up.
- **TC-ERR-MT-002**: OS thread creation failure cleans up.
- **TC-ERR-MT-003**: mutex/condition variable initialization failure cleans up.
- **TC-ERR-MT-004**: run queue allocation failure does not lose already-created
  tasks.
- **TC-ERR-MT-005**: wake notification failure is handled or documented impossible
  for the chosen platform API.
- **TC-ERR-MT-006**: shutdown during fault injection leaves runtime reusable.
- **TC-ERR-MT-007**: stack allocation failure under concurrent task creation is
  safe.
- **TC-ERR-MT-008**: channel allocation failure under concurrent use is safe.
- **TC-ERR-MT-009**: select waiter allocation failure rolls back all partial
  registrations under the runtime lock.
- **TC-ERR-MT-010**: timer allocation failure while many workers are active leaves
  no dangling timer references.
- **TC-ERR-MT-011**: handle allocation/reference failure paths preserve task
  lifetime invariants.
- **TC-ERR-MT-012**: debug/test fault hooks are thread-safe or documented as
  single-thread test-only hooks.

## 14. Memory Counter and Resource Accounting Tests

- **TC-MEM-MT-001**: task allocation/free counters balance after multi-worker
  normal completion.
- **TC-MEM-MT-002**: stack allocation/free counters balance after multi-worker
  normal completion.
- **TC-MEM-MT-003**: timer allocation/free counters balance after sleep, timeout,
  and cancellation workloads.
- **TC-MEM-MT-004**: channel allocation/free counters balance after send/recv,
  close, destroy, and cancellation workloads.
- **TC-MEM-MT-005**: select waiter allocation/free counters balance after normal
  wake, timeout, close, destroy, cancellation, and shutdown.
- **TC-MEM-MT-006**: handle/reference counters balance after join/release/cancel
  workloads.
- **TC-MEM-MT-007**: worker runtime allocations balance after repeated start/run
  cycles.
- **TC-MEM-MT-008**: counters remain correct under concurrent increments and
  decrements; sanitizer must not report races in debug counters.

## 15. Race and Sanitizer Tests

- **TC-RACE-MT-001**: ThreadSanitizer run has no data races in scheduler state.
- **TC-RACE-MT-002**: ThreadSanitizer run has no data races in channel state.
- **TC-RACE-MT-003**: ThreadSanitizer run has no data races in task lifecycle and
  handle state.
- **TC-RACE-MT-004**: ThreadSanitizer run has no data races in select waiter and
  timer state.
- **TC-RACE-MT-005**: ThreadSanitizer run has no data races in debug counters or
  test hooks.
- **TC-RACE-MT-006**: ASan/UBSan still pass where supported.
- **TC-RACE-MT-007**: Valgrind/Helgrind/DRD-style checks pass where available.
- **TC-RACE-MT-008**: sanitizer tests include external OS-thread `gt_go()` and
  internal green-thread `gt_go()` workloads.

## 16. Stress Tests

- **TC-STRESS-MT-001**: 100,000 short tasks over all workers.
- **TC-STRESS-MT-002**: 10,000 tasks each yielding 100 times.
- **TC-STRESS-MT-003**: 10,000 sleeping tasks with staggered deadlines.
- **TC-STRESS-MT-004**: 100 producers and 100 consumers over channels.
- **TC-STRESS-MT-005**: 10,000 join/cancel operations.
- **TC-STRESS-MT-006**: mixed workload of yield, sleep, channel, join, and select
  tasks completes.
- **TC-STRESS-MT-007**: repeated multi-worker start/shutdown cycles.
- **TC-STRESS-MT-008**: concurrent external task submission while workers drain a
  large workload.
- **TC-STRESS-MT-009**: close/destroy/cancel/select race stress with bounded
  randomized scheduling.
- **TC-STRESS-MT-010**: stress suite runs with 2, 4, and 8 workers where practical.
- **TC-STRESS-MT-011**: stress suite can run under sanitizer with reduced counts.

## 17. Documentation and Contract Tests

- **TC-DOC-MT-001**: README documents whether v0.6 uses shared global queue or
  work stealing.
- **TC-DOC-MT-002**: README documents which public APIs are safe from external OS
  threads.
- **TC-DOC-MT-003**: README documents channel lifetime requirements under
  concurrent close/destroy/use.
- **TC-DOC-MT-004**: README documents runtime lifecycle rules for task creation
  before, during, and after `gt_runtime_start()`.
- **TC-DOC-MT-005**: README documents whether tasks may migrate between workers.
- **TC-DOC-MT-006**: README documents sanitizer/stress targets and known platform
  limitations.
- **TC-DOC-MT-007**: public headers match the documented API and error codes.

## Acceptance Criteria

v0.6 is accepted when:

- all earlier implemented tests pass in single-worker mode
- required multi-worker tests pass with 2, 4, and 8 workers where practical
- either shared-queue policy tests or work-stealing policy tests pass, matching
  the documented scheduler policy
- ThreadSanitizer or equivalent race testing shows no scheduler/channel/timer/
  select/handle/debug-counter races
- ASan/UBSan pass where supported
- no task is lost or run twice under stress
- no blocked waiter can be woken twice under close/destroy/cancel/timeout races
- all resource counters balance after normal, error, cancellation, timeout,
  close/destroy, and shutdown paths
- runtime can be started, completed, and reused repeatedly after success, error,
  and deadlock paths
- documentation clearly states what is and is not thread-safe for user code
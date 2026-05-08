# Test Coverage Notes

## Current implemented version

The implemented mainstream runtime is v0.6. Socket/network I/O was removed from the core runtime and is intentionally not covered by the main test suite.

## v0.1 coverage

The automated suite in `tests/test_v0_1.c` covers the v0.1 scheduler/yield/task-lifecycle plan, including initialization, task creation, run loop behavior, yield/resume, argument passing, stack preservation, fairness, error paths, edge cases, stress cases, backend smoke tests, and memory/resource checks.

## v0.2 coverage

The automated suite in `tests/test_v0_2.c` follows `v0_2_test_plan.md` and covers configurable stacks, guard-page metadata, guard-disabled fallback, sleep/timer behavior, scheduler integration with timers, error/fault-injection paths, memory counters, and stress cases.

Additional targets:

- `make guard-test` covers the expected-crash guard-page overflow subprocess test on Unix-like platforms.
- `make guard-disabled-test` covers the explicit no-guard fallback configuration.

## v0.3 coverage

The automated suite in `tests/test_v0_3.c` follows `v0_3_test_plan.md` and covers cooperative channel behavior:

- channel creation and invalid arguments
- buffered send/receive, FIFO behavior, wraparound, full-buffer blocking
- unbuffered sender/receiver rendezvous
- close behavior and waiter wakeups
- destroy behavior and shutdown safety
- scheduler integration and channel deadlock detection
- lifecycle cleanup and repeated runtime cycles
- data copying for ints, structs, pointers, and larger values
- channel allocation/buffer fault injection
- memory counter balance
- stress cases for producer/consumer and many-waiter scenarios

## v0.4 coverage

The automated suite in `tests/test_v0_4.c` follows `v0_4_test_plan.md` and covers the core-runtime handle/join/cancellation behavior:

- v0.1/v0.2/v0.3 regression behavior after handle support
- `gt_go_handle()` and `gt_go_handle_with_stack()` creation paths
- invalid handle creation inputs and handle/task/stack/context fault injection
- handled and detached task coexistence
- join of normal, yielding, sleeping, channel-send-blocked, and channel-recv-blocked tasks
- join after completion, join outside a green thread, null join, self-join, multiple joiners, chained joins, and cyclic join deadlock detection
- task status for READY, RUNNING, SLEEPING, WAITING_CHAN, WAITING_JOIN, DONE, and CANCELLED
- cooperative cancellation of READY, RUNNING/self-cancelling, sleeping, channel-send-waiting, channel-recv-waiting, and join-waiting tasks
- repeated/null cancellation behavior
- handle release after completion, release before completion, null release, and cancelled-task release
- scheduler integration for join waiters, ready peers while joining, sleep/channel interaction, and join deadlock cleanup
- lifecycle/shutdown cleanup for handled sleepers, channel waiters, and pending joiners
- handle allocation/free counter balance
- practical stress tests for many handled tasks, yielding tasks, and many joiners on one target

Non-executable or intentionally documented-only v0.4 cases:

- Status after `gt_task_handle_release()` is invalid because the handle may be freed; tests do not dereference released handles.
- Double release is invalid because the second call may use a freed pointer; this is documented rather than executed as a normal test.
- Joining/cancelling through corrupted or raw internal task pointers is unsupported because task internals are opaque.
- Cancellation is cooperative. CPU-bound tasks that never yield cannot be preempted by the single-threaded runtime; tests cover the non-preemptive contract without relying on unsafe asynchronous cancellation.
- Join-waiter allocation failure is not applicable to the current implementation because join waiters are intrusive task nodes and do not allocate separately.
- Windows Fiber backend coverage requires running the same suite on Windows.

## v0.5 coverage

The suite in `tests/test_v0_5.c` follows `v0_5_test_plan.md` and covers:

- `TC-V05-REG-001` through `TC-V05-REG-004`: `make test` runs the v0.1, v0.2, v0.3, and implemented v0.4 suites before the v0.5 suite.
- `TC-TRY-SEND-*`: buffered/unbuffered success, would-block behavior, closed channels, null validation, and outside-green-thread immediate-only behavior.
- `TC-TRY-RECV-*`: buffered/unbuffered success, would-block behavior, closed buffered draining, closed empty channels, and null validation.
- `TC-SEL-IMM-*`: ready buffered receive/send, ready unbuffered sender/receiver, exactly-one winner behavior, selected-index reporting, and losing-case preservation.
- `TC-SEL-BLOCK-*`: parking, sender/receiver wakeups, one-at-a-time selector wakeups, losing-case unregister, coexistence with normal channel waiters, and run-queue state while blocked.
- `TC-SEL-DEFAULT-*` and `TC-SEL-TIMEOUT-*`: deterministic default behavior, duplicate rejection, zero timeout, timeout wakeup, channel readiness before timeout, timeout unregistering of parked channel cases, and shutdown cleanup while a select timeout is still pending.
- `TC-SEL-CLOSE-*`: close and destroy wakeups for receive/send select waiters, closed buffered drain semantics, closed empty receive semantics, and no double-wake across multi-channel select waiters.
- `TC-SEL-SCHED-*` and `TC-SEL-LIFE-*`: scheduler progress while select is blocked, exactly-once readying, coexistence with sleep/join/channel waiters, deadlock reporting, timeout preventing false deadlock, shutdown cleanup, and repeated init/run/shutdown cycles.
- `TC-SEL-ERR-*` and `TC-SEL-MEM-*`: invalid inputs, select/timer allocation fault injection, registration rollback, and select/timer memory-counter balance across normal, timeout, close, destroy, and shutdown paths.
- `TC-SEL-STRESS-*`: practical stress loops for ready selects, blocking selects woken by producers, 100 green tasks each selecting across 10 channels, and a mixed workload of yield, sleep, channels, handles, and select.

One plan item is covered by the closest behavior exposed by the current public/test API rather than a synthetic internal race: `TC-SEL-ERR-006` verifies safe rollback on select registration allocation failure. The current fault hook fails the next select-waiter allocation, so it exercises rollback from the public API but cannot force a channel destroy exactly mid-registration without adding an internal-only hook.

## v0.6 coverage

The v0.6 public API surface is implemented:

- `gt_runtime_start(size_t worker_count)`
- `gt_runtime_workers(void)`
- `gt_run_workers(size_t worker_count)`

Current implementation provides a pthread-backed multi-worker scheduler on Unix-like platforms. Workers share a mutex-protected global run queue, timer heap, channel wait queues, select waiter queues, and task-handle state. `worker_count == 0` returns `GT_ERR_INVALID`; starting while already running or from inside a green thread returns `GT_ERR_STATE`; `gt_run_workers(n)` is an alias for `gt_runtime_start(n)`.

`tests/test_v0_6.c` now provides checked-in coverage for the required v0.6
multi-worker behavior exposed through the public API:

- `TC-V06-HARNESS-*`: bounded, repeatable public-API scenarios; sanitizer
  support through the same checked-in test binary.
- `TC-V06-REG-*`: old `gt_run()` behavior, `gt_run_workers(1)`, and v0.1-v0.5
  regression coverage through the default `make test` target.
- `TC-WORKER-INIT-*`: one-worker and multi-worker starts, zero-worker invalid
  input, repeated no-work starts, `gt_runtime_workers()` before/during/after a
  run, and `gt_runtime_start()` misuse from inside a green thread.
- `TC-PAR-*`: many runnable tasks execute across more than one OS worker and
  complete exactly once.
- `TC-RUNQ-MT-*` and `TC-RUNQ-SHARED-*`: owner-thread task submission,
  green-thread task submission from multiple workers, external OS-thread
  submission while the runtime is active, idle-worker wakeups, yielded task
  requeueing, and exact task counts under contention.
- `TC-GO-MT-*`: concurrent `gt_go()` before/during active multi-worker runs and
  recursive/green-thread task creation from many workers.
- `TC-YIELD-MT-*`: repeated yield-heavy workloads under multiple workers and
  safe `gt_runtime_start()` rejection from a running task.
- `TC-TIMER-MT-*`: timer-backed anchor tasks, select timeouts, cancellation of a
  sleeping task from another worker, and no false deadlock while timers exist.
- `TC-CHAN-MT-*`: buffered many-producer/many-consumer channels, unbuffered
  rendezvous, cross-worker close wakeups, exact delivery counts, and channel
  counter balance.
- `TC-JOIN-MT-*`: multiple joiners across workers, cancellation of sleeping
  targets from another worker, cancelled join result propagation, and handle
  counter balance.
- `TC-SELECT-MT-*`: receive-select wakeups from another worker, timeout while
  otherwise idle, close wakeup, destroy wakeup, selected-index reporting, and
  select waiter counter balance.
- `TC-DEADLOCK-MT-*` and `TC-SHUT-MT-*`: no false deadlock while timers or
  active external submissions can make progress, runtime reuse after no-work and
  ordinary completion, and post-run worker-count reset.
- `TC-ERR-MT-*`: public fault hooks for task and channel allocation failures with
  runtime reuse afterward.
- `TC-MEM-MT-*`: task, stack, timer, channel, handle, and select allocation/free
  counters balance after v0.6 workloads.
- `TC-RACE-MT-*`: `make tsan` runs the v0.6 public test suite under
  ThreadSanitizer where supported.

Remaining limitations for strict v0.6 acceptance:

- Work-stealing-specific cases (`TC-RUNQ-STEAL-*`) are not applicable to the
  current implementation because v0.6 documents and implements a shared global
  run queue.
- Fault-injection cases that require synthetic pthread allocation/thread/condvar
  creation failures are not directly injectable through the current public test
  hooks.
- Very large stress counts from `TC-STRESS-MT-*` are represented by practical
  default counts in `tests/test_v0_6.c`; larger counts can be added under a slow
  stress profile if needed.

## Build targets

- `make test` runs v0.1, v0.2, v0.3, v0.4, v0.5 plan coverage, v0.6 multi-worker coverage, and the guard-disabled fallback binary.
- `make stress` runs v0.1, v0.2, v0.3, and v0.4 with larger practical counts; v0.5 includes practical stress coverage in its normal suite.
- `make sanitize` runs v0.1, v0.2, v0.3, v0.4, v0.5, and v0.6 plan coverage with AddressSanitizer and UndefinedBehaviorSanitizer where supported.
- `make tsan` runs v0.6 multi-worker coverage with ThreadSanitizer where supported.
- `make valgrind` runs v0.1, v0.2, v0.3, v0.4, v0.5, and v0.6 under Valgrind when Valgrind is installed.
- `make handles-example` builds and runs the v0.4 handle/join/cancellation example.
- `make try-example` builds and runs the v0.5 nonblocking channel example.
- `make select-advanced-example` builds and runs the v0.5 default/send/timeout/close select example.
- `make examples` builds and runs every example program.
- `make guard-test` runs the expected-crash guard-page subprocess test.
- `make guard-disabled-test` runs the explicit no-guard fallback configuration test and the v0.4 handle suite with guard pages disabled.

## Platform backend coverage

Unix-like builds exercise the `ucontext` backend. Windows Fiber backend verification requires running the same suite on Windows.

## Scale note

Some plan examples intentionally use very high task counts. The stress target uses practical default counts for ordinary developer machines while keeping the same behavioral coverage. Exact larger counts can be raised in the test files when running on machines with enough memory and time budget.

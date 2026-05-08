# v0.4 Test Plan — Task Handles, Join, and Cancellation

v0.4 replaces the previously planned socket/I/O work with core green-thread runtime features. Networking is intentionally out of scope for the main runtime.

v0.4 should add task handles and structured task lifecycle operations on top of the v0.1/v0.2/v0.3 runtime:

- create tasks and keep a handle to them
- wait for a task to finish with `mt_join()`
- query task status
- request cooperative cancellation
- cleanly wake joiners when a task exits or is cancelled
- keep existing yield, sleep, and channel behavior unchanged

## Proposed v0.4 API Scope

Exact names may change during implementation, but tests should target this API shape:

```c
typedef struct mt_task_handle mt_task_handle_t;

typedef enum mt_task_status {
    MT_TASK_STATUS_READY,
    MT_TASK_STATUS_RUNNING,
    MT_TASK_STATUS_SLEEPING,
    MT_TASK_STATUS_WAITING_CHAN,
    MT_TASK_STATUS_DONE,
    MT_TASK_STATUS_CANCELLED
} mt_task_status_t;

mt_task_handle_t *mt_go_handle(mt_fn fn, void *arg);
mt_task_handle_t *mt_go_handle_with_stack(mt_fn fn, void *arg, size_t stack_size);

int mt_join(mt_task_handle_t *task);
int mt_task_cancel(mt_task_handle_t *task);
int mt_task_cancelled(void);
int mt_task_status(mt_task_handle_t *task, mt_task_status_t *out_status);
void mt_task_handle_release(mt_task_handle_t *task);
```

The old `mt_go()` and `mt_go_with_stack()` APIs must continue to work.

## API Contract Decisions

- A task handle remains valid until `mt_task_handle_release()` is called.
- `mt_join()` blocks the current microthread until the target task reaches DONE or CANCELLED.
- `mt_join()` outside a running microthread should return a documented error instead of blocking the OS thread.
- Joining an already finished task returns immediately.
- Multiple joiners on the same task are allowed unless implementation deliberately documents one-joiner-only semantics. The preferred behavior is multiple joiners.
- A task cannot join itself; this returns `MT_ERR_STATE`.
- Cancellation is cooperative. `mt_task_cancel()` sets a flag and wakes the task if it is sleeping or channel-waiting.
- A cancelled task must observe cancellation through `mt_task_cancelled()` or through interrupted blocking runtime calls.
- The runtime must not asynchronously destroy a running task stack.
- Handle release does not kill the task. It only drops the user reference.
- A detached task with no user handle is still cleaned by the scheduler when it finishes.

## 1. Regression Tests

- **TC-V04-REG-001**: all v0.1 tests still pass.
- **TC-V04-REG-002**: all v0.2 tests still pass.
- **TC-V04-REG-003**: all v0.3 tests still pass.
- **TC-V04-REG-004**: `mt_go()` still creates detached tasks as before.
- **TC-V04-REG-005**: `mt_go_with_stack()` still honors custom stack sizes.
- **TC-V04-REG-006**: channels still wake senders/receivers correctly after handle support is added.

## 2. Handle Creation Tests

- **TC-HANDLE-CREATE-001**: `mt_go_handle(fn, arg)` returns a non-null handle for a valid task.
- **TC-HANDLE-CREATE-002**: `mt_go_handle(NULL, arg)` fails cleanly.
- **TC-HANDLE-CREATE-003**: `mt_go_handle_with_stack()` accepts valid custom stack sizes.
- **TC-HANDLE-CREATE-004**: `mt_go_handle_with_stack()` rejects too-small stack sizes.
- **TC-HANDLE-CREATE-005**: creating many handled tasks assigns distinct handles.
- **TC-HANDLE-CREATE-006**: handled and detached tasks can coexist.
- **TC-HANDLE-CREATE-007**: task allocation failure returns null and does not leak.
- **TC-HANDLE-CREATE-008**: context creation failure returns null and does not leak.

## 3. Join Tests

- **TC-JOIN-001**: joining a task that returns normally succeeds.
- **TC-JOIN-002**: joining a task that yields before returning succeeds.
- **TC-JOIN-003**: joining a sleeping task waits until it finishes.
- **TC-JOIN-004**: joining a task blocked on channel send succeeds after a receiver wakes it.
- **TC-JOIN-005**: joining a task blocked on channel receive succeeds after a sender wakes it.
- **TC-JOIN-006**: joining an already completed task returns immediately.
- **TC-JOIN-007**: multiple tasks joining the same target all wake when target completes.
- **TC-JOIN-008**: self-join returns `MT_ERR_STATE` and does not deadlock.
- **TC-JOIN-009**: `mt_join(NULL)` returns `MT_ERR_INVALID`.
- **TC-JOIN-010**: `mt_join()` outside a microthread returns the documented error.
- **TC-JOIN-011**: chained joins A waits B, B waits C complete in order.
- **TC-JOIN-012**: cyclic joins are detected as deadlock or return a documented error.

## 4. Status Tests

- **TC-STATUS-001**: newly created handle reports READY before running.
- **TC-STATUS-002**: currently running task can report RUNNING through its own handle.
- **TC-STATUS-003**: sleeping task reports SLEEPING.
- **TC-STATUS-004**: channel-blocked sender reports WAITING_CHAN.
- **TC-STATUS-005**: channel-blocked receiver reports WAITING_CHAN.
- **TC-STATUS-006**: completed task reports DONE.
- **TC-STATUS-007**: cancelled task reports CANCELLED.
- **TC-STATUS-008**: status after handle release is not allowed and is documented invalid.
- **TC-STATUS-009**: `mt_task_status(NULL, out)` returns `MT_ERR_INVALID`.
- **TC-STATUS-010**: `mt_task_status(handle, NULL)` returns `MT_ERR_INVALID`.

## 5. Cancellation Tests

- **TC-CANCEL-001**: cancelling a READY task marks it cancelled before it runs.
- **TC-CANCEL-002**: cancelling a RUNNING task sets the cancellation flag; task exits cooperatively after checking `mt_task_cancelled()`.
- **TC-CANCEL-003**: cancelling a sleeping task wakes it promptly.
- **TC-CANCEL-004**: cancelling a channel-send waiter wakes it with a documented cancellation result.
- **TC-CANCEL-005**: cancelling a channel-receive waiter wakes it with a documented cancellation result.
- **TC-CANCEL-006**: cancelling an already completed task is a no-op or documented error.
- **TC-CANCEL-007**: cancelling a null handle returns `MT_ERR_INVALID`.
- **TC-CANCEL-008**: repeated cancellation is safe.
- **TC-CANCEL-009**: cancellation does not destroy a running stack asynchronously.
- **TC-CANCEL-010**: join on a cancelled task returns the documented cancellation result.

## 6. Handle Release Tests

- **TC-RELEASE-001**: releasing a handle after task completion frees handle resources.
- **TC-RELEASE-002**: releasing a handle before task completion detaches user ownership but task still completes.
- **TC-RELEASE-003**: releasing null is safe or returns a documented error.
- **TC-RELEASE-004**: double release is invalid and should be caught in debug/testing mode if possible.
- **TC-RELEASE-005**: releasing one of multiple references does not wake/join incorrectly if reference counting is implemented.
- **TC-RELEASE-006**: detached tasks do not leak after `mt_run()` completes.
- **TC-RELEASE-007**: handles to cancelled tasks release cleanly.

## 7. Scheduler Integration Tests

- **TC-SCHED-HANDLE-001**: join waiter is removed from run queue while blocked.
- **TC-SCHED-HANDLE-002**: target completion moves join waiters back to run queue exactly once.
- **TC-SCHED-HANDLE-003**: ready tasks continue while another task is blocked in join.
- **TC-SCHED-HANDLE-004**: sleep timers coexist with join waiters.
- **TC-SCHED-HANDLE-005**: channel waiters coexist with join waiters.
- **TC-SCHED-HANDLE-006**: if only impossible joins remain, `mt_run()` returns a documented deadlock error.
- **TC-SCHED-HANDLE-007**: cancellation wakes relevant wait queues without corrupting run queue counts.
- **TC-SCHED-HANDLE-008**: shutdown cleans tasks and handles with pending joiners.

## 8. Lifecycle and Shutdown Tests

- **TC-LIFE-HANDLE-001**: handled task normal return is cleaned by scheduler.
- **TC-LIFE-HANDLE-002**: handled task yielding many times is cleaned after completion.
- **TC-LIFE-HANDLE-003**: handled task sleeping during shutdown is cleaned.
- **TC-LIFE-HANDLE-004**: handled task channel-waiting during shutdown is cleaned.
- **TC-LIFE-HANDLE-005**: joiner waiting during shutdown is cleaned.
- **TC-LIFE-HANDLE-006**: repeated init/run/shutdown cycles with handles remain healthy.
- **TC-LIFE-HANDLE-007**: task creates handled child and joins it.
- **TC-LIFE-HANDLE-008**: task creates detached child while parent handle is joined.

## 9. Error and Fault-Injection Tests

- **TC-ERR-HANDLE-001**: task handle allocation failure is reported cleanly.
- **TC-ERR-HANDLE-002**: join waiter allocation failure is reported cleanly.
- **TC-ERR-HANDLE-003**: cancellation wake allocation failure is handled or documented impossible.
- **TC-ERR-HANDLE-004**: releasing a handle with corrupted/invalid state is guarded in debug mode where practical.
- **TC-ERR-HANDLE-005**: handle counters balance after creation failure.
- **TC-ERR-HANDLE-006**: handle counters balance after join failure.
- **TC-ERR-HANDLE-007**: shutdown after fault injection leaves zero live tasks.

## 10. Memory and Resource Tests

- **TC-MEM-HANDLE-001**: task handle allocation/free counters balance after normal joins.
- **TC-MEM-HANDLE-002**: join waiter allocation/free counters balance after normal joins.
- **TC-MEM-HANDLE-003**: counters balance after cancellation paths.
- **TC-MEM-HANDLE-004**: counters balance after shutdown with pending joiners.
- **TC-MEM-HANDLE-005**: ASan/UBSan pass the handle test suite where supported.
- **TC-MEM-HANDLE-006**: Valgrind reports no leaks when available.

## 11. Stress Tests

- **TC-STRESS-HANDLE-001**: 10,000 handled tasks are created, run, joined, and released.
- **TC-STRESS-HANDLE-002**: 1,000 tasks each yield 100 times before join.
- **TC-STRESS-HANDLE-003**: 1,000 sleeping tasks are joined.
- **TC-STRESS-HANDLE-004**: 1,000 channel-blocked tasks are cancelled.
- **TC-STRESS-HANDLE-005**: 100 tasks join a single target task.
- **TC-STRESS-HANDLE-006**: repeated create/join/release cycles for 1,000 iterations.
- **TC-STRESS-HANDLE-007**: mixed workload of detached tasks, handled tasks, sleep, yield, channels, join, and cancellation completes with no leaks.

## 12. Backend Tests

- **TC-BACKEND-HANDLE-001**: Unix `ucontext` backend passes the full v0.4 handle suite.
- **TC-BACKEND-HANDLE-002**: Windows Fiber backend passes the full v0.4 handle suite on Windows.
- **TC-BACKEND-HANDLE-003**: guard-page-enabled Unix build passes handle suite.
- **TC-BACKEND-HANDLE-004**: guard-page-disabled build passes handle suite.

## 13. Misuse and Documentation Tests

- **TC-MISUSE-HANDLE-001**: keeping raw pointers to task internals is unsupported and not exposed by the API.
- **TC-MISUSE-HANDLE-002**: cancelling does not preempt CPU-bound tasks; non-yielding tasks can still block the runtime.
- **TC-MISUSE-HANDLE-003**: joining from outside a microthread is invalid unless final API explicitly supports a blocking owner-thread join.
- **TC-MISUSE-HANDLE-004**: handle release ownership rules are documented.
- **TC-MISUSE-HANDLE-005**: cancellation is cooperative, not an asynchronous kill.

## Suggested Test Files

```text
tests/test_v0_4.c
```

Optionally split later:

```text
tests/test_task_handles.c
tests/test_join.c
tests/test_cancel.c
tests/test_handle_stress.c
```

## Acceptance Criteria

v0.4 is accepted when:

- all v0.1/v0.2/v0.3 tests still pass
- normal v0.4 handle/join/cancellation tests pass
- stress v0.4 tests pass under `make stress`
- sanitizer tests pass where supported
- handle and join waiter counters balance
- shutdown with pending handled tasks, joiners, sleepers, and channel waiters is safe
- cancellation is cooperative and never frees a running stack asynchronously

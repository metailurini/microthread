# v0.5 Test Plan — Nonblocking Channel Operations and Channel Select

v0.5 is a core-runtime feature release. It does not add networking. It builds on v0.3 channels and the planned v0.4 task-handle work.

Main goals:

- add nonblocking channel operations
- add a channel-only `gt_select()` style API
- support default cases and timeout cases
- keep scheduler, timer, channel, join, and cancellation behavior correct

## Proposed API Scope

```c
int gt_chan_try_send(gt_chan_t *ch, const void *value);
int gt_chan_try_recv(gt_chan_t *ch, void *out);

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

Exact names may change, but v0.5 should remain channel/timer focused.

## 1. Regression Tests

- **TC-V05-REG-001**: all v0.1 tests still pass.
- **TC-V05-REG-002**: all v0.2 tests still pass.
- **TC-V05-REG-003**: all v0.3 tests still pass.
- **TC-V05-REG-004**: all implemented v0.4 core handle tests still pass.

## 2. Try Send Tests

- **TC-TRY-SEND-001**: try-send to buffered channel with space succeeds immediately.
- **TC-TRY-SEND-002**: try-send to full buffered channel returns would-block and does not park.
- **TC-TRY-SEND-003**: try-send to unbuffered channel with waiting receiver succeeds.
- **TC-TRY-SEND-004**: try-send to unbuffered channel without receiver returns would-block.
- **TC-TRY-SEND-005**: try-send to closed channel fails cleanly.
- **TC-TRY-SEND-006**: null channel/value validation follows channel API rules.
- **TC-TRY-SEND-007**: try-send outside a green thread works for immediate-only cases or returns documented error.

## 3. Try Receive Tests

- **TC-TRY-RECV-001**: try-recv from nonempty buffered channel succeeds immediately.
- **TC-TRY-RECV-002**: try-recv from empty open channel returns would-block and does not park.
- **TC-TRY-RECV-003**: try-recv from unbuffered channel with waiting sender succeeds.
- **TC-TRY-RECV-004**: try-recv from unbuffered channel without sender returns would-block.
- **TC-TRY-RECV-005**: try-recv from closed buffered channel drains remaining values.
- **TC-TRY-RECV-006**: try-recv from closed empty channel returns closed.
- **TC-TRY-RECV-007**: null channel/out validation follows channel API rules.

## 4. Select Immediate Tests

- **TC-SEL-IMM-001**: select receive chooses a ready buffered receive case.
- **TC-SEL-IMM-002**: select send chooses a ready buffered send case.
- **TC-SEL-IMM-003**: select receive chooses a ready unbuffered sender case.
- **TC-SEL-IMM-004**: select send chooses a waiting unbuffered receiver case.
- **TC-SEL-IMM-005**: select with multiple ready cases chooses exactly one.
- **TC-SEL-IMM-006**: selected index is written correctly.
- **TC-SEL-IMM-007**: losing cases are not modified beyond documented behavior.

## 5. Select Blocking Tests

- **TC-SEL-BLOCK-001**: select over empty receive cases parks the task.
- **TC-SEL-BLOCK-002**: sender wakes a blocked receive-select task.
- **TC-SEL-BLOCK-003**: receiver wakes a blocked send-select task.
- **TC-SEL-BLOCK-004**: multiple blocked selectors on one channel wake one-at-a-time correctly.
- **TC-SEL-BLOCK-005**: one selector registered on multiple channels unregisters losing cases after wake.
- **TC-SEL-BLOCK-006**: select waiters coexist with normal channel send/recv waiters.
- **TC-SEL-BLOCK-007**: blocked select task is not in run queue until woken.

## 6. Default Case Tests

- **TC-SEL-DEFAULT-001**: default case fires when no channel case is ready.
- **TC-SEL-DEFAULT-002**: default case does not fire when any channel case is ready.
- **TC-SEL-DEFAULT-003**: multiple default cases are rejected or deterministic according to API contract.
- **TC-SEL-DEFAULT-004**: default case outside a green thread follows documented behavior.

## 7. Timeout Case Tests

- **TC-SEL-TIMEOUT-001**: timeout case fires after deadline when no channel case is ready.
- **TC-SEL-TIMEOUT-002**: channel readiness before timeout wins over timeout.
- **TC-SEL-TIMEOUT-003**: zero-timeout behaves like default.
- **TC-SEL-TIMEOUT-004**: multiple timeout cases choose earliest deadline or reject according to API contract.
- **TC-SEL-TIMEOUT-005**: timeout unregisters all channel cases.
- **TC-SEL-TIMEOUT-006**: timeout waiters are cleaned during shutdown.

## 8. Close/Destroy Tests

- **TC-SEL-CLOSE-001**: closing a channel wakes receive-select waiters.
- **TC-SEL-CLOSE-002**: closing a channel wakes send-select waiters with failure.
- **TC-SEL-CLOSE-003**: closed buffered channel select receives remaining buffered values.
- **TC-SEL-CLOSE-004**: closed empty channel select receive reports closed.
- **TC-SEL-CLOSE-005**: destroying a channel wakes relevant select waiters with clean error.
- **TC-SEL-CLOSE-006**: destroying multiple channels referenced by one select waiter does not double-wake.

## 9. Scheduler and Lifecycle Tests

- **TC-SEL-SCHED-001**: ready tasks continue while another task is blocked in select.
- **TC-SEL-SCHED-002**: select wake returns task to run queue exactly once.
- **TC-SEL-SCHED-003**: select waiters, channel waiters, sleeping tasks, and join waiters coexist.
- **TC-SEL-SCHED-004**: impossible select waits are reported as deadlock when no timeout exists.
- **TC-SEL-SCHED-005**: select timeout prevents false deadlock.
- **TC-SEL-LIFE-001**: shutdown cleans blocked select waiters.
- **TC-SEL-LIFE-002**: repeated init/run/shutdown cycles with select remain healthy.

## 10. Error and Fault-Injection Tests

- **TC-SEL-ERR-001**: null cases pointer returns invalid.
- **TC-SEL-ERR-002**: zero case count returns invalid.
- **TC-SEL-ERR-003**: invalid op returns invalid.
- **TC-SEL-ERR-004**: allocation failure for select waiter is handled cleanly.
- **TC-SEL-ERR-005**: timer allocation failure for timeout case is handled cleanly.
- **TC-SEL-ERR-006**: channel destroy during select registration rolls back safely.

## 11. Memory and Stress Tests

- **TC-SEL-MEM-001**: select waiter allocation/free counters balance after normal wake.
- **TC-SEL-MEM-002**: counters balance after timeout.
- **TC-SEL-MEM-003**: counters balance after channel close/destroy.
- **TC-SEL-MEM-004**: counters balance after shutdown with blocked select waiters.
- **TC-SEL-STRESS-001**: 1,000 selects over ready buffered channels.
- **TC-SEL-STRESS-002**: 1,000 blocking selects woken by producers.
- **TC-SEL-STRESS-003**: 100 tasks each selecting over 10 channels.
- **TC-SEL-STRESS-004**: mixed workload of yield, sleep, channels, handles, and select.

## Acceptance Criteria

- all earlier implemented version tests still pass
- try-send/try-recv never park the task
- select wakes exactly once and unregisters all losing cases
- default and timeout semantics are deterministic and documented
- memory counters balance under normal, error, close, destroy, timeout, and shutdown paths

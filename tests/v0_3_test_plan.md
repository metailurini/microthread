# v0.3 Test Plan — Cooperative Channels

v0.3 adds Go-inspired cooperative channels to the v0.1/v0.2 green-thread runtime. The goal of this plan is to verify that channels work correctly for buffered and unbuffered communication, interact safely with the scheduler, wake blocked tasks correctly, handle close/destroy/error cases, and preserve v0.1/v0.2 behavior.

## Scope

Included:

- `mt_chan_create(elem_size, capacity)`
- `mt_chan_send(ch, value)`
- `mt_chan_recv(ch, out)`
- `mt_chan_close(ch)`
- `mt_chan_destroy(ch)`
- `mt_chan_len(ch)`
- `mt_chan_capacity(ch)`
- `mt_chan_is_closed(ch)`
- channel wait queues for blocked senders/receivers
- channel close wakeups
- channel deadlock detection in `mt_run()`
- shutdown cleanup with channel-waiting tasks
- regression compatibility with v0.1/v0.2

Not included yet:

- `select`/multi-channel wait
- multi-OS-thread safety
- cancellation
- timed channel operations
- nonblocking try-send/try-recv APIs

---

## 1. Regression

- **TC-V03-REG-001**: v0.1 tests still pass.
- **TC-V03-REG-002**: v0.2 tests still pass.
- **TC-V03-REG-003**: yield-only tasks retain round-robin behavior after channel code is added.
- **TC-V03-REG-004**: sleep/timer tasks retain ordering after channel code is added.

---

## 2. Channel creation and metadata

- **TC-CHAN-CREATE-001**: create unbuffered channel with `capacity == 0`.
- **TC-CHAN-CREATE-002**: create buffered channel with `capacity > 0`.
- **TC-CHAN-CREATE-003**: reject `elem_size == 0`.
- **TC-CHAN-CREATE-004**: reject size overflow where `elem_size * capacity` would overflow.
- **TC-CHAN-CREATE-005**: channel creation before explicit `mt_init()` auto-initializes the runtime.
- **TC-CHAN-CREATE-006**: `mt_chan_len()` and `mt_chan_capacity()` report correct initial values.
- **TC-CHAN-CREATE-007**: invalid channel allocation failure is handled cleanly through fault injection.
- **TC-CHAN-CREATE-008**: buffered channel buffer allocation failure is handled cleanly through fault injection.

---

## 3. Buffered channel semantics

- **TC-CHAN-BUF-001**: send to channel with free capacity succeeds without blocking.
- **TC-CHAN-BUF-002**: receive from non-empty buffer returns FIFO values.
- **TC-CHAN-BUF-003**: length increases on buffered send and decreases on receive.
- **TC-CHAN-BUF-004**: sender blocks when buffer is full and no receiver is ready.
- **TC-CHAN-BUF-005**: receiver waking a blocked sender moves the sender value into the buffer when capacity allows.
- **TC-CHAN-BUF-006**: many buffered sends/receives preserve FIFO order across wraparound.
- **TC-CHAN-BUF-007**: buffered values remain receivable after close until the buffer is drained.

---

## 4. Unbuffered channel semantics

- **TC-CHAN-UNBUF-001**: send blocks until a receiver arrives.
- **TC-CHAN-UNBUF-002**: receive blocks until a sender arrives.
- **TC-CHAN-UNBUF-003**: sender-first rendezvous transfers the value exactly once.
- **TC-CHAN-UNBUF-004**: receiver-first rendezvous transfers the value exactly once.
- **TC-CHAN-UNBUF-005**: multiple blocked senders are served FIFO.
- **TC-CHAN-UNBUF-006**: multiple blocked receivers are served FIFO.
- **TC-CHAN-UNBUF-007**: stack-local send values remain valid while the sender is blocked.

---

## 5. Close behavior

- **TC-CHAN-CLOSE-001**: closing an open channel succeeds.
- **TC-CHAN-CLOSE-002**: closing an already closed channel returns `MT_ERR_CLOSED`.
- **TC-CHAN-CLOSE-003**: send to closed channel returns `MT_ERR_CLOSED`.
- **TC-CHAN-CLOSE-004**: receive from closed empty channel returns `MT_ERR_CLOSED`.
- **TC-CHAN-CLOSE-005**: close wakes blocked senders with `MT_ERR_CLOSED`.
- **TC-CHAN-CLOSE-006**: close wakes blocked receivers with `MT_ERR_CLOSED`.
- **TC-CHAN-CLOSE-007**: closing a buffered channel does not discard already buffered values.
- **TC-CHAN-CLOSE-008**: `mt_chan_is_closed()` reports state correctly.

---

## 6. Destroy behavior

- **TC-CHAN-DESTROY-001**: destroying an unused channel succeeds.
- **TC-CHAN-DESTROY-002**: destroying a closed/drained channel succeeds.
- **TC-CHAN-DESTROY-003**: destroying a channel with blocked waiters returns `MT_ERR_STATE`.
- **TC-CHAN-DESTROY-004**: destroying `NULL` returns `MT_ERR_INVALID`.
- **TC-CHAN-DESTROY-005**: destroy unregisters channel so repeated runtime shutdown remains safe.

---

## 7. Scheduler integration and deadlock

- **TC-CHAN-SCHED-001**: task waiting on channel does not remain runnable.
- **TC-CHAN-SCHED-002**: waking a channel waiter makes it runnable.
- **TC-CHAN-SCHED-003**: ready tasks continue running while another task is channel-blocked.
- **TC-CHAN-SCHED-004**: `mt_run()` returns `MT_ERR_STATE` when all live tasks are blocked on channels and no timers/runnable tasks can wake them.
- **TC-CHAN-SCHED-005**: closing a channel after a deadlock return wakes waiters and allows a later `mt_run()` to finish.
- **TC-CHAN-SCHED-006**: sleeping tasks can wake later and communicate with channel waiters.
- **TC-CHAN-SCHED-007**: channel operations compose correctly with `mt_yield()`.

---

## 8. Lifecycle and shutdown

- **TC-CHAN-LIFE-001**: tasks blocked on channel are cleaned by `mt_shutdown()`.
- **TC-CHAN-LIFE-002**: channel close from inside a task wakes peers.
- **TC-CHAN-LIFE-003**: channel destroy after `mt_run()` completion succeeds.
- **TC-CHAN-LIFE-004**: shutdown with registered but undestroyed channel does not corrupt runtime.
- **TC-CHAN-LIFE-005**: repeated init/run/shutdown cycles with channels remain healthy.

---

## 9. Error and misuse behavior

- **TC-CHAN-ERR-001**: send with `NULL` channel returns `MT_ERR_INVALID`.
- **TC-CHAN-ERR-002**: send with `NULL` value returns `MT_ERR_INVALID`.
- **TC-CHAN-ERR-003**: receive with `NULL` channel returns `MT_ERR_INVALID`.
- **TC-CHAN-ERR-004**: receive with `NULL` output returns `MT_ERR_INVALID`.
- **TC-CHAN-ERR-005**: blocking send outside a microthread returns `MT_ERR_STATE`.
- **TC-CHAN-ERR-006**: blocking receive outside a microthread returns `MT_ERR_STATE`.
- **TC-CHAN-ERR-007**: nonblocking buffered send outside a microthread succeeds when buffer has space.
- **TC-CHAN-ERR-008**: nonblocking buffered receive outside a microthread succeeds when buffer has data.

---

## 10. Type and data-size behavior

- **TC-CHAN-DATA-001**: channel transfers integers correctly.
- **TC-CHAN-DATA-002**: channel transfers structs correctly.
- **TC-CHAN-DATA-003**: channel transfers pointer values correctly without taking ownership.
- **TC-CHAN-DATA-004**: large element values are copied correctly.
- **TC-CHAN-DATA-005**: modifying sender-side storage after a buffered send does not alter buffered value.

---

## 11. Stress

Default `make test` may use practical counts. `make stress` should enable larger counts.

- **TC-CHAN-STRESS-001**: many producer/consumer transfers through a buffered channel.
- **TC-CHAN-STRESS-002**: many sender/receiver rendezvous transfers through an unbuffered channel.
- **TC-CHAN-STRESS-003**: many channels created, used, destroyed.
- **TC-CHAN-STRESS-004**: many blocked waiters woken by close.
- **TC-CHAN-STRESS-005**: mixed sleep/yield/channel traffic.

---

## 12. Memory/resource behavior

- **TC-CHAN-MEM-001**: channel allocation/free counters balance after destroy.
- **TC-CHAN-MEM-002**: channel buffer allocation/free counters balance after destroy.
- **TC-CHAN-MEM-003**: blocked channel tasks are freed by shutdown.
- **TC-CHAN-MEM-004**: sanitizer/Valgrind targets include v0.3 tests where supported.

---

## 13. Backend/platform smoke coverage

- **TC-CHAN-BACKEND-001**: v0.3 tests pass on Unix `ucontext` backend.
- **TC-CHAN-BACKEND-002**: v0.3 tests pass on Windows Fiber backend when run on Windows.

---

## Expected build targets

- `make test`: v0.1 + v0.2 + v0.3 normal tests.
- `make stress`: v0.1 + v0.2 + v0.3 larger stress profile.
- `make sanitize`: v0.1 + v0.2 + v0.3 sanitizer tests where supported.
- `make valgrind`: v0.1 + v0.2 + v0.3 Valgrind checks when Valgrind is installed.
- `make channels-example`: channel demo still builds and runs.

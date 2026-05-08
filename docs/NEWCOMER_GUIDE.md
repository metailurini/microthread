# Newcomer Guide: Green Threads Runtime v0.6

This guide is written for your current knowledge profile:

- You can read C and follow structs/functions across files.
- You understand buffered and unbuffered channels reasonably well.
- You are newer to scheduling theory, OS-thread concurrency, locks, condition variables, and `select` internals.
- You prefer architecture explanations with text diagrams, pseudocode, and edge cases.

The goal is not to memorize every function. The goal is to build a mental model strong enough that you can open `src/gt.c`, read a test, and understand why the runtime behaves that way.

---

## 1. What this project is

This project is a small stackful green-thread runtime in C.

A green thread is a lightweight task managed by this runtime instead of directly by the operating system. The runtime provides APIs like:

```c
int  gt_init(void);
int  gt_go(gt_fn fn, void *arg);
int  gt_run(void);
int  gt_runtime_start(size_t worker_count);
void gt_yield(void);
void gt_sleep_ms(uint64_t ms);
```

You can think of it as a tiny Go-like runtime, but written in C and intentionally limited in scope.

It supports:

- task creation
- cooperative yielding
- sleeping with timers
- buffered and unbuffered channels
- nonblocking channel operations
- channel `select`
- task handles, join, and cooperative cancellation
- v0.6 multi-worker execution using pthread OS threads

It does **not** provide:

- preemptive interruption of CPU-bound tasks
- network I/O integration
- general-purpose async file/socket polling
- garbage collection

---

## 2. The most important mental model

The runtime manages many green tasks. A task runs until it does one of these things:

1. returns from its function
2. calls `gt_yield()`
3. calls `gt_sleep_ms()`
4. blocks on a channel
5. blocks in `gt_select()`
6. blocks in `gt_join()`
7. is cooperatively cancelled and reaches a runtime operation or checks cancellation

A green task is **not** forcibly stopped at random points. That is the meaning of cooperative scheduling.

Text diagram:

```text
              gt_go(fn, arg)
                   |
                   v
              READY queue
                   |
                   v
               RUNNING
              /   |    \
             /    |     \
        yield   sleep   channel/select/join
          |       |             |
          v       v             v
        READY   TIMER       WAIT QUEUE
          ^       |             |
          |       v             |
          +---- wakeup <--------+
                   |
                   v
                READY
```

A task is runnable only when it is in the ready queue. A blocked task is stored somewhere else: a timer heap, a channel wait queue, a select waiter list, or a join waiter list.

---

## 3. Cooperative vs preemptive scheduling

You answered that cooperative vs preemptive scheduling is still a weak spot, so start here.

### Preemptive scheduling

In a normal OS thread, the operating system may pause your thread at almost any machine instruction and run something else.

That means this can happen:

```text
Thread A starts modifying shared data
OS interrupts Thread A halfway through
Thread B runs and sees half-modified data
```

That is why OS-threaded programs need mutexes, atomics, and condition variables.

### Cooperative scheduling

In a cooperative green-thread runtime, a task gives up control only at known points, such as `gt_yield()`, `gt_sleep_ms()`, channel block, select block, or function return.

That means this does **not** happen inside a single worker:

```text
Task A is interrupted randomly in the middle of ordinary C code
```

But with v0.6, multiple OS workers can run green tasks at the same time. So the project now has both ideas:

```text
Inside one green task: cooperative
Across multiple OS workers: real pthread concurrency
```

That is why v0.6 is much harder than v0.1-v0.5.

---

## 4. Runtime architecture at a glance

The public API is in:

```text
include/gt.h
```

The main implementation is in:

```text
src/gt.c
```

The platform-specific context-switching layer is in:

```text
src/context.h
src/context_ucontext.c
src/context_win_fiber.c
```

The tests are organized by version:

```text
tests/test_v0_1.c    scheduler basics
tests/test_v0_2.c    stack sizes, sleep, timers, guard pages
tests/test_v0_3.c    channels
tests/test_v0_4.c    handles, join, cancellation
tests/test_v0_5.c    try_send/try_recv/select
tests/test_v0_6.c    multi-worker runtime
```

The test plans are:

```text
v0_1_test_plan.md
v0_2_test_plan.md
v0_3_test_plan.md
v0_4_test_plan.md
v0_5_test_plan.md
v0_6_test_plan.md
```

When learning this project, read in this order:

1. `include/gt.h`
2. `examples/basic.c`
3. `examples/channels.c`
4. `examples/select.c`
5. `tests/test_v0_1.c`
6. `tests/test_v0_3.c`
7. `tests/test_v0_5.c`
8. `tests/test_v0_6.c`
9. `src/gt.c`

Do not start with the entire `src/gt.c` file. It is easier after you understand the API and tests.

---

## 5. The scheduler in plain English

The scheduler answers one question repeatedly:

> Which green task should run next?

The simple version is:

```text
while runtime is active:
    move expired timers to ready queue
    take next task from ready queue
    if task exists:
        switch from scheduler context to task context
    else if timers exist:
        wait until nearest timer expires or new work arrives
    else:
        finish or report deadlock
```

In pseudocode:

```c
while (true) {
    wake_expired_timers();

    task = pop_ready_task();
    if (task) {
        current_task = task;
        switch_to_task(task);
        current_task = NULL;
        cleanup_or_requeue_task(task);
        continue;
    }

    if (there_are_sleeping_timers()) {
        wait_for_timer_or_new_ready_task();
        continue;
    }

    if (there_are_blocked_tasks_but_no_future_wakeup()) {
        return GT_ERR_STATE; // deadlock-like state
    }

    return GT_OK;
}
```

This is the core idea behind `gt_run()` and the worker loop used by `gt_runtime_start(n)`.

---

## 6. What `gt_yield()` means

When a green task calls:

```c
gt_yield();
```

it says:

> I am still runnable, but let another ready task run first.

Conceptually:

```c
void gt_yield(void) {
    current_task->state = READY;
    push_ready_queue(current_task);
    switch_back_to_scheduler();
}
```

Important detail: yielding does not destroy the task. It just moves the task to the back of the ready queue.

---

## 7. What `gt_sleep_ms()` means

When a green task calls:

```c
gt_sleep_ms(100);
```

it says:

> I am not runnable until about 100ms from now.

Conceptually:

```c
void gt_sleep_ms(uint64_t ms) {
    current_task->state = SLEEPING;
    add_current_task_to_timer_heap(now + ms);
    switch_back_to_scheduler();
}
```

The task leaves the ready queue. Later, the scheduler notices the timer expired and moves the task back to the ready queue.

Text diagram:

```text
Task calls gt_sleep_ms(100)
        |
        v
  state = SLEEPING
        |
        v
  timer heap: wake at now + 100ms
        |
        v
scheduler runs other tasks
        |
        v
timer expires
        |
        v
task becomes READY again
```

A key edge case: a sleeping task is not a deadlock. If the runtime knows a timer will expire, it should wait for the timer, not report deadlock.

---

## 8. Channels: the practical model

A channel lets tasks communicate by sending typed bytes.

```c
gt_chan_t *ch = gt_chan_create(sizeof(int), capacity);
```

There are two main kinds.

### Buffered channel

A buffered channel has storage.

```text
capacity = 3

send 10 -> [10]
send 20 -> [10, 20]
recv    -> gets 10, buffer becomes [20]
```

If the buffer has space, send can complete immediately.
If the buffer has data, receive can complete immediately.

If the buffer is full, send may block.
If the buffer is empty, receive may block.

### Unbuffered channel

An unbuffered channel has no storage. A sender and receiver must rendezvous.

```text
sender arrives first:
    sender blocks
    receiver arrives later
    value transfers directly
    sender wakes

receiver arrives first:
    receiver blocks
    sender arrives later
    value transfers directly
    receiver wakes
```

Pseudocode for unbuffered send:

```c
send(ch, value):
    if receiver_waiting(ch):
        copy value directly to receiver
        wake receiver
        return OK

    park current task as sender
    switch back to scheduler
    return result after wake
```

Pseudocode for unbuffered receive:

```c
recv(ch, out):
    if sender_waiting(ch):
        copy sender value into out
        wake sender
        return OK

    park current task as receiver
    switch back to scheduler
    return result after wake
```

This answers your Round 2 question 4: when a sender wakes a blocked receiver, the runtime transfers/enqueues the value, marks the receiver runnable, and the scheduler runs it later.

---

## 9. Channel close vs destroy

You said close semantics are still basic, so this is important.

### Close

Closing a channel means:

- future sends fail with closed/error behavior
- receivers may still drain buffered values
- once the buffer is empty, receives report closed
- waiters should wake so they do not remain stuck forever

Buffered close example:

```text
channel buffer before close: [1, 2]
close(channel)
recv -> 1, OK
recv -> 2, OK
recv -> CLOSED
```

Close is a normal lifecycle operation.

### Destroy

Destroy means freeing the channel object/resources. This is more dangerous.

In this project, v0.5/v0.6 behavior includes waking select waiters safely during destroy. Plain blocked send/recv behavior is more conservative because destroying a channel with ordinary waiters can invalidate data paths.

The safe rule for users:

```text
Prefer close first, let tasks drain/wake, then destroy after nobody needs the channel.
```

---

## 10. Nonblocking channel operations

v0.5 added:

```c
int gt_chan_try_send(gt_chan_t *ch, const void *value);
int gt_chan_try_recv(gt_chan_t *ch, void *out);
```

These never park the current task.

That means:

```text
operation can complete now -> GT_OK
operation cannot complete now, but channel is open -> GT_ERR_WOULD_BLOCK
channel is closed -> GT_ERR_CLOSED, depending on operation/state
invalid input -> GT_ERR_INVALID
```

Use these when you want to check channel state without blocking the green task.

---

## 11. Select: choosing among channel operations

`gt_select()` chooses one operation from a list of cases.

Public shape:

```c
typedef struct gt_select_case {
    gt_select_op_t op;
    gt_chan_t *ch;
    void *value;
    uint64_t timeout_ms;
} gt_select_case_t;

int gt_select(gt_select_case_t *cases, size_t count, size_t *selected_index);
```

Case types:

```text
GT_SELECT_RECV      receive from a channel
GT_SELECT_SEND      send to a channel
GT_SELECT_DEFAULT   run immediately if no channel case is ready
GT_SELECT_TIMEOUT   wake after a timeout if nothing else wins first
```

Conceptually:

```c
gt_select(cases):
    for each case:
        if case is immediately ready:
            perform it
            return selected index

    if default case exists:
        return default index

    if timeout is zero:
        return timeout index

    register current task as a select waiter on all channel cases
    if timeout exists:
        register timer too

    park current task
    switch back to scheduler

    when one case wins:
        unregister losing cases
        return winning index/result
```

The hardest part is not picking a winner. The hardest part is cleanup.

When one select case wins, the runtime must remove the task from every other channel wait list. Otherwise the same task could wake twice later.

Text diagram:

```text
Task selects on ch1, ch2, ch3
        |
        v
registered as waiter on all three
        |
        v
ch2 becomes ready
        |
        v
winner = ch2
        |
        +--> remove waiter from ch1
        +--> remove waiter from ch3
        +--> cancel timeout if any
        +--> mark task READY exactly once
```

This is why v0.5 tests spend so much effort on close/destroy/timeouts/double-wakeup cases.

---

## 12. Handles, join, and cancellation

A normal detached task is created with:

```c
gt_go(fn, arg);
```

A handled task is created with:

```c
gt_task_handle_t *h = gt_go_handle(fn, arg);
```

A handle lets another green task:

- join it
- cancel it
- ask for its status
- release the handle when done

### Join

`gt_join(h)` means:

> Park the current green task until the target task finishes.

Pseudocode:

```c
gt_join(handle):
    if target already done:
        return OK

    add current task to target's join waiters
    current_task->state = WAITING_JOIN
    switch back to scheduler
    return when target completes or cancellation/shutdown wakes us
```

Important: `gt_join()` is a green-thread operation. It should not be used to block an ordinary OS thread outside the runtime.

### Cancellation

Cancellation is cooperative.

`gt_task_cancel(h)` requests cancellation. It does not forcibly kill a running C function in the middle of arbitrary code.

A task notices cancellation when:

- it calls `gt_task_cancelled()`
- it reaches a runtime blocking operation that checks cancellation
- it is sleeping/waiting and gets woken due to cancellation

Mental model:

```text
cancel request = please stop when you reach a safe point
```

Not:

```text
cancel request = immediately kill the OS thread
```

---

## 13. v0.6: why multiple workers are harder

Before v0.6, you can mostly think like this:

```text
one OS thread
one scheduler
one green task running at a time
```

With v0.6:

```text
multiple OS worker threads
one shared runtime
many green tasks may run at the same time on different workers
```

Text diagram:

```text
                 shared runtime state
        +----------------------------------+
        | run queue                        |
        | timer heap                       |
        | channel wait queues              |
        | select wait queues               |
        | task handles / join waiters       |
        | memory/debug counters            |
        +----------------------------------+
              ^          ^          ^
              |          |          |
          Worker 1   Worker 2   Worker 3
              |          |          |
           Task A     Task B     Task C
```

Now shared state must be protected.

The current implementation uses a shared global run queue guarded by runtime synchronization. This is simpler than per-worker queues plus work stealing.

The key idea:

```text
Only one worker at a time may mutate shared runtime data structures.
Multiple workers may run task code in parallel when they are outside the runtime lock.
```

That means a task can execute C code concurrently with other tasks, but when it calls back into the runtime, the runtime serializes access to queues, channels, timers, and handles.

---

## 14. Locks and condition variables from scratch

Because you answered “explain from scratch,” here is the shortest useful version.

### Mutex / lock

A mutex protects shared data.

```c
pthread_mutex_lock(&lock);
// read/write shared data safely
pthread_mutex_unlock(&lock);
```

Only one OS thread can hold the lock at a time.

Use it when two OS threads might touch the same data.

### Condition variable

A condition variable lets one OS thread sleep until another OS thread says, “something changed.”

Typical pattern:

```c
pthread_mutex_lock(&lock);
while (!condition_is_true) {
    pthread_cond_wait(&cond, &lock);
}
// condition is true, and lock is held again
pthread_mutex_unlock(&lock);
```

Another thread does:

```c
pthread_mutex_lock(&lock);
make_condition_true();
pthread_cond_signal(&cond);
pthread_mutex_unlock(&lock);
```

In this project, condition variables are useful when workers have no runnable task but may need to wake when:

- a new task is created
- a timer expires
- a channel operation wakes a task
- shutdown starts

### Lost wakeup problem

A lost wakeup happens when a thread goes to sleep even though the event already happened.

Bad conceptual pattern:

```text
Thread checks: no work
Another thread adds work and signals
Thread starts waiting too late
Thread sleeps forever
```

Correct pattern:

```text
hold lock
check condition in while loop
wait atomically releases lock and sleeps
wake reacquires lock
check condition again
```

This is why v0.6 tests care about idle-worker wakeups and external OS-thread `gt_go()`.

---

## 15. How blocking does not block the whole program

You said you worry about why blocking does not block the whole OS thread.

When a green task blocks on a channel, it does **not** call a blocking OS syscall. Instead, the runtime records that the task is waiting, then switches back to the scheduler.

Example: receiving from empty channel.

```text
Task A calls gt_chan_recv(empty channel)
        |
        v
runtime records Task A in channel recv_waiters
        |
        v
Task A state = WAITING_CHAN
        |
        v
context switch back to scheduler
        |
        v
worker runs Task B
```

So the task is blocked, but the worker is free to run another task.

In v0.6, if one worker has no ready work, another worker may still be running tasks. Workers coordinate through the shared runtime state.

---

## 16. Important source-code landmarks

Start with these, not random scrolling.

### Public API

```text
include/gt.h
```

Read the error codes first:

```c
GT_OK
GT_ERR_INVALID
GT_ERR_NOMEM
GT_ERR_STATE
GT_ERR_CLOSED
GT_ERR_CANCELLED
GT_ERR_WOULD_BLOCK
```

These tell you what each operation can report.

### Main runtime implementation

```text
src/gt.c
```

Look for these areas:

```text
struct gt_task              task state and bookkeeping
struct gt_chan              channel buffer/wait queues
struct gt_task_handle       join/cancel/status ownership
struct gt_select_waiter     select registration state
runtime global              scheduler queues, timers, worker state, lock/condvar
```

### Context switching

```text
src/context.h
src/context_ucontext.c
src/context_win_fiber.c
```

This layer hides the platform-specific way to switch stacks.

You do not need to master this first. Just know that it lets the runtime switch between:

```text
scheduler context <-> task context
```

---

## 17. How to read the tests

The tests are the best map of expected behavior.

Read them like this:

```text
What state is being set up?
What operation is being tested?
What wakeup/result is expected?
What cleanup is checked afterward?
```

Recommended order:

### First pass

```text
tests/test_v0_1.c
```

Goal: understand task creation, yield, run loop.

### Second pass

```text
tests/test_v0_3.c
```

Goal: understand buffered/unbuffered channels, close, destroy, channel deadlock.

### Third pass

```text
tests/test_v0_5.c
```

Goal: understand try-send/try-recv and select.

### Fourth pass

```text
tests/test_v0_6.c
```

Goal: understand multi-worker behavior and race-sensitive cases.

When a test fails, ask:

1. Did the task become runnable when expected?
2. Did a blocked task remain registered somewhere incorrectly?
3. Did the runtime forget to wake a worker?
4. Did the operation return the correct error code?
5. Did memory counters balance?

---

## 18. Common edge cases to understand

### Edge case: ready task vs timeout

If a select timeout exists, the runtime must not report deadlock just because no task is ready right now.

```text
No ready tasks
One task sleeping/select-timeout pending
=> wait for timer, not deadlock
```

### Edge case: select double wakeup

A task selecting on multiple channels must wake exactly once.

```text
select on ch1 and ch2
ch1 wakes task
runtime must unregister task from ch2
later ch2 closes
must not wake same task again
```

### Edge case: close with buffered values

```text
buffer has [1, 2]
close channel
recv -> 1 OK
recv -> 2 OK
recv -> CLOSED
```

### Edge case: cancellation is not preemption

```text
while (1) { }
```

A task like this cannot be safely cancelled by this runtime because it never reaches a cooperative safe point.

### Edge case: external OS thread calls `gt_go()`

In v0.6, another OS thread may create work while runtime workers are active. The runtime must:

```text
lock shared queue
add new task
signal worker condition variable
unlock
```

Otherwise idle workers might sleep forever.

---

## 19. Debugging checklist

When investigating a runtime bug, classify it first.

### If a task never runs

Check:

- Was `gt_go()` successful?
- Was the task put on the ready queue?
- Did a worker get signaled?
- Did the runtime already shut down?

### If a task blocks forever

Check:

- Which wait queue is it on?
- What event should wake it?
- Does that event call the wakeup helper?
- Is the task removed from losing wait queues?
- Is the worker condition variable signaled?

### If a test has a memory-counter mismatch

Check:

- Every allocation path has cleanup on failure.
- Every blocked waiter is freed on timeout/cancel/close/destroy/shutdown.
- Handles are released.
- Timers are removed/freed when no longer needed.

### If ThreadSanitizer reports a race

Check:

- Is shared runtime state accessed without the runtime lock?
- Is task state read from another OS thread without synchronization?
- Are debug counters protected?
- Is a channel accessed outside the lock?

---

## 20. A learning roadmap for you

Because you chose “start with concurrency theory” and want all topics explained, use this path.

### Day 1: API and examples

Read:

```text
include/gt.h
examples/basic.c
examples/sleep.c
examples/channels.c
```

Goal:

```text
Know what the runtime promises users.
```

### Day 2: Scheduler basics

Read:

```text
tests/test_v0_1.c
tests/test_v0_2.c
```

Focus on:

```text
gt_go
gt_run
gt_yield
gt_sleep_ms
ready queue
timer heap
```

### Day 3: Channels

Read:

```text
tests/test_v0_3.c
examples/channels.c
```

Focus on:

```text
buffered vs unbuffered
send/recv blocking
close
waiter wakeups
```

### Day 4: Join and cancellation

Read:

```text
tests/test_v0_4.c
examples/handles.c
```

Focus on:

```text
task handles
join waiters
status
cooperative cancellation
handle release
```

### Day 5: Select

Read:

```text
tests/test_v0_5.c
examples/select.c
examples/select_advanced.c
```

Focus on:

```text
immediate ready cases
default
timeout
blocking select
losing-case unregister
close/destroy wakeups
```

### Day 6: Multi-worker runtime

Read:

```text
tests/test_v0_6.c
v0_6_test_plan.md
```

Focus on:

```text
pthread workers
runtime lock
condition variables
shared run queue
cross-worker wakeups
ThreadSanitizer
```

### Day 7: Source dive

Now read selected parts of:

```text
src/gt.c
```

Do not read top-to-bottom. Trace one behavior at a time:

```text
gt_go -> ready queue -> worker loop -> task returns
gt_sleep_ms -> timer heap -> wakeup
gt_chan_recv empty -> wait queue -> sender wakes receiver
gt_select -> register waiters -> one case wins -> cleanup losing cases
gt_join -> join waiter -> target completion wakes joiners
```

---

## 21. What “good understanding” looks like

You understand this project well when you can answer these without looking too much:

1. Where does a task live when it is ready?
2. Where does a task live when it is sleeping?
3. Where does a task live when it is blocked on a channel?
4. What wakes each blocked state?
5. Why does select need losing-case cleanup?
6. Why does v0.6 need locks even though green threads are cooperative?
7. Why is cancellation cooperative instead of forced?
8. What should happen to memory counters after shutdown?
9. Why does a pending timer prevent deadlock?
10. Why does `gt_runtime_workers()` return zero after runtime completion?

If you can answer those, you can review most changes to this project safely.

---

## 22. One-page summary

```text
This project is a stackful cooperative green-thread runtime in C.

A task runs until it yields, sleeps, blocks, is cancelled at a safe point, or returns.

The scheduler owns ready tasks. Timers own sleeping tasks. Channels own channel-blocked tasks. Select registers one task on multiple possible channel wait queues. Join registers one task on another task's completion waiters.

Channels are either buffered or unbuffered. Buffered channels store values. Unbuffered channels require sender/receiver rendezvous.

Close means no more sends, but buffered values may still be received. Destroy means freeing channel resources and must be handled carefully around waiters.

Select first tries immediate cases, then default/zero-timeout, then parks the task on all relevant wait queues and optionally a timer. When one case wins, losing cases must be unregistered.

v0.6 adds multiple OS worker threads. Green tasks are still cooperative, but different workers can run different green tasks truly in parallel. Therefore shared runtime state needs locks and worker wakeups need condition variables.

The tests are the best learning map. Read them by version before reading all of src/gt.c.
```


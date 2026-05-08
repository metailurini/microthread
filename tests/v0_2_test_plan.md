# v0.2 Test Plan: Stacks and Timers

## 1. Purpose

v0.2 extends the v0.1 cooperative green-thread runtime with safer stack handling and the first true parking primitive: sleeping until a timer deadline.

The target v0.2 features are:

- configurable stack sizes
- stack allocator abstraction
- guard pages where the platform supports them
- `gt_go_with_stack(fn, arg, stack_size)`
- `gt_sleep_ms(ms)`
- timer heap or equivalent deadline queue
- scheduler support for sleeping tasks
- runtime exit when all tasks are completed and no sleepers remain
- correct behavior when runnable tasks and sleeping tasks coexist

This test plan assumes v0.1 behavior remains valid. All v0.1 tests must continue to pass.

## 2. Proposed v0.2 Public API

```c
typedef struct gt_options {
    size_t stack_size;
} gt_options_t;

int gt_go_with_stack(gt_fn fn, void *arg, size_t stack_size);

void gt_sleep_ms(uint64_t ms);

size_t gt_debug_sleeping_task_count(void);
```

Implementation notes now documented for v0.2:

- `gt_go(fn, arg)` is equivalent to `gt_go_with_stack(fn, arg, 0)`.
- `stack_size == 0` means use the default stack size.
- stack sizes below `GT_MIN_STACK_SIZE` are rejected.
- `gt_sleep_ms(0)` behaves like `gt_yield()`.
- `gt_sleep_ms()` outside a running green thread is a safe no-op.

## 3. Test Categories

| Category | Focus |
|---|---|
| TC-V02-REG | v0.1 regression safety |
| TC-STACKCFG | configurable stack creation |
| TC-GUARD | guard page and stack overflow behavior |
| TC-SLEEP | basic `gt_sleep_ms` behavior |
| TC-TIMER | timer ordering and deadline behavior |
| TC-SCHED | scheduler behavior with ready and sleeping tasks |
| TC-LIFE | lifecycle and cleanup of sleeping tasks |
| TC-ERR | invalid inputs and error paths |
| TC-STRESS | practical stress tests |
| TC-BACKEND | Unix/Windows platform behavior |
| TC-MEM | leaks and resource cleanup |
| TC-MISUSE | documented misuse and dangerous patterns |

## 4. Test Environment

Run the v0.2 suite under these configurations where practical:

```text
make test
make stress
make sanitize
```

Recommended platform matrix:

```text
Linux x86_64:       ucontext backend, mmap/mprotect stack backend
macOS arm64/x86_64: ucontext backend if available, mmap/mprotect stack backend
Windows x64:        Fiber backend, VirtualAlloc/VirtualProtect stack backend
```

Timer tests should avoid assuming perfect millisecond precision. Use tolerance windows.

Recommended tolerance:

```text
sleep should not complete before requested deadline except for clock/tolerance noise
sleep may complete late, especially on busy CI machines
normal tolerance: 20ms to 50ms
stress tolerance: looser, 100ms or more if needed
```

Use a monotonic clock for all runtime and test timing:

```text
Unix:    clock_gettime(CLOCK_MONOTONIC, ...)
Windows: QueryPerformanceCounter or GetTickCount64
```

## 5. v0.1 Regression Tests

### TC-V02-REG-001: v0.1 suite still passes

Steps:

1. Build the project.
2. Run the full v0.1 test suite.
3. Run the new v0.2 test suite.

Expected:

- All v0.1 tests still pass.
- No API behavior from v0.1 regresses.

### TC-V02-REG-002: yield-only tasks still work with timer support compiled in

Steps:

1. Create several tasks that only call `gt_yield()`.
2. Do not call `gt_sleep_ms()`.
3. Run the scheduler.

Expected:

- Behavior matches v0.1 round-robin behavior.
- Timer subsystem does not affect pure yield scheduling.

## 6. Configurable Stack Tests

### TC-STACKCFG-001: create task with default stack

Steps:

1. Call `gt_go(fn, arg)`.
2. Run the scheduler.

Expected:

- Task runs and completes.
- Debug stack bounds, if available, show the default stack size.

### TC-STACKCFG-002: create task with explicit stack size

Steps:

1. Call `gt_go_with_stack(fn, arg, 128 * 1024)`.
2. In the task, yield at least once.
3. Return normally.

Expected:

- Task runs, yields, resumes, and completes.
- Stack allocation uses the requested size or a documented rounded-up size.

### TC-STACKCFG-003: small but valid stack

Steps:

1. Create a task with the minimum supported stack size.
2. Task performs a shallow call chain and yields.

Expected:

- Task succeeds.
- Runtime documents the minimum stack size.

### TC-STACKCFG-004: stack size below minimum

Steps:

1. Call `gt_go_with_stack(fn, arg, 1)`.

Expected:

- Runtime rejects the task with an error.
- No task is added to the run queue.
- No memory is leaked.

### TC-STACKCFG-005: zero stack size

Steps:

1. Call `gt_go_with_stack(fn, arg, 0)`.

Expected:

- Either rejected, or explicitly treated as default stack size.
- Behavior must be documented and tested.

### TC-STACKCFG-006: very large stack size

Steps:

1. Request a very large stack size such as 64 MiB.
2. If allocation succeeds, task runs and exits.
3. If allocation fails, runtime returns an error cleanly.

Expected:

- No crash.
- No partial task remains in scheduler queues after failure.

### TC-STACKCFG-007: many tasks with mixed stack sizes

Steps:

1. Create tasks with stack sizes such as 64 KiB, 128 KiB, and 256 KiB.
2. Each task records its id and yields several times.
3. Run scheduler.

Expected:

- All tasks complete.
- Stack sizes do not interfere with scheduling correctness.

## 7. Guard Page Tests

Guard page tests are platform-sensitive. Some should be separate crash-test binaries rather than part of the normal test process.

### TC-GUARD-001: stack allocation includes guard page when supported

Steps:

1. Create a task.
2. Use debug-only stack metadata to inspect stack base, usable size, and guard size.

Expected:

- Guard page size is at least one OS page when guard pages are enabled.
- Usable stack area does not include the guard page.

### TC-GUARD-002: usable stack address is within stack bounds

Steps:

1. In a task, take the address of a local variable.
2. Compare it against debug stack bounds.
3. Yield and check again after resume.

Expected:

- Local variable address is inside usable stack bounds before and after yield.

### TC-GUARD-003: intentional stack overflow trips guard page

Steps:

1. Run a separate test binary or subprocess.
2. Create a task with a small stack.
3. Recursively consume stack until overflow.

Expected:

- Process terminates with a segmentation/access violation on platforms with guard pages.
- Test harness treats this expected crash as pass.
- This test must not run inside the normal in-process unit test binary.

### TC-GUARD-004: guard page disabled fallback

Steps:

1. Build with guard pages disabled, if supported.
2. Create and run tasks.

Expected:

- Runtime still works.
- Debug metadata reports guard size as zero.
- Overflow protection is documented as unavailable in this mode.

## 8. Basic Sleep Tests

### TC-SLEEP-001: sleep zero milliseconds

Steps:

1. Task records event `A1`.
2. Task calls `gt_sleep_ms(0)`.
3. Task records event `A2`.

Expected:

- Task yields/parks according to documented semantics.
- If `sleep(0)` is defined as yield, other ready tasks may run before `A2`.
- Behavior must be deterministic and documented.

### TC-SLEEP-002: single task sleeps then resumes

Steps:

1. Task records start time.
2. Calls `gt_sleep_ms(20)`.
3. Records resume time.

Expected:

- Resume time is not earlier than requested deadline minus tolerance.
- Task completes after waking.

### TC-SLEEP-003: sleep outside a green thread

Steps:

1. Call `gt_sleep_ms(1)` from main, outside a running task.

Expected:

- Runtime either treats it as no-op or returns/records misuse according to API design.
- It must not crash.

### TC-SLEEP-004: task sleeps multiple times

Steps:

1. Task loops 5 times.
2. Each iteration calls `gt_sleep_ms(5)`.

Expected:

- Task resumes after each sleep.
- Total elapsed time is at least roughly 25ms minus tolerance.

### TC-SLEEP-005: many tasks sleep same duration

Steps:

1. Create 100 tasks.
2. Each task sleeps 10ms and increments a counter.

Expected:

- All tasks wake and complete.
- No tasks remain sleeping after `gt_run()` returns.

## 9. Timer Ordering Tests

### TC-TIMER-001: shorter sleep wakes first

Steps:

1. Task A sleeps 30ms.
2. Task B sleeps 5ms.
3. Record wake order.

Expected:

- B wakes before A.

### TC-TIMER-002: multiple different deadlines

Steps:

1. Create tasks sleeping 50ms, 10ms, 30ms, 20ms, and 40ms.
2. Record wake order.

Expected:

- Wake order follows deadline order, allowing ties only within tolerance.

### TC-TIMER-003: equal deadlines

Steps:

1. Create several tasks with the same sleep duration.
2. Record all wakes.

Expected:

- All wake eventually.
- Stable FIFO ordering for equal deadlines is preferred if documented.
- If equal-deadline order is unspecified, test only checks completion.

### TC-TIMER-004: timer heap grows

Steps:

1. Insert more sleeping tasks than the timer heap's initial capacity.
2. Run scheduler.

Expected:

- Heap grows safely.
- All tasks wake and complete.

### TC-TIMER-005: expired timers are moved to run queue before waiting

Steps:

1. Task sleeps for a short duration.
2. Scheduler observes no ready tasks.
3. Timer expires.

Expected:

- Scheduler wakes the task and does not hang forever.

### TC-TIMER-006: timer count debug value

Steps:

1. Task sleeps.
2. Another task checks debug sleeping count if supported.

Expected:

- Sleeping count increases when a task parks.
- Sleeping count decreases when it wakes.

## 10. Scheduler Interaction Tests

### TC-SCHED-001: ready task runs while another task sleeps

Steps:

1. Task A records `A1`, sleeps 30ms, records `A2`.
2. Task B records `B1`, yields, records `B2`.

Expected:

- B continues running while A is sleeping.
- A resumes after its deadline.

### TC-SCHED-002: scheduler waits when only sleepers exist

Steps:

1. Create one task that sleeps 20ms.
2. Run scheduler.

Expected:

- `gt_run()` does not return early.
- Scheduler waits until the task wakes and completes.

### TC-SCHED-003: scheduler returns when no live tasks remain

Steps:

1. Create tasks that sleep and then return.
2. Run scheduler.

Expected:

- `gt_run()` returns after all tasks complete.
- Runnable count and sleeping count are zero.

### TC-SCHED-004: task creates child then sleeps

Steps:

1. Parent creates child task.
2. Parent sleeps.
3. Child runs while parent sleeps.

Expected:

- Child is not blocked by parent sleeping.
- Parent wakes and completes later.

### TC-SCHED-005: task creates child after waking

Steps:

1. Parent sleeps.
2. Parent wakes and creates child.
3. Parent returns.

Expected:

- Child runs and completes before `gt_run()` returns.

### TC-SCHED-006: yield after sleep

Steps:

1. Task sleeps.
2. After waking, task calls `gt_yield()`.
3. Task resumes and completes.

Expected:

- Sleep and yield compose correctly.

### TC-SCHED-007: sleep after yield

Steps:

1. Task yields.
2. Task resumes and sleeps.
3. Task wakes and completes.

Expected:

- Yield and sleep compose correctly.

## 11. Lifecycle and Cleanup Tests

### TC-LIFE-001: sleeping task returns normally after wake

Expected:

- Task is destroyed exactly once.
- Live count decrements.

### TC-LIFE-002: shutdown with sleeping tasks pending

Steps:

1. Create a task that sleeps for a long duration.
2. Before running, or from outside the scheduler, call `gt_shutdown()`.

Expected:

- Sleeping and runnable tasks are cleaned.
- Timer heap is cleared.
- No leak.

### TC-LIFE-003: shutdown called from sleeping/woken task

Steps:

1. Task sleeps briefly.
2. After waking, task calls `gt_shutdown()`.

Expected:

- Same v0.1 rule applies: shutdown from inside a task is safe no-op or documented request behavior.
- No active stack is freed while running.

### TC-LIFE-004: repeated init/run/shutdown with sleepers

Steps:

1. Repeat 100 cycles.
2. Each cycle creates tasks that sleep briefly.
3. Run and shutdown.

Expected:

- No state leaks across runtime cycles.

## 12. Error Handling Tests

### TC-ERR-001: sleep before init

Steps:

1. Call `gt_sleep_ms(1)` before `gt_init()`.

Expected:

- Behavior is documented.
- Must not crash.

### TC-ERR-002: `gt_go_with_stack` before init

Steps:

1. Call `gt_go_with_stack(fn, arg, valid_stack_size)` before `gt_init()`.

Expected:

- Either auto-initializes like `gt_go()` or returns documented error.

### TC-ERR-003: invalid function pointer with custom stack

Steps:

1. Call `gt_go_with_stack(NULL, arg, valid_stack_size)`.

Expected:

- Returns invalid-argument error.
- No allocation leak.

### TC-ERR-004: stack allocation failure

Steps:

1. Use test-only fault injection to fail stack allocation.
2. Call `gt_go_with_stack()`.

Expected:

- Function returns an error.
- No runnable task is added.
- Runtime remains usable afterwards.

### TC-ERR-005: timer heap allocation failure

Steps:

1. Use test-only fault injection to fail timer heap growth.
2. Task calls `gt_sleep_ms()`.

Expected:

- Runtime handles failure according to documented policy.
- Preferred: task continues or runtime reports fatal error without corrupting queues.

### TC-ERR-006: monotonic clock failure

Steps:

1. Use test-only clock backend to force a clock failure.

Expected:

- Runtime returns/records error or uses documented fallback.
- No scheduler hang.

## 13. Stress Tests

### TC-STRESS-001: many sleepers

Steps:

1. Create 5,000 sleeping tasks in stress mode.
2. Sleep durations vary from 0ms to 20ms.

Expected:

- All complete.
- No memory leaks.

### TC-STRESS-002: many sleep/yield cycles

Steps:

1. Create 200 tasks.
2. Each task alternates `gt_yield()` and `gt_sleep_ms(1)` for 100 iterations.

Expected:

- All complete.
- Scheduler remains fair enough that no task starves.

### TC-STRESS-003: timer heap churn

Steps:

1. Tasks repeatedly sleep for pseudo-random short durations.
2. New timers are added while old timers expire.

Expected:

- Timer heap remains valid.
- Wakeups continue correctly.

### TC-STRESS-004: repeated runtime cycles with timers

Steps:

1. Repeat init/run/shutdown 1,000 times in stress mode.
2. Each cycle includes at least one sleeping task.

Expected:

- No leak or stale global state.

## 14. Backend and Platform Tests

### TC-BACKEND-001: Unix guarded stack backend

Expected:

- `mmap`/`mprotect` stack allocation works.
- Guard page metadata is correct.
- `munmap` cleanup occurs.

### TC-BACKEND-002: Windows Fiber backend

Expected:

- Windows Fibers are created, switched, and destroyed correctly.
- Requested stack size is passed to `CreateFiber()` and reflected in debug metadata.
- The scheduler fiber created by `ConvertThreadToFiber()` is cleaned up with `ConvertFiberToThread()`, not `DeleteFiber()`.
- This must be verified by building and running the suite in a Windows environment.

### TC-BACKEND-003: monotonic clock backend

Expected:

- Runtime uses monotonic time.
- System clock changes do not affect sleeps.

### TC-BACKEND-004: sleep precision smoke test per platform

Expected:

- Short sleeps complete within a reasonable upper bound on each platform.
- Tests use platform-specific tolerance where necessary.

## 15. Memory and Resource Tests

### TC-MEM-001: no leaks after sleeping tasks complete

Run with in-process allocation/free counters and under ASan/Valgrind where available.

Expected:

- No leaked task objects.
- No leaked stacks.
- No leaked timer nodes/heap storage.
- Allocation/free counters balance after shutdown.

### TC-MEM-002: no leaks after shutdown with sleeping tasks

Expected:

- Shutdown frees all pending sleeping tasks and timer resources.

### TC-MEM-003: no double-free after timer wake and task completion

Expected:

- A task removed from the timer heap is destroyed exactly once after completion.

### TC-MEM-004: stack allocator repeated allocation/free

Steps:

1. Create and destroy many tasks with mixed stack sizes.

Expected:

- No leaked virtual memory.
- Allocation/free counters balance across repeated stack allocation cycles.
- No use-after-free.

## 16. Misuse and Contract Tests

### TC-MISUSE-001: blocking OS sleep inside green thread

Steps:

1. Task A calls OS `sleep()` directly.
2. Task B is ready.

Expected:

- Task B does not run until OS sleep returns.
- This documents why users should call `gt_sleep_ms()` instead.

### TC-MISUSE-002: busy loop still blocks scheduler

Steps:

1. Task A busy-loops without yield or sleep.
2. Task B is ready.

Expected:

- Task B waits until A returns.
- Cooperative scheduling limitation remains documented.

### TC-MISUSE-003: stack pointer escaping

Steps:

1. Task stores address of a local variable globally.
2. Task returns.
3. Another task verifies that the escaped pointer exists but deliberately does not dereference it.

Expected:

- Document as invalid C lifetime usage.
- Runtime remains healthy after the misuse demonstration.
- Dereferencing the expired pointer remains intentionally excluded because it would be undefined behavior in the test itself.

## 17. Suggested Test File Layout

Recommended files:

```text
tests/test_v0_1.c              existing v0.1 regression suite
tests/test_v0_2.c              normal v0.2 behavior tests
tests/test_v0_2_stress.c       optional heavy stress tests, or same binary with GT_STRESS
tests/test_guard_overflow.c    subprocess/crash-style guard page test
tests/test_guard_disabled.c    no-guard fallback configuration test
tests/TEST_COVERAGE.md         updated coverage matrix
Windows environment            Optional backend verification for Fibers
```

Recommended make targets:

```text
make test          # v0.1 + normal v0.2 tests
make stress        # larger v0.1/v0.2 stress profile
make sanitize      # ASan/UBSan where supported
make valgrind      # Valgrind where installed
make guard-test    # runs expected-crash guard-page subprocess test
make guard-disabled-test
```

## 18. Acceptance Criteria

v0.2 is considered complete when:

```text
1. All v0.1 tests still pass.
2. `gt_go_with_stack()` supports valid custom stack sizes.
3. Invalid stack sizes are rejected or documented.
4. Guard pages are implemented on at least Unix, or platform limitations are documented.
5. `gt_sleep_ms()` parks the current task without blocking ready tasks.
6. Scheduler waits correctly when only sleeping tasks exist.
7. Timer ordering is correct for different deadlines.
8. Sleeping tasks are cleaned correctly on completion and shutdown.
9. Normal, stress, sanitizer, and guard-disabled targets pass on the primary development platform.
10. Windows-specific Fiber backend behavior is tested on Windows CI.
```

## 19. Implementation Notes for Tests

- Do not assert exact wake time. Assert minimum deadline and broad upper bounds.
- Keep normal tests fast; put large task counts behind `GT_STRESS` or `make stress`.
- Use test-only fault injection for allocation and clock errors.
- Keep guard-page overflow tests out of the normal in-process test binary.
- Always check debug counters after each test:

```text
runnable count == 0
sleeping count == 0
live task count == 0
```

- Prefer event logs over timing-only assertions for scheduler ordering.

Example event log:

```c
static int events[32];
static int event_count;

static void record_event(int value) {
    events[event_count++] = value;
}
```

Use this to prove order such as:

```text
A starts
B runs while A sleeps
A wakes later
```

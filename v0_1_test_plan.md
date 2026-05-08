# v0.1 Test Plan: Minimal Green-Thread Runtime

## Version Scope

This test plan targets v0.1 of the green-thread runtime.

v0.1 features:

- single OS thread
- `ucontext` backend on Unix
- Windows Fibers backend on Windows
- `gt_go`
- `gt_yield`
- `gt_run`

Out of scope for v0.1:

- timers
- sleeping
- channels
- nonblocking I/O
- multiple OS worker threads
- preemption
- cancellation
- guard pages
- work stealing

## Assumed Public API

The exact names may change, but the test plan assumes an API similar to:

```c
typedef void (*gt_fn)(void *arg);

int  gt_init(void);
int  gt_go(gt_fn fn, void *arg);
void gt_yield(void);
int  gt_run(void);
void gt_shutdown(void);
```

Optional debug/test helpers are useful but not required in the public API:

```c
size_t gt_debug_runnable_count(void);
size_t gt_debug_live_task_count(void);
size_t gt_debug_completed_task_count(void);
int    gt_debug_current_task_id(void);
```

## Test Goals

The v0.1 test suite should prove that:

1. The runtime initializes and shuts down cleanly.
2. `gt_go` creates runnable green threads.
3. `gt_run` executes all runnable green threads.
4. A green thread can yield and later resume.
5. Multiple green threads are scheduled fairly enough for cooperative execution.
6. Green threads can return normally without corrupting scheduler state.
7. Nested or reentrant misuse is handled safely or explicitly rejected.
8. Invalid inputs are handled predictably.
9. Stackful execution preserves local variables across yields.
10. The Unix and Windows context backends behave consistently.

## Test Environment Matrix

### Operating Systems

| Platform | Backend | Required |
|---|---|---:|
| Linux x86_64 | `ucontext` or assembly backend later | Yes |
| macOS x86_64 | `ucontext` if available | Recommended |
| macOS arm64 | `ucontext` if available | Recommended |
| Windows x64 | Fibers | Yes |
| FreeBSD x86_64 | `ucontext` | Optional |

### Build Modes

| Mode | Flags / Tools | Purpose |
|---|---|---|
| Debug | `-O0 -g` | easier debugging |
| Release | `-O2` or `-O3` | optimizer safety |
| Warnings strict | `-Wall -Wextra -Werror` | API and portability hygiene |
| AddressSanitizer | `-fsanitize=address` | memory errors on supported platforms |
| UndefinedBehaviorSanitizer | `-fsanitize=undefined` | UB detection |
| Valgrind | Linux debug build | leak and invalid access checks |

### Compilers

| Compiler | Required |
|---|---:|
| GCC | Yes on Unix |
| Clang | Yes on Unix/macOS |
| MSVC | Yes on Windows |
| MinGW | Optional |

## Test Categories

1. Initialization and shutdown
2. Task creation
3. Scheduler run loop
4. Yield and resume behavior
5. Task lifecycle
6. Argument passing
7. Stack preservation
8. Ordering and fairness
9. Error handling
10. Edge cases
11. Stress tests
12. Cross-platform backend tests
13. Memory/resource tests
14. Misuse/contract tests

---

## 1. Initialization and Shutdown Tests

### TC-INIT-001: Initialize runtime successfully

Steps:

1. Call `gt_init()`.
2. Assert return value indicates success.
3. Call `gt_shutdown()`.

Expected:

- Runtime initializes without crashing.
- Runtime shuts down without leaks or invalid accesses.

### TC-INIT-002: Double initialization

Steps:

1. Call `gt_init()`.
2. Call `gt_init()` again.
3. Call `gt_shutdown()`.

Expected:

- Either the second call is harmless and returns success, or it returns a documented error.
- Runtime remains usable or fails safely according to the documented behavior.

### TC-INIT-003: Shutdown without initialization

Steps:

1. Call `gt_shutdown()` before `gt_init()`.

Expected:

- No crash.
- Either no-op behavior or documented error behavior.

### TC-INIT-004: Reinitialize after shutdown

Steps:

1. Call `gt_init()`.
2. Call `gt_shutdown()`.
3. Call `gt_init()` again.
4. Create one green thread.
5. Call `gt_run()`.
6. Call `gt_shutdown()`.

Expected:

- Runtime can be cleanly reused if this is supported.
- If reuse is unsupported, the second `gt_init()` returns a documented error.

---

## 2. Task Creation Tests

### TC-GO-001: Create one green thread

Steps:

1. Initialize runtime.
2. Call `gt_go(task_fn, &counter)`.
3. Call `gt_run()`.

Task behavior:

```c
static void task_fn(void *arg) {
    int *counter = arg;
    (*counter)++;
}
```

Expected:

- `gt_go` succeeds.
- Counter equals `1` after `gt_run()`.

### TC-GO-002: Create multiple green threads before run

Steps:

1. Create `N = 100` green threads.
2. Each increments a shared counter once.
3. Call `gt_run()`.

Expected:

- Counter equals `100`.
- All tasks complete.

### TC-GO-003: Create zero green threads then run

Steps:

1. Initialize runtime.
2. Call `gt_run()` without creating tasks.

Expected:

- `gt_run()` returns successfully.
- No crash.

### TC-GO-004: Null function pointer

Steps:

1. Call `gt_go(NULL, arg)`.

Expected:

- `gt_go` returns an error.
- Runtime remains usable.

### TC-GO-005: Null argument

Steps:

1. Call `gt_go(task_fn_accepting_null, NULL)`.
2. Call `gt_run()`.

Expected:

- Task receives `NULL` as its argument.
- Runtime does not reject `NULL` arguments unless documented otherwise.

### TC-GO-006: Create task from inside another task

Steps:

1. Parent green thread calls `gt_go(child_fn, arg)`.
2. Parent returns or yields.
3. Call `gt_run()`.

Expected:

- Child task runs before `gt_run()` returns.
- Scheduler handles task creation during scheduling.

### TC-GO-007: Create many tasks from inside tasks

Steps:

1. Create one parent task.
2. Parent creates 1,000 child tasks.
3. Each child increments a counter.
4. Call `gt_run()`.

Expected:

- All children run.
- Counter equals `1,000`.
- No queue corruption.

---

## 3. Scheduler Run Loop Tests

### TC-RUN-001: `gt_run` drains all runnable tasks

Steps:

1. Create several tasks that do not yield.
2. Call `gt_run()`.

Expected:

- All tasks complete.
- `gt_run()` returns only after the runnable queue is empty.

### TC-RUN-002: Calling `gt_run` twice after completion

Steps:

1. Create tasks.
2. Call `gt_run()`.
3. Call `gt_run()` again.

Expected:

- First call runs tasks.
- Second call returns immediately without crash.

### TC-RUN-003: Add tasks after previous run completed

Steps:

1. Create task A.
2. Call `gt_run()`.
3. Create task B.
4. Call `gt_run()` again.

Expected:

- Both A and B run in their respective runs if post-run task creation is supported.
- If unsupported, task creation after run returns a documented error.

### TC-RUN-004: Return code when all tasks complete

Steps:

1. Create one normal task.
2. Call `gt_run()`.

Expected:

- Return code indicates success.

### TC-RUN-005: Reentrant `gt_run` from green thread

Steps:

1. A green thread calls `gt_run()`.

Expected:

- Runtime rejects reentrant scheduler entry with a documented error or assertion in debug builds.
- No scheduler corruption.

---

## 4. Yield and Resume Tests

### TC-YIELD-001: Single task yields once

Steps:

1. Create one task.
2. Task increments counter.
3. Task calls `gt_yield()`.
4. Task increments counter again.
5. Call `gt_run()`.

Expected:

- Counter equals `2`.
- Task resumes after the yield point.

### TC-YIELD-002: Two tasks alternate using yield

Steps:

1. Create task A and task B.
2. Each appends its ID to a log, yields, then appends again.

Expected:

- Both tasks complete.
- Log shows both tasks made progress.
- Acceptable example order: `A1, B1, A2, B2`.

Note:

- Exact order depends on scheduler policy. The test should assert progress and documented order only.

### TC-YIELD-003: Yield in tight loop

Steps:

1. Create `N = 10` tasks.
2. Each loops `M = 1,000` times.
3. Each iteration increments its own counter and calls `gt_yield()`.

Expected:

- Every task counter equals `1,000`.
- No task disappears from the run queue.

### TC-YIELD-004: Yield as first operation

Steps:

1. Task calls `gt_yield()` immediately.
2. Then increments counter.

Expected:

- Counter equals `1`.
- Yield before doing work is safe.

### TC-YIELD-005: Yield as last operation

Steps:

1. Task increments counter.
2. Task calls `gt_yield()`.
3. Task returns immediately after resuming.

Expected:

- Counter equals `1`.
- Task cleanup is correct after resuming from final yield.

### TC-YIELD-006: Yield outside green thread

Steps:

1. Call `gt_yield()` from the main thread before `gt_run()`.

Expected:

- Runtime handles this safely.
- Recommended behavior: return no-op or fail in debug mode with a clear error.
- No crash in release builds.

---

## 5. Task Lifecycle Tests

### TC-LIFE-001: Task returns normally

Steps:

1. Create a task that simply returns.
2. Call `gt_run()`.

Expected:

- Task is marked complete.
- Stack/context resources are released or queued for release.

### TC-LIFE-002: Multiple tasks return in different yield states

Steps:

1. Task A returns without yielding.
2. Task B yields once, then returns.
3. Task C yields multiple times, then returns.

Expected:

- All tasks complete.
- No completed task is scheduled again.

### TC-LIFE-003: Task creates child then returns immediately

Steps:

1. Parent creates child.
2. Parent returns without yielding.
3. Child increments counter.

Expected:

- Child still runs.
- Parent cleanup does not remove or corrupt child task.

### TC-LIFE-004: Task function exits through runtime trampoline

Steps:

1. Create a task that returns normally.
2. Verify runtime trampoline marks it as dead and switches back to scheduler.

Expected:

- No task returns into invalid memory.
- No fall-through past task entry function.

---

## 6. Argument Passing Tests

### TC-ARG-001: Pass pointer argument

Steps:

1. Pass pointer to struct into task.
2. Task verifies fields and modifies one field.

Expected:

- Task receives the exact pointer.
- Field modification is visible after `gt_run()`.

### TC-ARG-002: Pass different arguments to many tasks

Steps:

1. Create 100 tasks.
2. Each receives pointer to a unique integer slot.
3. Each writes its task index into the slot.

Expected:

- All slots contain expected values.
- No argument aliasing caused by scheduler internals.

### TC-ARG-003: Pass stack address from caller

Steps:

1. In main, create local variable.
2. Pass its address to a task.
3. Call `gt_run()` before the local variable goes out of scope.

Expected:

- Task can read and write the value.

Note:

- This is valid only while the caller's stack variable remains alive.

---

## 7. Stack Preservation Tests

### TC-STACK-001: Local variables survive yield

Steps:

1. Task declares local variables.
2. Task assigns known values.
3. Task yields.
4. Task verifies values after resume.

Expected:

- All local values remain intact.

### TC-STACK-002: Deep call chain survives yield

Steps:

1. Task calls function A.
2. A calls B.
3. B calls C.
4. C calls `gt_yield()`.
5. After resume, return through C, B, A.

Expected:

- Call stack resumes correctly.
- Return values are correct.

### TC-STACK-003: Recursion survives yield

Steps:

1. Task recursively calls a function to depth 32.
2. At deepest point, call `gt_yield()`.
3. Resume and unwind recursion.

Expected:

- Recursion state is preserved.
- Final computed result is correct.

### TC-STACK-004: Large local array within default stack limit

Steps:

1. Task allocates a local array smaller than default stack.
2. Writes a pattern.
3. Yields.
4. Verifies pattern after resume.

Expected:

- Pattern remains intact.

### TC-STACK-005: Many tasks each have independent stacks

Steps:

1. Create 100 tasks.
2. Each declares local state with a unique pattern.
3. Each yields several times.
4. Each verifies its own pattern.

Expected:

- No stack memory overlaps between tasks.

---

## 8. Ordering and Fairness Tests

### TC-FAIR-001: FIFO scheduling without yield

Steps:

1. Create tasks A, B, C.
2. Each appends its ID once and returns.

Expected:

- If FIFO is documented, log equals `A, B, C`.
- If order is unspecified, assert only that A, B, and C all appear exactly once.

### TC-FAIR-002: Round-robin behavior with yield

Steps:

1. Create tasks A, B, C.
2. Each appends ID, yields, appends ID, yields, appends ID.

Expected:

- If round-robin is documented, expected log is `A, B, C, A, B, C, A, B, C`.
- If not documented, assert each task appears exactly 3 times and all complete.

### TC-FAIR-003: Long-running non-yielding task blocks others

Steps:

1. Create task A that performs a long CPU loop without yield.
2. Create task B that increments counter.
3. Call `gt_run()`.

Expected:

- Since v0.1 is cooperative, B does not run until A returns.
- This behavior should be documented.

### TC-FAIR-004: Yield-heavy task does not starve normal tasks

Steps:

1. Task A yields 10,000 times.
2. Task B increments a counter and returns.

Expected:

- Task B completes.
- Scheduler does not continuously reschedule A ahead of all others.

---

## 9. Error Handling Tests

### TC-ERR-001: `gt_go` before `gt_init`

Steps:

1. Call `gt_go(task, arg)` before initialization.

Expected:

- Returns documented error or implicitly initializes if that behavior is documented.
- No crash.

### TC-ERR-002: `gt_run` before `gt_init`

Steps:

1. Call `gt_run()` before initialization.

Expected:

- Returns documented error or no-op if documented.
- No crash.

### TC-ERR-003: Allocation failure during task creation

Steps:

1. Use a fault-injection allocator.
2. Force stack allocation or task object allocation to fail.
3. Call `gt_go()`.

Expected:

- `gt_go()` returns error.
- No partial task remains in the runnable queue.
- No leaked resources.

### TC-ERR-004: Context creation failure

Steps:

1. Inject failure from context backend.
2. Call `gt_go()`.

Expected:

- `gt_go()` returns error.
- Allocated stack and task object are freed.

### TC-ERR-005: Runtime shutdown with pending tasks

Steps:

1. Initialize runtime.
2. Create tasks.
3. Call `gt_shutdown()` without `gt_run()`.

Expected:

- Runtime frees pending tasks or returns a documented error.
- No leaks.

---

## 10. Edge Case Tests

### TC-EDGE-001: Task count equals one

Steps:

1. Create exactly one task.
2. Run.

Expected:

- Basic single-task case works.

### TC-EDGE-002: Very large number of tasks

Steps:

1. Create 100,000 trivial tasks, or the largest practical number for CI.
2. Run.

Expected:

- Either all tasks run or task creation fails gracefully when memory is exhausted.
- No crash or corruption.

### TC-EDGE-003: Task yields many times

Steps:

1. One task yields 1,000,000 times.

Expected:

- Runtime remains stable.
- No stack growth or queue corruption.

### TC-EDGE-004: Task creates task after many yields

Steps:

1. Task yields 100 times.
2. Then creates a child task.
3. Both complete.

Expected:

- Delayed task creation works.

### TC-EDGE-005: Task function uses floating-point values across yield

Steps:

1. Task computes floating-point values.
2. Yields.
3. Verifies values after resume.

Expected:

- Floating-point state is preserved if required by the backend ABI.

Note:

- This is especially important for custom assembly backends later.

### TC-EDGE-006: Task function uses callee-saved registers across yield

Steps:

1. Compile optimized build.
2. Use a task with enough local variables to force register allocation.
3. Yield and verify values.

Expected:

- Register state required by the ABI is preserved.

### TC-EDGE-007: Yield inside callback-style helper

Steps:

1. Task calls helper function.
2. Helper calls another function pointer callback.
3. Callback calls `gt_yield()`.

Expected:

- Stack resumes correctly through callback frame.

### TC-EDGE-008: Task returns after child completes first

Steps:

1. Parent creates child.
2. Parent yields.
3. Child completes.
4. Parent resumes and returns.

Expected:

- Both lifecycles are handled correctly.

---

## 11. Stress Tests

### TC-STRESS-001: Many tasks, many yields

Configuration:

```text
tasks: 10,000
yields per task: 100
```

Expected:

- All tasks complete.
- Total progress count equals `1,000,000`.
- No leaks or invalid memory accesses.

### TC-STRESS-002: Repeated init-run-shutdown cycles

Steps:

1. Repeat 1,000 times:
   - `gt_init()`
   - create 10 tasks
   - `gt_run()`
   - `gt_shutdown()`

Expected:

- No resource accumulation.
- No stale global state.

### TC-STRESS-003: Randomized yield patterns

Steps:

1. Create many tasks.
2. Each task randomly decides whether to yield during each loop iteration.

Expected:

- Final counters match expected totals.
- Scheduler remains stable under varied interleavings.

### TC-STRESS-004: Recursive task creation tree

Steps:

1. Root task creates two child tasks.
2. Each child creates two more until depth 10.

Expected:

- Expected number of tasks run: `2^(depth + 1) - 1`.
- No queue corruption.

---

## 12. Cross-Platform Backend Tests

### TC-BACKEND-001: Unix `ucontext` smoke test

Steps:

1. Build with Unix backend.
2. Run basic task/yield tests.

Expected:

- All v0.1 core tests pass.

### TC-BACKEND-002: Windows Fiber smoke test

Steps:

1. Build with Windows Fiber backend.
2. Run basic task/yield tests.

Expected:

- All v0.1 core tests pass.

### TC-BACKEND-003: Context switch preserves stack pointer

Steps:

1. In task, take address of local variable before yield.
2. Yield.
3. Take address of another local variable after yield.

Expected:

- Addresses are within the task stack range if debug stack bounds are available.
- Values remain valid.

### TC-BACKEND-004: Main thread converted to fiber on Windows

Steps:

1. Initialize runtime on Windows.
2. Verify scheduler can switch to created Fiber task and back.

Expected:

- Main thread conversion is handled once.
- Shutdown cleans up task fibers correctly.

---

## 13. Memory and Resource Tests

### TC-MEM-001: No leaks after basic run

Steps:

1. Create 100 tasks.
2. Run to completion.
3. Shutdown.
4. Check with ASan, Valgrind, or platform leak detector.

Expected:

- No leaked task objects.
- No leaked stacks.
- No leaked backend contexts.

### TC-MEM-002: No use-after-free after task completion

Steps:

1. Create tasks that yield and return.
2. After completion, continue scheduler loop until empty.
3. Run under ASan.

Expected:

- Completed tasks are not scheduled again.
- No use-after-free.

### TC-MEM-003: Failed task creation does not leak stack

Steps:

1. Inject failure after stack allocation but before context creation completes.

Expected:

- Stack allocation is released.

### TC-MEM-004: Runtime frees tasks that never ran

Steps:

1. Create tasks.
2. Shutdown without running.

Expected:

- Pending task resources are freed or shutdown returns a documented error.

---

## 14. Misuse and Contract Tests

### TC-MISUSE-001: Calling blocking OS sleep inside green thread

Steps:

1. Task A calls native `sleep()` or `Sleep()`.
2. Task B is runnable.

Expected:

- In v0.1, Task B does not run until Task A returns from blocking sleep.
- This confirms and documents cooperative single-thread limitation.

### TC-MISUSE-002: Calling `gt_shutdown` from green thread

Steps:

1. A task calls `gt_shutdown()`.

Expected:

- Runtime rejects the call or handles it according to documented behavior.
- Recommended: reject in debug builds and return error in release builds.

### TC-MISUSE-003: Longjmp across green-thread boundary

Steps:

1. Attempt `setjmp` in one context and `longjmp` from another green thread.

Expected:

- This should be documented as unsupported.
- If test is included, it should be negative/documentation-only unless runtime explicitly protects against it.

### TC-MISUSE-004: C++ exceptions across green-thread boundary

Steps:

1. If runtime is compiled with C++ tests, throw exception out of green-thread entry function.

Expected:

- Unsupported unless explicitly designed.
- Runtime should document that task functions must not throw across C ABI boundary.

---

## Suggested Minimal Automated Test Files

```text
tests/
  test_init.c
  test_go.c
  test_run.c
  test_yield.c
  test_lifecycle.c
  test_arguments.c
  test_stack_preservation.c
  test_scheduler_order.c
  test_errors.c
  test_stress.c
  test_backend.c
```

## Suggested Test Harness Style

Use simple C assertions at first:

```c
#include <assert.h>

static int counter;

static void task(void *arg) {
    int *p = arg;
    (*p)++;
    gt_yield();
    (*p)++;
}

int main(void) {
    assert(gt_init() == 0);
    assert(gt_go(task, &counter) == 0);
    assert(gt_run() == 0);
    assert(counter == 2);
    gt_shutdown();
    return 0;
}
```

Later, move to a small test framework such as Unity, CMocka, Criterion, or custom TAP output.

## CI Requirements for v0.1

Minimum CI jobs:

```text
Linux GCC Debug
Linux GCC Release
Linux Clang Debug
Linux Clang ASan/UBSan
Windows MSVC Debug
Windows MSVC Release
```

Recommended CI jobs:

```text
macOS Clang Debug
macOS Clang Release
Linux Valgrind
```

## v0.1 Exit Criteria

v0.1 is complete when:

- All initialization tests pass.
- All basic task creation tests pass.
- All scheduler run-loop tests pass.
- All yield/resume tests pass.
- Stack preservation tests pass on Unix and Windows.
- Memory/resource tests pass under at least one leak detector.
- Invalid input behavior is documented and tested.
- Cooperative blocking limitation is documented.
- The same user-facing API works on Unix and Windows.

## Known Limitations to Document in v0.1

- Scheduling is cooperative only.
- A task that never yields can starve all other tasks.
- Blocking system calls block the whole runtime.
- There is only one OS thread.
- No timers or sleep API yet.
- No channels yet.
- No cancellation yet.
- No stack guard pages yet unless implemented earlier than planned.
- Behavior of `longjmp`, C++ exceptions, signals, and thread-local storage across green-thread boundaries is unsupported unless explicitly designed.

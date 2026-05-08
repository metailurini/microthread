#if !defined(_WIN32) && !defined(_POSIX_C_SOURCE)
#define _POSIX_C_SOURCE 200809L
#endif

#ifndef MT_TESTING
#define MT_TESTING
#endif
#include "microthread.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <unistd.h>
#endif

#define ARRAY_LEN(a) (sizeof(a) / sizeof((a)[0]))
#define MS_TO_NS UINT64_C(1000000)

#if defined(__SANITIZE_ADDRESS__)
#define MT_TEST_ASAN 1
#elif defined(__has_feature)
#if __has_feature(address_sanitizer)
#define MT_TEST_ASAN 1
#endif
#endif

#if defined(MT_FULL_STRESS)
enum {
    V02_MANY_SLEEPERS = 5000,
    V02_SLEEP_YIELD_TASKS = 200,
    V02_SLEEP_YIELD_ITERS = 100,
    V02_RUNTIME_CYCLES = 1000,
    V02_TIMER_CHURN_TASKS = 500,
    V02_TIMER_CHURN_ITERS = 25
};
#else
enum {
    V02_MANY_SLEEPERS = 200,
    V02_SLEEP_YIELD_TASKS = 40,
    V02_SLEEP_YIELD_ITERS = 20,
    V02_RUNTIME_CYCLES = 50,
    V02_TIMER_CHURN_TASKS = 80,
    V02_TIMER_CHURN_ITERS = 8
};
#endif

static int g_counter;
static int g_counter2;
static int g_errors;
static int g_events[20000];
static size_t g_event_count;
static uint64_t g_times[1024];
static volatile uintptr_t g_escaped_stack_pointer;

static uint64_t now_ns(void) {
#if defined(_WIN32)
    static LARGE_INTEGER freq;
    LARGE_INTEGER counter;
    if (freq.QuadPart == 0) {
        QueryPerformanceFrequency(&freq);
    }
    QueryPerformanceCounter(&counter);
    return (uint64_t)((counter.QuadPart * UINT64_C(1000000000)) / freq.QuadPart);
#else
    struct timespec ts;
    assert(clock_gettime(CLOCK_MONOTONIC, &ts) == 0);
    return ((uint64_t)ts.tv_sec * UINT64_C(1000000000)) + (uint64_t)ts.tv_nsec;
#endif
}

static void os_sleep_ms(unsigned ms) {
#if defined(_WIN32)
    Sleep((DWORD)ms);
#else
    struct timespec req;
    req.tv_sec = (time_t)(ms / 1000u);
    req.tv_nsec = (long)((ms % 1000u) * 1000000u);
    while (nanosleep(&req, &req) != 0) {
    }
#endif
}

static void reset_globals(void) {
    g_counter = 0;
    g_counter2 = 0;
    g_errors = 0;
    memset(g_events, 0, sizeof(g_events));
    memset(g_times, 0, sizeof(g_times));
    g_event_count = 0;
    g_escaped_stack_pointer = 0;
    mt_test_reset_faults();
}

static void record_event(int event) {
    assert(g_event_count < ARRAY_LEN(g_events));
    g_events[g_event_count++] = event;
}

static void expect_clean_runtime(void) {
    assert(mt_debug_runnable_count() == 0);
    assert(mt_debug_sleeping_task_count() == 0);
    assert(mt_debug_live_task_count() == 0);
    assert(mt_debug_current_task_id() == 0);
}

typedef struct memory_counters {
    size_t task_allocs;
    size_t task_frees;
    size_t stack_allocs;
    size_t stack_frees;
    size_t timer_allocs;
    size_t timer_frees;
} memory_counters_t;

static memory_counters_t memory_snapshot(void) {
    memory_counters_t c;
    mt_test_memory_counters(&c.task_allocs,
                            &c.task_frees,
                            &c.stack_allocs,
                            &c.stack_frees,
                            &c.timer_allocs,
                            &c.timer_frees);
    return c;
}

static void expect_memory_balanced_since(memory_counters_t before) {
    memory_counters_t after = memory_snapshot();
    assert(after.task_allocs - before.task_allocs == after.task_frees - before.task_frees);
    assert(after.stack_allocs - before.stack_allocs == after.stack_frees - before.stack_frees);
    assert(after.timer_allocs - before.timer_allocs == after.timer_frees - before.timer_frees);
}

static void teardown(void) {
    mt_shutdown();
    mt_test_reset_faults();
}

static void simple_inc(void *arg) {
    int *value = (int *)arg;
    (*value)++;
}

static void yield_only_a(void *arg) {
    (void)arg;
    record_event(1);
    mt_yield();
    record_event(3);
}

static void yield_only_b(void *arg) {
    (void)arg;
    record_event(2);
    mt_yield();
    record_event(4);
}

static void stack_probe_task(void *arg) {
    size_t *sizes = (size_t *)arg;
    int local = 123;
    void *base = mt_test_current_stack_base();
    size_t size = mt_test_current_stack_size();
    size_t guard = mt_test_current_stack_guard_size();
    sizes[0] = size;
    sizes[1] = guard;
#if !defined(_WIN32) && !defined(MT_TEST_ASAN)
    assert(base != NULL);
    assert(size >= MT_MIN_STACK_SIZE);
    assert((char *)&local >= (char *)base);
    assert((char *)&local < (char *)base + size);
#endif
    mt_yield();
#if !defined(_WIN32) && !defined(MT_TEST_ASAN)
    assert((char *)&local >= (char *)base);
    assert((char *)&local < (char *)base + size);
#endif
    assert(local == 123);
}

static void minimum_stack_task(void *arg) {
    int *value = (int *)arg;
    int local = 7;
    mt_yield();
    *value = local;
}

typedef struct mixed_stack_arg {
    int id;
    int iterations;
    int *sum;
} mixed_stack_arg_t;

static void mixed_stack_task(void *arg) {
    mixed_stack_arg_t *cfg = (mixed_stack_arg_t *)arg;
    int local = cfg->id;
    for (int i = 0; i < cfg->iterations; ++i) {
        local += 1;
        mt_yield();
    }
    *cfg->sum += local;
}

static void sleep_zero_a(void *arg) {
    (void)arg;
    record_event(1);
    mt_sleep_ms(0);
    record_event(3);
}

static void sleep_zero_b(void *arg) {
    (void)arg;
    record_event(2);
}

static void timed_sleep_task(void *arg) {
    uint64_t *times = (uint64_t *)arg;
    times[0] = now_ns();
    mt_sleep_ms(20);
    times[1] = now_ns();
}

static void multi_sleep_task(void *arg) {
    uint64_t *times = (uint64_t *)arg;
    times[0] = now_ns();
    for (int i = 0; i < 5; ++i) {
        mt_sleep_ms(5);
    }
    times[1] = now_ns();
}

static void same_duration_sleep_task(void *arg) {
    int *counter = (int *)arg;
    mt_sleep_ms(10);
    (*counter)++;
}

typedef struct sleep_order_arg {
    int id;
    unsigned ms;
} sleep_order_arg_t;

static void sleep_order_task(void *arg) {
    sleep_order_arg_t *cfg = (sleep_order_arg_t *)arg;
    mt_sleep_ms(cfg->ms);
    record_event(cfg->id);
    if ((size_t)cfg->id < ARRAY_LEN(g_times)) {
        g_times[cfg->id] = now_ns();
    }
}

static void sleeping_count_sleeper(void *arg) {
    (void)arg;
    mt_sleep_ms(20);
    record_event(2);
}

static void sleeping_count_observer(void *arg) {
    (void)arg;
    mt_yield();
    assert(mt_debug_sleeping_task_count() == 1);
    record_event(1);
}

static void ready_while_sleeping_a(void *arg) {
    (void)arg;
    record_event(1);
    mt_sleep_ms(30);
    record_event(4);
}

static void ready_while_sleeping_b(void *arg) {
    (void)arg;
    record_event(2);
    mt_yield();
    record_event(3);
}

static void child_records(void *arg) {
    int event = *(int *)arg;
    record_event(event);
}

static void parent_create_then_sleep(void *arg) {
    int *child_event = (int *)arg;
    record_event(1);
    assert(mt_go(child_records, child_event) > 0);
    mt_sleep_ms(10);
    record_event(3);
}

static void parent_create_after_wake(void *arg) {
    int *child_event = (int *)arg;
    record_event(1);
    mt_sleep_ms(5);
    assert(mt_go(child_records, child_event) > 0);
    record_event(2);
}

static void sleep_then_yield_task(void *arg) {
    (void)arg;
    record_event(1);
    mt_sleep_ms(5);
    record_event(2);
    mt_yield();
    record_event(4);
}

static void sleep_then_yield_peer(void *arg) {
    (void)arg;
    mt_yield();
    record_event(3);
}

static void yield_then_sleep_task(void *arg) {
    (void)arg;
    record_event(1);
    mt_yield();
    record_event(3);
    mt_sleep_ms(5);
    record_event(4);
}

static void yield_then_sleep_peer(void *arg) {
    (void)arg;
    record_event(2);
}

static void shutdown_from_woken_task(void *arg) {
    (void)arg;
    mt_sleep_ms(2);
    mt_shutdown();
    record_event(1);
}

static void timer_alloc_failure_task(void *arg) {
    uint64_t *times = (uint64_t *)arg;
    times[0] = now_ns();
    mt_test_fail_next_timer_alloc();
    mt_sleep_ms(5);
    times[1] = now_ns();
    record_event(1);
}

static void timer_alloc_failure_peer(void *arg) {
    (void)arg;
    record_event(2);
}

static void clock_failure_sleep_task(void *arg) {
    int *value = (int *)arg;
    mt_test_fail_next_clock_read();
    mt_sleep_ms(1);
    *value = 99;
}

static void long_os_sleep_task(void *arg) {
    (void)arg;
    record_event(1);
    os_sleep_ms(30);
    record_event(2);
}

static void busy_loop_task(void *arg) {
    (void)arg;
    record_event(1);
    volatile unsigned sink = 0;
    for (int i = 0; i < 1000000; ++i) {
        sink += i;
    }
    record_event(2);
}

static void ready_peer_task(void *arg) {
    (void)arg;
    record_event(3);
}

static void remember_escaped_stack_pointer(void *ptr) {
    g_escaped_stack_pointer = (uintptr_t)ptr;
}

static void escape_stack_pointer_task(void *arg) {
    (void)arg;
    int local = 1234;
    remember_escaped_stack_pointer(&local);
    record_event(1);
}

static void stack_escape_documentation_observer(void *arg) {
    (void)arg;
    record_event(2);
    /*
     * TC-MISUSE-003: this intentionally does not dereference
     * g_escaped_stack_pointer.  The pointer refers to another task's expired
     * stack frame after that task returns, so reading through it would be
     * undefined C behavior.  The automated test only captures the documented
     * misuse pattern and verifies the runtime stays healthy when users avoid
     * dereferencing the escaped pointer.
     */
    assert(g_escaped_stack_pointer != 0);
}

typedef struct churn_arg {
    unsigned seed;
    int iterations;
    int *done;
} churn_arg_t;

static void churn_task(void *arg) {
    churn_arg_t *cfg = (churn_arg_t *)arg;
    unsigned x = cfg->seed;
    for (int i = 0; i < cfg->iterations; ++i) {
        x = x * 1103515245u + 12345u;
        if (i & 1) {
            mt_yield();
        }
        mt_sleep_ms((x >> 16u) % 4u);
    }
    (*cfg->done)++;
}

static void sleep_yield_cycle_task(void *arg) {
    int *done = (int *)arg;
    for (int i = 0; i < V02_SLEEP_YIELD_ITERS; ++i) {
        mt_yield();
        mt_sleep_ms(1);
    }
    (*done)++;
}

static void run_regression_tests(void) {
    reset_globals();
    assert(mt_init() == MT_OK);
    assert(mt_go(yield_only_a, NULL) > 0);
    assert(mt_go(yield_only_b, NULL) > 0);
    assert(mt_run() == MT_OK);
    assert(g_event_count == 4);
    assert(g_events[0] == 1 && g_events[1] == 2 && g_events[2] == 3 && g_events[3] == 4);
    expect_clean_runtime();
    teardown();
}

static void run_stack_config_tests(void) {
    size_t sizes[2] = {0, 0};
    int value = 0;

    reset_globals();
    assert(mt_init() == MT_OK);
    assert(mt_go(stack_probe_task, sizes) > 0);
    assert(mt_run() == MT_OK);
    assert(sizes[0] >= MT_DEFAULT_STACK_SIZE);
#if !defined(_WIN32)
    assert(sizes[1] >= 4096u);
#endif
    expect_clean_runtime();
    teardown();

    reset_globals();
    assert(mt_init() == MT_OK);
    assert(mt_go_with_stack(stack_probe_task, sizes, 128u * 1024u) > 0);
    assert(mt_run() == MT_OK);
    assert(sizes[0] >= 128u * 1024u);
    expect_clean_runtime();
    teardown();

    reset_globals();
    assert(mt_init() == MT_OK);
    assert(mt_go_with_stack(minimum_stack_task, &value, MT_MIN_STACK_SIZE) > 0);
    assert(mt_run() == MT_OK);
    assert(value == 7);
    expect_clean_runtime();
    teardown();

    reset_globals();
    assert(mt_init() == MT_OK);
    assert(mt_go_with_stack(simple_inc, &value, 1) == MT_ERR_INVALID);
    assert(mt_debug_runnable_count() == 0);
    assert(mt_debug_live_task_count() == 0);
    teardown();

    reset_globals();
    value = 0;
    assert(mt_go_with_stack(simple_inc, &value, 0) > 0);
    assert(mt_run() == MT_OK);
    assert(value == 1);
    expect_clean_runtime();
    teardown();

    reset_globals();
    value = 0;
    int rc = mt_go_with_stack(simple_inc, &value, 64u * 1024u * 1024u);
    if (rc > 0) {
        assert(mt_run() == MT_OK);
        assert(value == 1);
    } else {
        assert(rc == MT_ERR_NOMEM || rc == MT_ERR);
        assert(mt_debug_runnable_count() == 0);
        assert(mt_debug_live_task_count() == 0);
    }
    expect_clean_runtime();
    teardown();

    reset_globals();
    enum { MIXED = 12 };
    mixed_stack_arg_t args[MIXED];
    int sum = 0;
    const size_t stack_sizes[] = { MT_DEFAULT_STACK_SIZE, 128u * 1024u, 256u * 1024u };
    assert(mt_init() == MT_OK);
    for (int i = 0; i < MIXED; ++i) {
        args[i].id = i + 1;
        args[i].iterations = 3;
        args[i].sum = &sum;
        assert(mt_go_with_stack(mixed_stack_task, &args[i], stack_sizes[i % 3]) > 0);
    }
    assert(mt_run() == MT_OK);
    assert(sum == ((MIXED * (MIXED + 1)) / 2) + MIXED * 3);
    expect_clean_runtime();
    teardown();
}

static void run_guard_page_tests(void) {
    size_t sizes[2] = {0, 0};
    reset_globals();
    assert(mt_init() == MT_OK);
    assert(mt_go(stack_probe_task, sizes) > 0);
    assert(mt_run() == MT_OK);
#if !defined(_WIN32)
    assert(sizes[1] >= 4096u);
#else
    assert(sizes[1] > 0);
#endif
    expect_clean_runtime();
    teardown();
}

static void run_basic_sleep_tests(void) {
    reset_globals();
    assert(mt_init() == MT_OK);
    assert(mt_go(sleep_zero_a, NULL) > 0);
    assert(mt_go(sleep_zero_b, NULL) > 0);
    assert(mt_run() == MT_OK);
    assert(g_event_count == 3);
    assert(g_events[0] == 1 && g_events[1] == 2 && g_events[2] == 3);
    expect_clean_runtime();
    teardown();

    reset_globals();
    assert(mt_init() == MT_OK);
    assert(mt_go(timed_sleep_task, g_times) > 0);
    assert(mt_run() == MT_OK);
    assert(g_times[1] >= g_times[0]);
    assert(g_times[1] - g_times[0] + (20u * MS_TO_NS) / 4u >= 20u * MS_TO_NS);
    expect_clean_runtime();
    teardown();

    reset_globals();
    mt_sleep_ms(1);
    assert(mt_init() == MT_OK);
    expect_clean_runtime();
    teardown();

    reset_globals();
    assert(mt_init() == MT_OK);
    assert(mt_go(multi_sleep_task, g_times) > 0);
    assert(mt_run() == MT_OK);
    assert(g_times[1] - g_times[0] + (5u * MS_TO_NS) >= 25u * MS_TO_NS);
    expect_clean_runtime();
    teardown();

    reset_globals();
    assert(mt_init() == MT_OK);
    for (int i = 0; i < 100; ++i) {
        assert(mt_go(same_duration_sleep_task, &g_counter) > 0);
    }
    assert(mt_run() == MT_OK);
    assert(g_counter == 100);
    expect_clean_runtime();
    teardown();
}

static void run_timer_ordering_tests(void) {
    reset_globals();
    sleep_order_arg_t args1[] = {{1, 30}, {2, 5}};
    assert(mt_init() == MT_OK);
    assert(mt_go(sleep_order_task, &args1[0]) > 0);
    assert(mt_go(sleep_order_task, &args1[1]) > 0);
    assert(mt_run() == MT_OK);
    assert(g_event_count == 2);
    assert(g_events[0] == 2 && g_events[1] == 1);
    expect_clean_runtime();
    teardown();

    reset_globals();
    sleep_order_arg_t args2[] = {{1, 50}, {2, 10}, {3, 30}, {4, 20}, {5, 40}};
    assert(mt_init() == MT_OK);
    for (size_t i = 0; i < ARRAY_LEN(args2); ++i) {
        assert(mt_go(sleep_order_task, &args2[i]) > 0);
    }
    assert(mt_run() == MT_OK);
    assert(g_event_count == ARRAY_LEN(args2));
    assert(g_events[0] == 2 && g_events[1] == 4 && g_events[2] == 3 && g_events[3] == 5 && g_events[4] == 1);
    expect_clean_runtime();
    teardown();

    reset_globals();
    assert(mt_init() == MT_OK);
    for (int i = 0; i < 8; ++i) {
        assert(mt_go(same_duration_sleep_task, &g_counter) > 0);
    }
    assert(mt_run() == MT_OK);
    assert(g_counter == 8);
    expect_clean_runtime();
    teardown();

    reset_globals();
    assert(mt_init() == MT_OK);
    for (int i = 0; i < 100; ++i) {
        assert(mt_go(same_duration_sleep_task, &g_counter) > 0);
    }
    assert(mt_run() == MT_OK);
    assert(g_counter == 100);
    expect_clean_runtime();
    teardown();

    reset_globals();
    assert(mt_init() == MT_OK);
    assert(mt_go(timed_sleep_task, g_times) > 0);
    assert(mt_run() == MT_OK);
    assert(g_times[1] >= g_times[0]);
    expect_clean_runtime();
    teardown();

    reset_globals();
    assert(mt_init() == MT_OK);
    assert(mt_go(sleeping_count_sleeper, NULL) > 0);
    assert(mt_go(sleeping_count_observer, NULL) > 0);
    assert(mt_run() == MT_OK);
    assert(g_event_count == 2);
    assert(g_events[0] == 1 && g_events[1] == 2);
    expect_clean_runtime();
    teardown();
}

static void run_scheduler_interaction_tests(void) {
    reset_globals();
    assert(mt_init() == MT_OK);
    assert(mt_go(ready_while_sleeping_a, NULL) > 0);
    assert(mt_go(ready_while_sleeping_b, NULL) > 0);
    assert(mt_run() == MT_OK);
    assert(g_event_count == 4);
    assert(g_events[0] == 1 && g_events[1] == 2 && g_events[2] == 3 && g_events[3] == 4);
    expect_clean_runtime();
    teardown();

    reset_globals();
    assert(mt_init() == MT_OK);
    assert(mt_go(timed_sleep_task, g_times) > 0);
    assert(mt_run() == MT_OK);
    assert(g_times[1] - g_times[0] + (20u * MS_TO_NS) / 4u >= 20u * MS_TO_NS);
    expect_clean_runtime();
    teardown();

    reset_globals();
    assert(mt_init() == MT_OK);
    for (int i = 0; i < 4; ++i) {
        assert(mt_go(same_duration_sleep_task, &g_counter) > 0);
    }
    assert(mt_run() == MT_OK);
    assert(g_counter == 4);
    expect_clean_runtime();
    teardown();

    reset_globals();
    int child_event = 2;
    assert(mt_init() == MT_OK);
    assert(mt_go(parent_create_then_sleep, &child_event) > 0);
    assert(mt_run() == MT_OK);
    assert(g_event_count == 3);
    assert(g_events[0] == 1 && g_events[1] == 2 && g_events[2] == 3);
    expect_clean_runtime();
    teardown();

    reset_globals();
    child_event = 3;
    assert(mt_init() == MT_OK);
    assert(mt_go(parent_create_after_wake, &child_event) > 0);
    assert(mt_run() == MT_OK);
    assert(g_event_count == 3);
    assert(g_events[0] == 1 && g_events[1] == 2 && g_events[2] == 3);
    expect_clean_runtime();
    teardown();

    reset_globals();
    assert(mt_init() == MT_OK);
    assert(mt_go(sleep_then_yield_task, NULL) > 0);
    assert(mt_go(sleep_then_yield_peer, NULL) > 0);
    assert(mt_run() == MT_OK);
    assert(g_event_count == 4);
    assert(g_events[0] == 1 && g_events[1] == 3 && g_events[2] == 2 && g_events[3] == 4);
    expect_clean_runtime();
    teardown();

    reset_globals();
    assert(mt_init() == MT_OK);
    assert(mt_go(yield_then_sleep_task, NULL) > 0);
    assert(mt_go(yield_then_sleep_peer, NULL) > 0);
    assert(mt_run() == MT_OK);
    assert(g_event_count == 4);
    assert(g_events[0] == 1 && g_events[1] == 2 && g_events[2] == 3 && g_events[3] == 4);
    expect_clean_runtime();
    teardown();
}

static void run_lifecycle_tests(void) {
    reset_globals();
    assert(mt_init() == MT_OK);
    assert(mt_go(timed_sleep_task, g_times) > 0);
    assert(mt_run() == MT_OK);
    assert(mt_debug_completed_task_count() == 1);
    expect_clean_runtime();
    teardown();

    reset_globals();
    assert(mt_init() == MT_OK);
    assert(mt_go(timed_sleep_task, g_times) > 0);
    mt_shutdown();
    assert(mt_init() == MT_OK);
    expect_clean_runtime();
    teardown();

    reset_globals();
    assert(mt_init() == MT_OK);
    assert(mt_go(shutdown_from_woken_task, NULL) > 0);
    assert(mt_run() == MT_OK);
    assert(g_event_count == 1 && g_events[0] == 1);
    expect_clean_runtime();
    teardown();

    for (int i = 0; i < 100; ++i) {
        reset_globals();
        assert(mt_init() == MT_OK);
        assert(mt_go(same_duration_sleep_task, &g_counter) > 0);
        assert(mt_run() == MT_OK);
        assert(g_counter == 1);
        expect_clean_runtime();
        teardown();
    }
}

static void run_error_tests(void) {
    int value = 0;

    reset_globals();
    mt_sleep_ms(1);
    assert(mt_init() == MT_OK);
    expect_clean_runtime();
    teardown();

    reset_globals();
    assert(mt_go_with_stack(simple_inc, &value, MT_MIN_STACK_SIZE) > 0);
    assert(mt_run() == MT_OK);
    assert(value == 1);
    expect_clean_runtime();
    teardown();

    reset_globals();
    assert(mt_init() == MT_OK);
    assert(mt_go_with_stack(NULL, &value, MT_MIN_STACK_SIZE) == MT_ERR_INVALID);
    assert(mt_debug_runnable_count() == 0);
    assert(mt_debug_live_task_count() == 0);
    teardown();

    reset_globals();
    assert(mt_init() == MT_OK);
    mt_test_fail_next_stack_alloc();
    assert(mt_go_with_stack(simple_inc, &value, MT_MIN_STACK_SIZE) == MT_ERR_NOMEM);
    assert(mt_debug_runnable_count() == 0);
    assert(mt_debug_live_task_count() == 0);
    assert(mt_go(simple_inc, &value) > 0);
    assert(mt_run() == MT_OK);
    expect_clean_runtime();
    teardown();

    reset_globals();
    assert(mt_init() == MT_OK);
    assert(mt_go(timer_alloc_failure_task, g_times) > 0);
    assert(mt_go(timer_alloc_failure_peer, NULL) > 0);
    assert(mt_run() == MT_OK);
    assert(g_event_count == 2);
    assert(g_events[0] == 1 && g_events[1] == 2);
    assert(g_times[1] >= g_times[0]);
    expect_clean_runtime();
    teardown();

    reset_globals();
    value = 0;
    assert(mt_init() == MT_OK);
    assert(mt_go(clock_failure_sleep_task, &value) > 0);
    assert(mt_run() == MT_OK);
    assert(value == 99);
    expect_clean_runtime();
    teardown();
}

static void run_stress_tests(void) {
    reset_globals();
    sleep_order_arg_t *many_args = (sleep_order_arg_t *)calloc((size_t)V02_MANY_SLEEPERS, sizeof(*many_args));
    assert(many_args != NULL);
    assert(mt_init() == MT_OK);
    for (int i = 0; i < V02_MANY_SLEEPERS; ++i) {
        sleep_order_arg_t *arg = &many_args[i];
        arg->id = i % 1000;
        arg->ms = (unsigned)(i % 21);
        assert(mt_go(sleep_order_task, arg) > 0);
    }
    assert(mt_run() == MT_OK);
    assert(g_event_count == (size_t)V02_MANY_SLEEPERS);
    expect_clean_runtime();
    teardown();
    free(many_args);

    reset_globals();
    int done = 0;
    assert(mt_init() == MT_OK);
    for (int i = 0; i < V02_SLEEP_YIELD_TASKS; ++i) {
        assert(mt_go(sleep_yield_cycle_task, &done) > 0);
    }
    assert(mt_run() == MT_OK);
    assert(done == V02_SLEEP_YIELD_TASKS);
    expect_clean_runtime();
    teardown();

    reset_globals();
    done = 0;
    churn_arg_t args[V02_TIMER_CHURN_TASKS];
    assert(mt_init() == MT_OK);
    for (int i = 0; i < V02_TIMER_CHURN_TASKS; ++i) {
        args[i].seed = (unsigned)(i + 1);
        args[i].iterations = V02_TIMER_CHURN_ITERS;
        args[i].done = &done;
        assert(mt_go(churn_task, &args[i]) > 0);
    }
    assert(mt_run() == MT_OK);
    assert(done == V02_TIMER_CHURN_TASKS);
    expect_clean_runtime();
    teardown();

    for (int i = 0; i < V02_RUNTIME_CYCLES; ++i) {
        reset_globals();
        assert(mt_init() == MT_OK);
        assert(mt_go(same_duration_sleep_task, &g_counter) > 0);
        assert(mt_run() == MT_OK);
        assert(g_counter == 1);
        expect_clean_runtime();
        teardown();
    }
}

static void run_backend_and_memory_tests(void) {
    memory_counters_t before;

    size_t sizes[2] = {0, 0};
    reset_globals();
    before = memory_snapshot();
    assert(mt_init() == MT_OK);
    assert(mt_go(stack_probe_task, sizes) > 0);
    assert(mt_run() == MT_OK);
#if !defined(_WIN32)
    assert(sizes[1] >= 4096u);
#endif
    expect_clean_runtime();
    teardown();
    expect_memory_balanced_since(before);

    reset_globals();
    before = memory_snapshot();
    assert(mt_init() == MT_OK);
    assert(mt_go(timed_sleep_task, g_times) > 0);
    assert(mt_run() == MT_OK);
    assert(g_times[1] - g_times[0] < UINT64_C(5000000000));
    expect_clean_runtime();
    teardown();
    expect_memory_balanced_since(before);

    reset_globals();
    before = memory_snapshot();
    assert(mt_init() == MT_OK);
    for (int i = 0; i < 100; ++i) {
        assert(mt_go_with_stack(simple_inc, &g_counter, (i % 2) ? MT_DEFAULT_STACK_SIZE : 128u * 1024u) > 0);
    }
    assert(mt_run() == MT_OK);
    assert(g_counter == 100);
    expect_clean_runtime();
    teardown();
    expect_memory_balanced_since(before);
}

static void run_misuse_tests(void) {
    reset_globals();
    assert(mt_init() == MT_OK);
    assert(mt_go(long_os_sleep_task, NULL) > 0);
    assert(mt_go(ready_peer_task, NULL) > 0);
    assert(mt_run() == MT_OK);
    assert(g_event_count == 3);
    assert(g_events[0] == 1 && g_events[1] == 2 && g_events[2] == 3);
    expect_clean_runtime();
    teardown();

    reset_globals();
    assert(mt_init() == MT_OK);
    assert(mt_go(busy_loop_task, NULL) > 0);
    assert(mt_go(ready_peer_task, NULL) > 0);
    assert(mt_run() == MT_OK);
    assert(g_event_count == 3);
    assert(g_events[0] == 1 && g_events[1] == 2 && g_events[2] == 3);
    expect_clean_runtime();
    teardown();

    reset_globals();
    assert(mt_init() == MT_OK);
    assert(mt_go(escape_stack_pointer_task, NULL) > 0);
    assert(mt_go(stack_escape_documentation_observer, NULL) > 0);
    assert(mt_run() == MT_OK);
    assert(g_event_count == 2);
    assert(g_events[0] == 1 && g_events[1] == 2);
    assert(g_escaped_stack_pointer != 0);
    expect_clean_runtime();
    teardown();
}

int main(void) {
    run_regression_tests();
    run_stack_config_tests();
    run_guard_page_tests();
    run_basic_sleep_tests();
    run_timer_ordering_tests();
    run_scheduler_interaction_tests();
    run_lifecycle_tests();
    run_error_tests();
    run_stress_tests();
    run_backend_and_memory_tests();
    run_misuse_tests();

    printf("v0.2 tests passed\n");
    return 0;
}

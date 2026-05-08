#ifndef MT_TESTING
#define MT_TESTING
#endif
#include "microthread.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#define ARRAY_LEN(a) (sizeof(a) / sizeof((a)[0]))

static int g_value;
static int g_value2;
static int g_counter;
static int g_errors;
static char g_trace[16384];
static size_t g_trace_len;

#if defined(MT_FULL_STRESS)
enum {
    TEST_MANY_CHILDREN = 1000,
    TEST_TIGHT_YIELD_TASKS = 10,
    TEST_TIGHT_YIELDS = 1000,
    TEST_INDEPENDENT_STACK_TASKS = 100,
    /* v0.1 uses fixed 64 KiB stacks.  These are the largest practical
     * always-on extended counts for ordinary developer/CI machines; a
     * 100,000-task run would reserve roughly 6.4 GiB of stack memory. */
    TEST_VERY_LARGE_TASKS = 5000,
    TEST_MANY_YIELDS = 200000,
    TEST_STRESS_TASKS = 1000,
    TEST_STRESS_YIELDS = 50,
    TEST_INIT_CYCLES = 200,
    TEST_TREE_DEPTH = 10
};
#else
enum {
    TEST_MANY_CHILDREN = 1000,
    TEST_TIGHT_YIELD_TASKS = 10,
    TEST_TIGHT_YIELDS = 1000,
    TEST_INDEPENDENT_STACK_TASKS = 100,
    TEST_VERY_LARGE_TASKS = 3000,
    TEST_MANY_YIELDS = 100000,
    TEST_STRESS_TASKS = 300,
    TEST_STRESS_YIELDS = 10,
    TEST_INIT_CYCLES = 50,
    TEST_TREE_DEPTH = 10
};
#endif

static void trace_char(char c) {
    assert(g_trace_len + 1 < sizeof(g_trace));
    g_trace[g_trace_len++] = c;
    g_trace[g_trace_len] = '\0';
}

static void reset_globals(void) {
    g_value = 0;
    g_value2 = 0;
    g_counter = 0;
    g_errors = 0;
    memset(g_trace, 0, sizeof(g_trace));
    g_trace_len = 0;
}

static void expect_clean_runtime(void) {
    assert(mt_debug_runnable_count() == 0);
    assert(mt_debug_live_task_count() == 0);
    assert(mt_debug_current_task_id() == 0);
}

static void one_shot(void *arg) {
    int *value = (int *)arg;
    *value += 1;
}

static void accepts_null(void *arg) {
    assert(arg == NULL);
    g_value = 1234;
}

static void trace_arg_char(void *arg) {
    char c = *(char *)arg;
    trace_char(c);
}

static void yielding_a(void *arg) {
    (void)arg;
    trace_char('A');
    mt_yield();
    trace_char('C');
}

static void yielding_b(void *arg) {
    (void)arg;
    trace_char('B');
    mt_yield();
    trace_char('D');
}

static void single_yield_task(void *arg) {
    int *value = (int *)arg;
    *value += 1;
    mt_yield();
    *value += 10;
}

static void yield_first_task(void *arg) {
    int *value = (int *)arg;
    mt_yield();
    *value = 7;
}

static void yield_last_task(void *arg) {
    int *value = (int *)arg;
    *value = 8;
    mt_yield();
}

static void stack_preserver(void *arg) {
    int *out = (int *)arg;
    int local = 41;
    mt_yield();
    local += 1;
    mt_yield();
    *out = local;
}

static void child_task(void *arg) {
    int *value = (int *)arg;
    *value += 10;
}

static void parent_task_yielding(void *arg) {
    int *value = (int *)arg;
    *value += 1;
    int id = mt_go(child_task, value);
    assert(id > 0);
    mt_yield();
    *value += 100;
}

static void parent_task_returning(void *arg) {
    int *value = (int *)arg;
    *value += 1;
    assert(mt_go(child_task, value) > 0);
}

static void spawn_one_increment(void *arg) {
    int *value = (int *)arg;
    *value += 1;
}

static void parent_many_children(void *arg) {
    int *value = (int *)arg;
    for (int i = 0; i < TEST_MANY_CHILDREN; ++i) {
        assert(mt_go(spawn_one_increment, value) > 0);
    }
}

static void tight_yield_task(void *arg) {
    int *value = (int *)arg;
    for (int i = 0; i < TEST_TIGHT_YIELDS; ++i) {
        *value += 1;
        mt_yield();
    }
}

static void reentrant_run_task(void *arg) {
    int *out = (int *)arg;
    *out = mt_run();
}

static void normal_return_task(void *arg) {
    int *value = (int *)arg;
    *value = 42;
}

static void mixed_lifecycle_a(void *arg) {
    (void)arg;
    trace_char('A');
}

static void mixed_lifecycle_b(void *arg) {
    (void)arg;
    trace_char('B');
    mt_yield();
    trace_char('D');
}

static void mixed_lifecycle_c(void *arg) {
    (void)arg;
    trace_char('C');
}

typedef struct pair_arg {
    int a;
    int b;
    int result;
} pair_arg_t;

static void struct_arg_task(void *arg) {
    pair_arg_t *p = (pair_arg_t *)arg;
    p->result = p->a + p->b;
}

static void stack_arg_task(void *arg) {
    int *value = (int *)arg;
    *value += 33;
}

static int deep_call_chain(int depth, int value) {
    volatile int local = value + depth;
    if (depth == 0) {
        mt_yield();
        return local;
    }
    return local + deep_call_chain(depth - 1, value);
}

static void deep_stack_task(void *arg) {
    int *out = (int *)arg;
    *out = deep_call_chain(32, 3);
}

static int recursive_sum(int n) {
    volatile int local = n;
    if (n == 0) {
        mt_yield();
        return 0;
    }
    return local + recursive_sum(n - 1);
}

static void recursion_task(void *arg) {
    int *out = (int *)arg;
    *out = recursive_sum(48);
}

static void large_local_array_task(void *arg) {
    int *out = (int *)arg;
    unsigned char buffer[16 * 1024];
    for (size_t i = 0; i < sizeof(buffer); ++i) {
        buffer[i] = (unsigned char)(i & 0xffu);
    }
    mt_yield();
    int sum = 0;
    for (size_t i = 0; i < sizeof(buffer); i += 257) {
        sum += buffer[i];
    }
    *out = sum;
}

static void independent_stack_task(void *arg) {
    int id = *(int *)arg;
    int local = id * 100;
    mt_yield();
    if (local != id * 100) {
        g_errors++;
    }
    g_counter += 1;
}

static void no_yield_trace_task(void *arg) {
    char c = *(char *)arg;
    trace_char(c);
}

static void yield_trace_task(void *arg) {
    char c = *(char *)arg;
    trace_char(c);
    mt_yield();
    trace_char((char)(c + ('a' - 'A')));
}

static void long_running_no_yield_task(void *arg) {
    (void)arg;
    volatile unsigned long sink = 0;
    for (unsigned long i = 0; i < 200000UL; ++i) {
        sink += i;
    }
    if (sink == 0) {
        g_errors++;
    }
    trace_char('A');
}

static void yield_heavy_task(void *arg) {
    (void)arg;
    for (int i = 0; i < 32; ++i) {
        mt_yield();
    }
    trace_char('Y');
}

static void normal_after_yield_heavy_task(void *arg) {
    (void)arg;
    trace_char('N');
}

static void many_yields_task(void *arg) {
    int *value = (int *)arg;
    for (int i = 0; i < TEST_MANY_YIELDS; ++i) {
        mt_yield();
    }
    *value = 1;
}

static void create_after_many_yields_task(void *arg) {
    int *value = (int *)arg;
    for (int i = 0; i < 25; ++i) {
        mt_yield();
    }
    assert(mt_go(child_task, value) > 0);
}

static void floating_point_task(void *arg) {
    double *out = (double *)arg;
    double x = 1.25;
    for (int i = 0; i < 20; ++i) {
        x = x * 1.125 + 0.5;
        mt_yield();
    }
    *out = x;
}

static void register_preservation_task(void *arg) {
    long *out = (long *)arg;
    volatile long a = 11;
    volatile long b = 22;
    volatile long c = 33;
    volatile long d = 44;
    mt_yield();
    *out = a + b + c + d;
}

static void helper_callback(void (*cb)(void *), void *arg) {
    cb(arg);
}

static void yielding_callback(void *arg) {
    int *value = (int *)arg;
    *value += 1;
    mt_yield();
    *value += 10;
}

static void callback_task(void *arg) {
    helper_callback(yielding_callback, arg);
}

static void child_sets_flag(void *arg) {
    int *value = (int *)arg;
    *value = 1;
}

static void parent_child_completes_first(void *arg) {
    int *value = (int *)arg;
    assert(mt_go(child_sets_flag, value) > 0);
    mt_yield();
    assert(*value == 1);
    *value = 2;
}

static void stress_task(void *arg) {
    int *value = (int *)arg;
    for (int i = 0; i < TEST_STRESS_YIELDS; ++i) {
        *value += 1;
        mt_yield();
    }
}

static void randomized_yield_task(void *arg) {
    int id = *(int *)arg;
    unsigned state = (unsigned)(id * 1103515245u + 12345u);
    for (int i = 0; i < 32; ++i) {
        state = state * 1664525u + 1013904223u;
        if ((state & 3u) != 0u) {
            mt_yield();
        }
    }
    g_counter += 1;
}

typedef struct tree_arg {
    int depth;
    int *count;
} tree_arg_t;

static tree_arg_t g_tree_nodes[4096];
static int g_tree_next_node;

static void recursive_tree_task(void *arg) {
    tree_arg_t *node = (tree_arg_t *)arg;
    *node->count += 1;
    if (node->depth > 0) {
        int left = g_tree_next_node++;
        int right = g_tree_next_node++;
        assert((size_t)right < ARRAY_LEN(g_tree_nodes));
        g_tree_nodes[left].depth = node->depth - 1;
        g_tree_nodes[left].count = node->count;
        g_tree_nodes[right].depth = node->depth - 1;
        g_tree_nodes[right].count = node->count;
        assert(mt_go(recursive_tree_task, &g_tree_nodes[left]) > 0);
        assert(mt_go(recursive_tree_task, &g_tree_nodes[right]) > 0);
    }
    mt_yield();
}

static void shutdown_from_task(void *arg) {
    int *value = (int *)arg;
    mt_shutdown();
    *value = 1;
}

static void blocking_sleep_task(void *arg) {
    int *value = (int *)arg;
    /* Deliberately no OS sleep here: the contract test verifies that a
     * non-yielding task runs to completion before the next task.  A real
     * blocking sleep would only make the unit test slow and environment
     * dependent while proving the same v0.1 scheduler rule. */
    volatile unsigned long sink = 0;
    for (unsigned long i = 0; i < 100000UL; ++i) {
        sink += i;
    }
    (void)sink;
    trace_char('S');
    *value += 1;
}

static void stack_pointer_probe_task(void *arg) {
    int *ok = (int *)arg;
#if defined(__SANITIZE_ADDRESS__)
    /* ASan does not fully support makecontext/swapcontext and can place locals
     * on fake stacks, so this low-level stack-bound assertion is skipped under
     * ASan.  The sanitizer run still covers memory cleanup and task lifecycle. */
    *ok = 1;
    mt_yield();
#else
    int local = 0;
    unsigned char *base = (unsigned char *)mt_test_current_stack_base();
    size_t size = mt_test_current_stack_size();
    unsigned char *addr = (unsigned char *)&local;
    assert(base != NULL);
    assert(size > 0);
    *ok = (addr >= base && addr < base + size) ? 1 : 0;
    mt_yield();
    addr = (unsigned char *)&local;
    assert(*ok == 1);
    assert(addr >= base && addr < base + size);
#endif
}

static void run_init_shutdown_tests(void) {
    reset_globals();

    /* TC-INIT-001 */
    assert(mt_init() == MT_OK);
    expect_clean_runtime();
    mt_shutdown();
    expect_clean_runtime();

    /* TC-INIT-002 */
    assert(mt_init() == MT_OK);
    assert(mt_init() == MT_OK);
    assert(mt_go(one_shot, &g_value) > 0);
    assert(mt_run() == MT_OK);
    assert(g_value == 1);
    mt_shutdown();

    /* TC-INIT-003 */
    mt_shutdown();
    expect_clean_runtime();

    /* TC-INIT-004 */
    assert(mt_init() == MT_OK);
    mt_shutdown();
    assert(mt_init() == MT_OK);
    assert(mt_go(one_shot, &g_value) > 0);
    assert(mt_run() == MT_OK);
    assert(g_value == 2);
    mt_shutdown();
}

static void run_task_creation_tests(void) {
    reset_globals();

    /* TC-GO-001 */
    assert(mt_init() == MT_OK);
    assert(mt_go(one_shot, &g_value) > 0);
    assert(mt_debug_runnable_count() == 1);
    assert(mt_run() == MT_OK);
    assert(g_value == 1);
    assert(mt_debug_completed_task_count() == 1);
    mt_shutdown();

    /* TC-GO-002 */
    assert(mt_init() == MT_OK);
    for (int i = 0; i < 100; ++i) {
        assert(mt_go(one_shot, &g_value) > 0);
    }
    assert(mt_debug_runnable_count() == 100);
    assert(mt_run() == MT_OK);
    assert(g_value == 101);
    mt_shutdown();

    /* TC-GO-003 */
    assert(mt_init() == MT_OK);
    assert(mt_run() == MT_OK);
    expect_clean_runtime();
    mt_shutdown();

    /* TC-GO-004 */
    assert(mt_init() == MT_OK);
    assert(mt_go(NULL, NULL) == MT_ERR_INVALID);
    assert(mt_go(one_shot, &g_value) > 0);
    assert(mt_run() == MT_OK);
    assert(g_value == 102);
    mt_shutdown();

    /* TC-GO-005 */
    assert(mt_init() == MT_OK);
    assert(mt_go(accepts_null, NULL) > 0);
    assert(mt_run() == MT_OK);
    assert(g_value == 1234);
    mt_shutdown();

    /* TC-GO-006 */
    assert(mt_init() == MT_OK);
    g_value = 0;
    assert(mt_go(parent_task_yielding, &g_value) > 0);
    assert(mt_run() == MT_OK);
    assert(g_value == 111);
    mt_shutdown();

    /* TC-GO-007 */
    assert(mt_init() == MT_OK);
    g_value = 0;
    assert(mt_go(parent_many_children, &g_value) > 0);
    assert(mt_run() == MT_OK);
    assert(g_value == TEST_MANY_CHILDREN);
    mt_shutdown();
}

static void run_scheduler_tests(void) {
    reset_globals();

    /* TC-RUN-001 */
    assert(mt_init() == MT_OK);
    for (int i = 0; i < 10; ++i) {
        assert(mt_go(one_shot, &g_value) > 0);
    }
    assert(mt_run() == MT_OK);
    assert(g_value == 10);
    expect_clean_runtime();
    mt_shutdown();

    /* TC-RUN-002 */
    assert(mt_init() == MT_OK);
    assert(mt_go(one_shot, &g_value) > 0);
    assert(mt_run() == MT_OK);
    assert(mt_run() == MT_OK);
    assert(g_value == 11);
    mt_shutdown();

    /* TC-RUN-003 */
    assert(mt_init() == MT_OK);
    assert(mt_run() == MT_OK);
    assert(mt_go(one_shot, &g_value) > 0);
    assert(mt_run() == MT_OK);
    assert(g_value == 12);
    mt_shutdown();

    /* TC-RUN-004 */
    assert(mt_init() == MT_OK);
    assert(mt_go(one_shot, &g_value) > 0);
    assert(mt_run() == MT_OK);
    mt_shutdown();

    /* TC-RUN-005 */
    assert(mt_init() == MT_OK);
    g_value = 0;
    assert(mt_go(reentrant_run_task, &g_value) > 0);
    assert(mt_run() == MT_OK);
    assert(g_value == MT_ERR_STATE);
    mt_shutdown();
}

static void run_yield_tests(void) {
    reset_globals();

    /* TC-YIELD-001 */
    assert(mt_init() == MT_OK);
    assert(mt_go(single_yield_task, &g_value) > 0);
    assert(mt_run() == MT_OK);
    assert(g_value == 11);
    mt_shutdown();

    /* TC-YIELD-002 */
    assert(mt_init() == MT_OK);
    reset_globals();
    assert(mt_go(yielding_a, NULL) > 0);
    assert(mt_go(yielding_b, NULL) > 0);
    assert(mt_run() == MT_OK);
    assert(strcmp(g_trace, "ABCD") == 0);
    mt_shutdown();

    /* TC-YIELD-003 */
    assert(mt_init() == MT_OK);
    for (int i = 0; i < TEST_TIGHT_YIELD_TASKS; ++i) {
        assert(mt_go(tight_yield_task, &g_value) > 0);
    }
    assert(mt_run() == MT_OK);
    assert(g_value == TEST_TIGHT_YIELD_TASKS * TEST_TIGHT_YIELDS);
    mt_shutdown();

    /* TC-YIELD-004 */
    assert(mt_init() == MT_OK);
    assert(mt_go(yield_first_task, &g_value) > 0);
    assert(mt_run() == MT_OK);
    assert(g_value == 7);
    mt_shutdown();

    /* TC-YIELD-005 */
    assert(mt_init() == MT_OK);
    assert(mt_go(yield_last_task, &g_value) > 0);
    assert(mt_run() == MT_OK);
    assert(g_value == 8);
    mt_shutdown();

    /* TC-YIELD-006 */
    mt_yield();
    assert(mt_init() == MT_OK);
    mt_yield();
    assert(mt_run() == MT_OK);
    mt_shutdown();
}

static void run_lifecycle_tests(void) {
    reset_globals();

    /* TC-LIFE-001 */
    assert(mt_init() == MT_OK);
    assert(mt_go(normal_return_task, &g_value) > 0);
    assert(mt_run() == MT_OK);
    assert(g_value == 42);
    assert(mt_debug_live_task_count() == 0);
    assert(mt_debug_completed_task_count() == 1);
    mt_shutdown();

    /* TC-LIFE-002 */
    reset_globals();
    assert(mt_init() == MT_OK);
    assert(mt_go(mixed_lifecycle_a, NULL) > 0);
    assert(mt_go(mixed_lifecycle_b, NULL) > 0);
    assert(mt_go(mixed_lifecycle_c, NULL) > 0);
    assert(mt_run() == MT_OK);
    assert(strcmp(g_trace, "ABCD") == 0);
    mt_shutdown();

    /* TC-LIFE-003 */
    assert(mt_init() == MT_OK);
    g_value = 0;
    assert(mt_go(parent_task_returning, &g_value) > 0);
    assert(mt_run() == MT_OK);
    assert(g_value == 11);
    mt_shutdown();

    /* TC-LIFE-004 */
    assert(mt_init() == MT_OK);
    assert(mt_go(normal_return_task, &g_value) > 0);
    assert(mt_run() == MT_OK);
    assert(mt_debug_current_task_id() == 0);
    assert(mt_debug_live_task_count() == 0);
    mt_shutdown();
}

static void run_argument_tests(void) {
    reset_globals();

    /* TC-ARG-001 */
    assert(mt_init() == MT_OK);
    pair_arg_t pair = { .a = 17, .b = 25, .result = 0 };
    assert(mt_go(struct_arg_task, &pair) > 0);
    assert(mt_run() == MT_OK);
    assert(pair.result == 42);
    mt_shutdown();

    /* TC-ARG-002 */
    assert(mt_init() == MT_OK);
    char args[] = { 'A', 'B', 'C', 'D', 'E' };
    for (size_t i = 0; i < ARRAY_LEN(args); ++i) {
        assert(mt_go(trace_arg_char, &args[i]) > 0);
    }
    assert(mt_run() == MT_OK);
    assert(strcmp(g_trace, "ABCDE") == 0);
    mt_shutdown();

    /* TC-ARG-003 */
    assert(mt_init() == MT_OK);
    int local = 9;
    assert(mt_go(stack_arg_task, &local) > 0);
    assert(mt_run() == MT_OK);
    assert(local == 42);
    mt_shutdown();
}

static void run_stack_tests(void) {
    reset_globals();

    /* TC-STACK-001 */
    assert(mt_init() == MT_OK);
    assert(mt_go(stack_preserver, &g_value) > 0);
    assert(mt_run() == MT_OK);
    assert(g_value == 42);
    mt_shutdown();

    /* TC-STACK-002 */
    assert(mt_init() == MT_OK);
    assert(mt_go(deep_stack_task, &g_value) > 0);
    assert(mt_run() == MT_OK);
    assert(g_value > 0);
    mt_shutdown();

    /* TC-STACK-003 */
    assert(mt_init() == MT_OK);
    assert(mt_go(recursion_task, &g_value) > 0);
    assert(mt_run() == MT_OK);
    assert(g_value == (48 * 49) / 2);
    mt_shutdown();

    /* TC-STACK-004 */
    assert(mt_init() == MT_OK);
    assert(mt_go(large_local_array_task, &g_value) > 0);
    assert(mt_run() == MT_OK);
    assert(g_value > 0);
    mt_shutdown();

    /* TC-STACK-005 */
    assert(mt_init() == MT_OK);
    int ids[TEST_INDEPENDENT_STACK_TASKS];
    g_counter = 0;
    g_errors = 0;
    for (int i = 0; i < TEST_INDEPENDENT_STACK_TASKS; ++i) {
        ids[i] = i + 1;
        assert(mt_go(independent_stack_task, &ids[i]) > 0);
    }
    assert(mt_run() == MT_OK);
    assert(g_counter == TEST_INDEPENDENT_STACK_TASKS);
    assert(g_errors == 0);
    mt_shutdown();
}

static void run_fairness_tests(void) {
    reset_globals();

    /* TC-FAIR-001 */
    assert(mt_init() == MT_OK);
    char args[] = { 'A', 'B', 'C' };
    for (size_t i = 0; i < ARRAY_LEN(args); ++i) {
        assert(mt_go(no_yield_trace_task, &args[i]) > 0);
    }
    assert(mt_run() == MT_OK);
    assert(strcmp(g_trace, "ABC") == 0);
    mt_shutdown();

    /* TC-FAIR-002 */
    reset_globals();
    assert(mt_init() == MT_OK);
    assert(mt_go(yield_trace_task, &args[0]) > 0);
    assert(mt_go(yield_trace_task, &args[1]) > 0);
    assert(mt_go(yield_trace_task, &args[2]) > 0);
    assert(mt_run() == MT_OK);
    assert(strcmp(g_trace, "ABCabc") == 0);
    mt_shutdown();

    /* TC-FAIR-003 */
    reset_globals();
    assert(mt_init() == MT_OK);
    assert(mt_go(long_running_no_yield_task, NULL) > 0);
    assert(mt_go(no_yield_trace_task, &args[1]) > 0);
    assert(mt_run() == MT_OK);
    assert(strcmp(g_trace, "AB") == 0);
    mt_shutdown();

    /* TC-FAIR-004 */
    reset_globals();
    assert(mt_init() == MT_OK);
    assert(mt_go(yield_heavy_task, NULL) > 0);
    assert(mt_go(normal_after_yield_heavy_task, NULL) > 0);
    assert(mt_run() == MT_OK);
    assert(strcmp(g_trace, "NY") == 0);
    mt_shutdown();
}

static void run_error_and_shutdown_tests(void) {
    reset_globals();

    /* TC-ERR-001: documented behavior is implicit init. */
    assert(mt_go(one_shot, &g_value) > 0);
    assert(mt_run() == MT_OK);
    assert(g_value == 1);
    mt_shutdown();

    /* TC-ERR-002: documented behavior is implicit init/no-op run. */
    assert(mt_run() == MT_OK);
    expect_clean_runtime();
    mt_shutdown();

    /* TC-ERR-003: allocation failure during task creation. */
    assert(mt_init() == MT_OK);
    mt_test_fail_next_task_alloc();
    assert(mt_go(one_shot, &g_value) == MT_ERR_NOMEM);
    expect_clean_runtime();
    mt_test_fail_next_stack_alloc();
    assert(mt_go(one_shot, &g_value) == MT_ERR_NOMEM);
    expect_clean_runtime();
    mt_test_reset_faults();
    mt_shutdown();

    /* TC-ERR-004: context creation failure after stack allocation. */
    assert(mt_init() == MT_OK);
    mt_test_fail_next_context_make();
    assert(mt_go(one_shot, &g_value) == MT_ERR);
    expect_clean_runtime();
    mt_test_reset_faults();
    mt_shutdown();

    /* TC-ERR-005 */
    assert(mt_init() == MT_OK);
    assert(mt_go(one_shot, &g_value) > 0);
    assert(mt_debug_live_task_count() == 1);
    mt_shutdown();
    expect_clean_runtime();

    /* TC-MISUSE-002: v0.1 makes shutdown from a microthread a safe no-op. */
    assert(mt_init() == MT_OK);
    assert(mt_go(shutdown_from_task, &g_value) > 0);
    assert(mt_run() == MT_OK);
    assert(g_value == 1);
    mt_shutdown();

    /* TC-MISUSE-001: blocking/non-yielding work blocks later tasks. */
    reset_globals();
    assert(mt_init() == MT_OK);
    assert(mt_go(blocking_sleep_task, &g_value) > 0);
    char b = 'B';
    assert(mt_go(no_yield_trace_task, &b) > 0);
    assert(mt_run() == MT_OK);
    assert(strcmp(g_trace, "SB") == 0);
    assert(g_value == 1);
    mt_shutdown();
}

static void run_edge_tests(void) {
    reset_globals();

    /* TC-EDGE-001 */
    assert(mt_init() == MT_OK);
    assert(mt_go(one_shot, &g_value) > 0);
    assert(mt_run() == MT_OK);
    assert(g_value == 1);
    mt_shutdown();

    /* TC-EDGE-002 */
    assert(mt_init() == MT_OK);
    for (int i = 0; i < TEST_VERY_LARGE_TASKS; ++i) {
        assert(mt_go(one_shot, &g_value) > 0);
    }
    assert(mt_run() == MT_OK);
    assert(g_value == 1 + TEST_VERY_LARGE_TASKS);
    mt_shutdown();

    /* TC-EDGE-003 */
    assert(mt_init() == MT_OK);
    g_value = 0;
    assert(mt_go(many_yields_task, &g_value) > 0);
    assert(mt_run() == MT_OK);
    assert(g_value == 1);
    mt_shutdown();

    /* TC-EDGE-004 */
    assert(mt_init() == MT_OK);
    g_value = 0;
    assert(mt_go(create_after_many_yields_task, &g_value) > 0);
    assert(mt_run() == MT_OK);
    assert(g_value == 10);
    mt_shutdown();

    /* TC-EDGE-005 */
    assert(mt_init() == MT_OK);
    double out = 0.0;
    assert(mt_go(floating_point_task, &out) > 0);
    assert(mt_run() == MT_OK);
    assert(out > 0.0);
    assert(isfinite(out));
    mt_shutdown();

    /* TC-EDGE-006 */
    assert(mt_init() == MT_OK);
    long reg_out = 0;
    assert(mt_go(register_preservation_task, &reg_out) > 0);
    assert(mt_run() == MT_OK);
    assert(reg_out == 110);
    mt_shutdown();

    /* TC-EDGE-007 */
    assert(mt_init() == MT_OK);
    g_value = 0;
    assert(mt_go(callback_task, &g_value) > 0);
    assert(mt_run() == MT_OK);
    assert(g_value == 11);
    mt_shutdown();

    /* TC-EDGE-008 */
    assert(mt_init() == MT_OK);
    g_value = 0;
    assert(mt_go(parent_child_completes_first, &g_value) > 0);
    assert(mt_run() == MT_OK);
    assert(g_value == 2);
    mt_shutdown();
}

static void run_stress_tests(void) {
    reset_globals();

    /* TC-STRESS-001 */
    assert(mt_init() == MT_OK);
    for (int i = 0; i < TEST_STRESS_TASKS; ++i) {
        assert(mt_go(stress_task, &g_value) > 0);
    }
    assert(mt_run() == MT_OK);
    assert(g_value == TEST_STRESS_TASKS * TEST_STRESS_YIELDS);
    mt_shutdown();

    /* TC-STRESS-002 */
    for (int i = 0; i < TEST_INIT_CYCLES; ++i) {
        assert(mt_init() == MT_OK);
        assert(mt_go(one_shot, &g_value) > 0);
        assert(mt_run() == MT_OK);
        mt_shutdown();
    }
    assert(g_value == TEST_STRESS_TASKS * TEST_STRESS_YIELDS + TEST_INIT_CYCLES);

    /* TC-STRESS-003 */
    assert(mt_init() == MT_OK);
    enum { RANDOM_TASKS = 128 };
    int ids[RANDOM_TASKS];
    g_counter = 0;
    for (int i = 0; i < RANDOM_TASKS; ++i) {
        ids[i] = i + 1;
        assert(mt_go(randomized_yield_task, &ids[i]) > 0);
    }
    assert(mt_run() == MT_OK);
    assert(g_counter == RANDOM_TASKS);
    mt_shutdown();

    /* TC-STRESS-004 */
    assert(mt_init() == MT_OK);
    tree_arg_t root = { .depth = TEST_TREE_DEPTH, .count = &g_counter };
    g_counter = 0;
    g_tree_next_node = 0;
    assert(mt_go(recursive_tree_task, &root) > 0);
    assert(mt_run() == MT_OK);
    assert(g_counter == (1 << (TEST_TREE_DEPTH + 1)) - 1);
    mt_shutdown();
}

static void run_backend_smoke_tests(void) {
    reset_globals();

    /* TC-BACKEND-001 / TC-BACKEND-002 smoke through public API. */
    assert(mt_init() == MT_OK);
    assert(mt_go(stack_preserver, &g_value) > 0);
    assert(mt_go(single_yield_task, &g_value2) > 0);
    assert(mt_run() == MT_OK);
    assert(g_value == 42);
    assert(g_value2 == 11);
    mt_shutdown();

    /* TC-BACKEND-003: stack pointer preservation is indirectly covered by
     * stack, recursion, and local variable tests above. */
    assert(mt_init() == MT_OK);
    g_value = 0;
    assert(mt_go(stack_pointer_probe_task, &g_value) > 0);
    assert(mt_run() == MT_OK);
    assert(g_value == 1);
    mt_shutdown();

    /* TC-BACKEND-004 is exercised when this same test binary runs on Windows:
     * mt_init() must convert the main thread to a scheduler fiber, task fibers
     * must switch, and mt_shutdown() must convert the scheduler fiber back. */
}

int main(void) {
    run_init_shutdown_tests();
    run_task_creation_tests();
    run_scheduler_tests();
    run_yield_tests();
    run_lifecycle_tests();
    run_argument_tests();
    run_stack_tests();
    run_fairness_tests();
    run_error_and_shutdown_tests();
    run_edge_tests();
    run_stress_tests();
    run_backend_smoke_tests();

    puts("v0.1 comprehensive tests passed");
    return 0;
}

#if !defined(_WIN32) && !defined(_POSIX_C_SOURCE)
#define _POSIX_C_SOURCE 200809L
#endif

#include "gt.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>

static int g_value;
static size_t g_stack_size;
static size_t g_guard_size;
static int g_local_was_inside;

static void guard_disabled_task(void *arg) {
    (void)arg;
    int local = 42;
    void *base = gt_test_current_stack_base();
    size_t size = gt_test_current_stack_size();
    size_t guard = gt_test_current_stack_guard_size();

    g_stack_size = size;
    g_guard_size = guard;
    g_local_was_inside = base != NULL
        && (char *)&local >= (char *)base
        && (char *)&local < (char *)base + size;

    gt_yield();

    assert(local == 42);
    assert(gt_test_current_stack_guard_size() == 0);
    g_value = local;
}

static void sleep_task(void *arg) {
    int *done = (int *)arg;
    gt_sleep_ms(1);
    (*done)++;
}

int main(void) {
#if !defined(GT_DISABLE_GUARD_PAGES)
#error "test_guard_disabled.c must be built with -DGT_DISABLE_GUARD_PAGES"
#endif

    assert(gt_init() == GT_OK);
    assert(gt_go_with_stack(guard_disabled_task, NULL, GT_MIN_STACK_SIZE) > 0);
    assert(gt_run() == GT_OK);
    assert(g_value == 42);
    assert(g_stack_size >= GT_MIN_STACK_SIZE);
    assert(g_guard_size == 0);
#if !defined(_WIN32)
    assert(g_local_was_inside);
#else
    /* Windows Fibers manage their own stack; the debug base is intentionally NULL. */
    assert(!g_local_was_inside);
#endif
    assert(gt_debug_live_task_count() == 0);
    assert(gt_debug_sleeping_task_count() == 0);
    gt_shutdown();

    int done = 0;
    assert(gt_init() == GT_OK);
    for (int i = 0; i < 16; ++i) {
        assert(gt_go_with_stack(sleep_task, &done, GT_MIN_STACK_SIZE) > 0);
    }
    assert(gt_run() == GT_OK);
    assert(done == 16);
    assert(gt_debug_live_task_count() == 0);
    assert(gt_debug_sleeping_task_count() == 0);
    gt_shutdown();

    printf("guard-disabled fallback test passed\n");
    return 0;
}

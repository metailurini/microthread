#if !defined(_WIN32) && !defined(_POSIX_C_SOURCE)
#define _POSIX_C_SOURCE 200809L
#endif

#ifndef MT_TESTING
#define MT_TESTING
#endif
#include "microthread.h"
#include "microthread_testing.h"
#include "microthread_debug.h"

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
    void *base = mt_test_current_stack_base();
    size_t size = mt_test_current_stack_size();
    size_t guard = mt_test_current_stack_guard_size();

    g_stack_size = size;
    g_guard_size = guard;
    g_local_was_inside = base != NULL
        && (char *)&local >= (char *)base
        && (char *)&local < (char *)base + size;

    mt_yield();

    assert(local == 42);
    assert(mt_test_current_stack_guard_size() == 0);
    g_value = local;
}

static void sleep_task(void *arg) {
    int *done = (int *)arg;
    mt_sleep_ms(1);
    (*done)++;
}

int main(void) {
#if !defined(MT_DISABLE_GUARD_PAGES)
#error "test_guard_disabled.c must be built with -DMT_DISABLE_GUARD_PAGES"
#endif

    assert(mt_init() == MT_OK);
    assert(mt_go_with_stack(guard_disabled_task, NULL, MT_MIN_STACK_SIZE) > 0);
    assert(mt_run() == MT_OK);
    assert(g_value == 42);
    assert(g_stack_size >= MT_MIN_STACK_SIZE);
    assert(g_guard_size == 0);
#if !defined(_WIN32)
    assert(g_local_was_inside);
#else
    /* Windows Fibers manage their own stack; the debug base is intentionally NULL. */
    assert(!g_local_was_inside);
#endif
    assert(mt_debug_live_task_count() == 0);
    assert(mt_debug_sleeping_task_count() == 0);
    mt_shutdown();

    int done = 0;
    assert(mt_init() == MT_OK);
    for (int i = 0; i < 16; ++i) {
        assert(mt_go_with_stack(sleep_task, &done, MT_MIN_STACK_SIZE) > 0);
    }
    assert(mt_run() == MT_OK);
    assert(done == 16);
    assert(mt_debug_live_task_count() == 0);
    assert(mt_debug_sleeping_task_count() == 0);
    mt_shutdown();

    printf("guard-disabled fallback test passed\n");
    return 0;
}

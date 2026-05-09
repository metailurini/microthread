#ifndef MT_TESTING
#define MT_TESTING
#endif
#include "microthread.h"
#include "microthread_testing.h"
#include "test_helpers.h"

#include <pthread.h>
#include <sched.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>


#define ARRAY_LEN(a) (sizeof(a) / sizeof((a)[0]))

enum {
    WORKERS = 4,
    PAR_TASKS = 96,
    EXTERNAL_THREADS = 4,
    EXTERNAL_TASKS_PER_THREAD = 64,
    SPAWNER_TASKS = 16,
    CHILDREN_PER_SPAWNER = 16,
    PRODUCERS = 4,
    CONSUMERS = 4,
    ITEMS_PER_PRODUCER = 100,
    JOINERS = 8
};

static mt_chan_t *g_ch;
static mt_task_handle_t *g_handle;
static atomic_int g_counter;
static atomic_int g_counter2;
static atomic_int g_stop_anchor;
static atomic_int g_started;
static atomic_int g_done;
static atomic_int g_worker_count_seen;
static atomic_int g_rc;
static atomic_int g_rc2;
static atomic_int g_index_seen;
static atomic_int g_value_seen;
static atomic_long g_sum;

static pthread_mutex_t g_seen_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_t g_seen_threads[PAR_TASKS];
static size_t g_seen_count;

static void reset_runtime(void) {
    mt_shutdown();
    mt_test_reset_faults();
    CHECK(mt_init() == MT_OK);
    g_ch = NULL;
    g_handle = NULL;
    atomic_store(&g_counter, 0);
    atomic_store(&g_counter2, 0);
    atomic_store(&g_stop_anchor, 0);
    atomic_store(&g_started, 0);
    atomic_store(&g_done, 0);
    atomic_store(&g_worker_count_seen, -9999);
    atomic_store(&g_rc, 12345);
    atomic_store(&g_rc2, 12345);
    atomic_store(&g_index_seen, -1);
    atomic_store(&g_value_seen, -1);
    atomic_store(&g_sum, 0);
    pthread_mutex_lock(&g_seen_lock);
    memset(g_seen_threads, 0, sizeof(g_seen_threads));
    g_seen_count = 0;
    pthread_mutex_unlock(&g_seen_lock);
}

static void finish_runtime(void) {
    if (g_handle) {
        mt_task_handle_release(g_handle);
        g_handle = NULL;
    }
    if (g_ch) {
        CHECK(mt_chan_destroy(g_ch) == MT_OK);
        g_ch = NULL;
    }
    mt_shutdown();
    mt_test_reset_faults();
}

static void assert_core_counters_balanced(void) {
    size_t task_allocs = 0;
    size_t task_frees = 0;
    size_t stack_allocs = 0;
    size_t stack_frees = 0;
    size_t timer_allocs = 0;
    size_t timer_frees = 0;
    mt_test_memory_counters(&task_allocs, &task_frees,
                            &stack_allocs, &stack_frees,
                            &timer_allocs, &timer_frees);
    CHECK(task_allocs == task_frees);
    CHECK(stack_allocs == stack_frees);
    CHECK(timer_allocs == timer_frees);
}

static void assert_channel_counters_balanced(void) {
    size_t channel_allocs = 0;
    size_t channel_frees = 0;
    size_t buffer_allocs = 0;
    size_t buffer_frees = 0;
    mt_test_channel_memory_counters(&channel_allocs, &channel_frees,
                                    &buffer_allocs, &buffer_frees);
    CHECK(channel_allocs == channel_frees);
    CHECK(buffer_allocs == buffer_frees);
}

static void assert_handle_counters_balanced(void) {
    size_t handle_allocs = 0;
    size_t handle_frees = 0;
    mt_test_handle_memory_counters(&handle_allocs, &handle_frees);
    CHECK(handle_allocs == handle_frees);
}

static void assert_select_counters_balanced(void) {
    size_t select_allocs = 0;
    size_t select_frees = 0;
    mt_test_select_memory_counters(&select_allocs, &select_frees);
    CHECK(select_allocs == select_frees);
}

static void note_current_os_thread(void) {
    pthread_t self = pthread_self();
    pthread_mutex_lock(&g_seen_lock);
    for (size_t i = 0; i < g_seen_count; ++i) {
        if (pthread_equal(g_seen_threads[i], self)) {
            pthread_mutex_unlock(&g_seen_lock);
            return;
        }
    }
    CHECK(g_seen_count < ARRAY_LEN(g_seen_threads));
    g_seen_threads[g_seen_count++] = self;
    pthread_mutex_unlock(&g_seen_lock);
}

static size_t unique_os_threads_seen(void) {
    pthread_mutex_lock(&g_seen_lock);
    size_t n = g_seen_count;
    pthread_mutex_unlock(&g_seen_lock);
    return n;
}

static uint64_t monotonic_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * UINT64_C(1000) + (uint64_t)ts.tv_nsec / UINT64_C(1000000);
}

static void burn_for_ms(uint64_t ms) {
    uint64_t until = monotonic_ms() + ms;
    while (monotonic_ms() < until) {
        sched_yield();
    }
}

static void task_record_worker_count(void *arg) {
    (void)arg;
    atomic_store(&g_worker_count_seen, mt_runtime_workers());
}

static void task_start_runtime_inside_green_thread(void *arg) {
    (void)arg;
    atomic_store(&g_rc, mt_runtime_start(2));
}

static void task_parallel_participation(void *arg) {
    (void)arg;
    note_current_os_thread();
    burn_for_ms(5);
    atomic_fetch_add(&g_counter, 1);
}

static void task_anchor_until_stopped(void *arg) {
    (void)arg;
    atomic_store(&g_started, 1);
    while (!atomic_load(&g_stop_anchor)) {
        mt_sleep_ms(1);
    }
}

static void task_increment(void *arg) {
    (void)arg;
    atomic_fetch_add(&g_counter, 1);
}

static void *runtime_thread_main(void *arg) {
    size_t workers = *(size_t *)arg;
    int rc = mt_runtime_start(workers);
    atomic_store(&g_rc, rc);
    return NULL;
}

static void *external_submitter_main(void *arg) {
    int base = *(int *)arg;
    for (int i = 0; i < EXTERNAL_TASKS_PER_THREAD; ++i) {
        int id = mt_go(task_increment, NULL);
        CHECK(id > base);
    }
    return NULL;
}

static void task_child_increment(void *arg) {
    (void)arg;
    atomic_fetch_add(&g_counter2, 1);
}

static void task_spawner(void *arg) {
    (void)arg;
    for (int i = 0; i < CHILDREN_PER_SPAWNER; ++i) {
        CHECK(mt_go(task_child_increment, NULL) > 0);
    }
    atomic_fetch_add(&g_counter, 1);
}

static long producer_value(int producer, int i) {
    return (long)producer * 100000L + (long)i;
}

static void task_channel_producer(void *arg) {
    int producer = *(int *)arg;
    for (int i = 0; i < ITEMS_PER_PRODUCER; ++i) {
        long value = producer_value(producer, i);
        CHECK(mt_chan_send(g_ch, &value) == MT_OK);
    }
    atomic_fetch_add(&g_done, 1);
}

static void task_channel_consumer(void *arg) {
    (void)arg;
    for (;;) {
        long value = 0;
        int rc = mt_chan_recv(g_ch, &value);
        if (rc == MT_ERR_CLOSED) {
            break;
        }
        CHECK(rc == MT_OK);
        atomic_fetch_add(&g_counter, 1);
        atomic_fetch_add(&g_sum, value);
    }
    atomic_fetch_add(&g_counter2, 1);
}

static void task_channel_closer(void *arg) {
    (void)arg;
    while (atomic_load(&g_done) != PRODUCERS) {
        mt_yield();
    }
    CHECK(mt_chan_close(g_ch) == MT_OK);
}

static void task_unbuffered_sender(void *arg) {
    int value = *(int *)arg;
    CHECK(mt_chan_send(g_ch, &value) == MT_OK);
}

static void task_unbuffered_receiver(void *arg) {
    (void)arg;
    int value = 0;
    CHECK(mt_chan_recv(g_ch, &value) == MT_OK);
    atomic_fetch_add(&g_counter, 1);
    atomic_fetch_add(&g_sum, value);
}

static void task_select_recv_waiter(void *arg) {
    (void)arg;
    int out = -1;
    size_t index = 99;
    mt_select_case_t cases[] = {
        { .op = MT_SELECT_RECV, .ch = g_ch, .value = &out, .timeout_ms = 0 },
        { .op = MT_SELECT_TIMEOUT, .ch = NULL, .value = NULL, .timeout_ms = 1000 }
    };
    int rc = mt_select(cases, ARRAY_LEN(cases), &index);
    atomic_store(&g_rc, rc);
    atomic_store(&g_index_seen, (int)index);
    atomic_store(&g_value_seen, out);
}

static void task_delayed_sender(void *arg) {
    int value = *(int *)arg;
    mt_sleep_ms(1);
    CHECK(mt_chan_send(g_ch, &value) == MT_OK);
}

static void task_select_timeout_only(void *arg) {
    (void)arg;
    size_t index = 99;
    mt_select_case_t cases[] = {
        { .op = MT_SELECT_RECV, .ch = g_ch, .value = &g_value_seen, .timeout_ms = 0 },
        { .op = MT_SELECT_TIMEOUT, .ch = NULL, .value = NULL, .timeout_ms = 1 }
    };
    int rc = mt_select(cases, ARRAY_LEN(cases), &index);
    atomic_store(&g_rc, rc);
    atomic_store(&g_index_seen, (int)index);
}

static void task_select_wait_for_close(void *arg) {
    (void)arg;
    int out = -1;
    size_t index = 99;
    mt_select_case_t cases[] = {
        { .op = MT_SELECT_RECV, .ch = g_ch, .value = &out, .timeout_ms = 0 }
    };
    int rc = mt_select(cases, ARRAY_LEN(cases), &index);
    atomic_store(&g_rc, rc);
    atomic_store(&g_index_seen, (int)index);
}

static void task_close_after_yield(void *arg) {
    (void)arg;
    mt_yield();
    CHECK(mt_chan_close(g_ch) == MT_OK);
}

static void task_target_yields_then_done(void *arg) {
    (void)arg;
    for (int i = 0; i < 10; ++i) {
        mt_yield();
    }
    atomic_store(&g_done, 1);
}

static void task_joiner(void *arg) {
    mt_task_handle_t *handle = (mt_task_handle_t *)arg;
    int rc = mt_join(handle);
    CHECK(rc == MT_OK);
    atomic_fetch_add(&g_counter, 1);
}

static void task_sleep_until_cancelled(void *arg) {
    (void)arg;
    mt_sleep_ms(1000);
    if (mt_task_cancelled()) {
        atomic_fetch_add(&g_counter, 1);
    }
}

static void task_cancel_target_after_yield(void *arg) {
    mt_task_handle_t *handle = (mt_task_handle_t *)arg;
    mt_yield();
    CHECK(mt_task_cancel(handle) == MT_OK);
}

static void task_join_cancelled_target(void *arg) {
    mt_task_handle_t *handle = (mt_task_handle_t *)arg;
    int rc = mt_join(handle);
    atomic_store(&g_rc2, rc);
}

static void test_worker_lifecycle_and_counts(void) {
    reset_runtime();
    CHECK(mt_runtime_workers() == 0);
    CHECK(mt_runtime_start(0) == MT_ERR_INVALID);
    CHECK(mt_runtime_start(1) == MT_OK);
    CHECK(mt_runtime_workers() == 0);

    CHECK(mt_go(task_record_worker_count, NULL) > 0);
    CHECK(mt_runtime_start(WORKERS) == MT_OK);
    CHECK(atomic_load(&g_worker_count_seen) == WORKERS);
    CHECK(mt_runtime_workers() == 0);

    CHECK(mt_go(task_start_runtime_inside_green_thread, NULL) > 0);
    CHECK(mt_runtime_start(2) == MT_OK);
    CHECK(atomic_load(&g_rc) == MT_ERR_STATE);
    CHECK(mt_run_workers(1) == MT_OK);
    finish_runtime();
    assert_core_counters_balanced();
}

static void test_parallel_workers_participate(void) {
    reset_runtime();
    for (int i = 0; i < PAR_TASKS; ++i) {
        CHECK(mt_go(task_parallel_participation, NULL) > 0);
    }
    CHECK(mt_runtime_start(WORKERS) == MT_OK);
    CHECK(atomic_load(&g_counter) == PAR_TASKS);
    CHECK(unique_os_threads_seen() >= 2);
    finish_runtime();
    assert_core_counters_balanced();
}

static void test_external_mt_go_while_running(void) {
    reset_runtime();
    CHECK(mt_go(task_anchor_until_stopped, NULL) > 0);

    size_t workers = WORKERS;
    pthread_t runtime_thread;
    CHECK(pthread_create(&runtime_thread, NULL, runtime_thread_main, &workers) == 0);
    while (!atomic_load(&g_started) || mt_runtime_workers() == 0) {
        sched_yield();
    }

    pthread_t submitters[EXTERNAL_THREADS];
    int bases[EXTERNAL_THREADS];
    for (int i = 0; i < EXTERNAL_THREADS; ++i) {
        bases[i] = 0;
        CHECK(pthread_create(&submitters[i], NULL, external_submitter_main, &bases[i]) == 0);
    }
    for (int i = 0; i < EXTERNAL_THREADS; ++i) {
        CHECK(pthread_join(submitters[i], NULL) == 0);
    }
    while (atomic_load(&g_counter) != EXTERNAL_THREADS * EXTERNAL_TASKS_PER_THREAD) {
        sched_yield();
    }
    atomic_store(&g_stop_anchor, 1);
    CHECK(pthread_join(runtime_thread, NULL) == 0);
    CHECK(atomic_load(&g_rc) == MT_OK);
    CHECK(mt_runtime_workers() == 0);
    finish_runtime();
    assert_core_counters_balanced();
}

static void test_green_thread_mt_go_concurrency(void) {
    reset_runtime();
    for (int i = 0; i < SPAWNER_TASKS; ++i) {
        CHECK(mt_go(task_spawner, NULL) > 0);
    }
    CHECK(mt_runtime_start(WORKERS) == MT_OK);
    CHECK(atomic_load(&g_counter) == SPAWNER_TASKS);
    CHECK(atomic_load(&g_counter2) == SPAWNER_TASKS * CHILDREN_PER_SPAWNER);
    finish_runtime();
    assert_core_counters_balanced();
}

static void test_mpmc_buffered_channel_and_close(void) {
    reset_runtime();
    g_ch = mt_chan_create(sizeof(long), 8);
    CHECK(g_ch != NULL);
    int producer_ids[PRODUCERS];
    long expected_sum = 0;
    for (int p = 0; p < PRODUCERS; ++p) {
        producer_ids[p] = p;
        for (int i = 0; i < ITEMS_PER_PRODUCER; ++i) {
            expected_sum += producer_value(p, i);
        }
    }
    for (int c = 0; c < CONSUMERS; ++c) {
        CHECK(mt_go(task_channel_consumer, NULL) > 0);
    }
    for (int p = 0; p < PRODUCERS; ++p) {
        CHECK(mt_go(task_channel_producer, &producer_ids[p]) > 0);
    }
    CHECK(mt_go(task_channel_closer, NULL) > 0);
    CHECK(mt_runtime_start(WORKERS) == MT_OK);
    CHECK(atomic_load(&g_counter) == PRODUCERS * ITEMS_PER_PRODUCER);
    CHECK(atomic_load(&g_counter2) == CONSUMERS);
    CHECK(atomic_load(&g_sum) == expected_sum);
    finish_runtime();
    assert_core_counters_balanced();
    assert_channel_counters_balanced();
}

static void test_unbuffered_channel_rendezvous_exactly_once(void) {
    reset_runtime();
    g_ch = mt_chan_create(sizeof(int), 0);
    CHECK(g_ch != NULL);
    int values[64];
    int expected_sum = 0;
    for (int i = 0; i < 64; ++i) {
        values[i] = i + 1;
        expected_sum += values[i];
        CHECK(mt_go(task_unbuffered_receiver, NULL) > 0);
        CHECK(mt_go(task_unbuffered_sender, &values[i]) > 0);
    }
    CHECK(mt_runtime_start(WORKERS) == MT_OK);
    CHECK(atomic_load(&g_counter) == 64);
    CHECK(atomic_load(&g_sum) == expected_sum);
    finish_runtime();
    assert_core_counters_balanced();
    assert_channel_counters_balanced();
}

static void test_select_cross_worker_send_timeout_and_close(void) {
    reset_runtime();
    g_ch = mt_chan_create(sizeof(int), 0);
    CHECK(g_ch != NULL);
    int value = 77;
    CHECK(mt_go(task_select_recv_waiter, NULL) > 0);
    CHECK(mt_go(task_delayed_sender, &value) > 0);
    CHECK(mt_runtime_start(WORKERS) == MT_OK);
    CHECK(atomic_load(&g_rc) == MT_OK);
    CHECK(atomic_load(&g_index_seen) == 0);
    CHECK(atomic_load(&g_value_seen) == value);
    finish_runtime();

    reset_runtime();
    g_ch = mt_chan_create(sizeof(int), 0);
    CHECK(g_ch != NULL);
    CHECK(mt_go(task_select_timeout_only, NULL) > 0);
    CHECK(mt_runtime_start(WORKERS) == MT_OK);
    CHECK(atomic_load(&g_rc) == MT_OK);
    CHECK(atomic_load(&g_index_seen) == 1);
    finish_runtime();

    reset_runtime();
    g_ch = mt_chan_create(sizeof(int), 0);
    CHECK(g_ch != NULL);
    CHECK(mt_go(task_select_wait_for_close, NULL) > 0);
    CHECK(mt_go(task_close_after_yield, NULL) > 0);
    CHECK(mt_runtime_start(WORKERS) == MT_OK);
    CHECK(atomic_load(&g_rc) == MT_ERR_CLOSED);
    CHECK(atomic_load(&g_index_seen) == 0);
    finish_runtime();

    assert_core_counters_balanced();
    assert_channel_counters_balanced();
    assert_select_counters_balanced();
}

static void test_select_destroy_wakes_multiple_workers(void) {
    reset_runtime();
    g_ch = mt_chan_create(sizeof(int), 0);
    CHECK(g_ch != NULL);
    CHECK(mt_go(task_select_wait_for_close, NULL) > 0);
    CHECK(mt_test_run_until_blocked() == MT_ERR_STATE);
    CHECK(mt_chan_destroy(g_ch) == MT_OK);
    g_ch = NULL;
    CHECK(mt_runtime_start(WORKERS) == MT_OK);
    CHECK(atomic_load(&g_rc) == MT_ERR_CLOSED);
    CHECK(atomic_load(&g_index_seen) == 0);
    finish_runtime();
    assert_core_counters_balanced();
    assert_channel_counters_balanced();
    assert_select_counters_balanced();
}

static void test_join_and_cancel_across_workers(void) {
    reset_runtime();
    g_handle = mt_go_handle(task_target_yields_then_done, NULL);
    CHECK(g_handle != NULL);
    for (int i = 0; i < JOINERS; ++i) {
        CHECK(mt_go(task_joiner, g_handle) > 0);
    }
    CHECK(mt_runtime_start(WORKERS) == MT_OK);
    CHECK(atomic_load(&g_done) == 1);
    CHECK(atomic_load(&g_counter) == JOINERS);
    finish_runtime();

    reset_runtime();
    g_handle = mt_go_handle(task_sleep_until_cancelled, NULL);
    CHECK(g_handle != NULL);
    CHECK(mt_go(task_cancel_target_after_yield, g_handle) > 0);
    CHECK(mt_go(task_join_cancelled_target, g_handle) > 0);
    CHECK(mt_runtime_start(WORKERS) == MT_OK);
    CHECK(atomic_load(&g_counter) == 1);
    CHECK(atomic_load(&g_rc2) == MT_ERR_CANCELLED);
    finish_runtime();

    assert_core_counters_balanced();
    assert_handle_counters_balanced();
}

static void test_fault_hooks_and_reuse(void) {
    reset_runtime();
    mt_test_fail_next_task_alloc();
    CHECK(mt_go(task_increment, NULL) == MT_ERR_NOMEM);
    CHECK(mt_go(task_increment, NULL) > 0);
    CHECK(mt_runtime_start(WORKERS) == MT_OK);
    CHECK(atomic_load(&g_counter) == 1);

    mt_test_fail_next_channel_alloc();
    CHECK(mt_chan_create(sizeof(int), 1) == NULL);
    g_ch = mt_chan_create(sizeof(int), 1);
    CHECK(g_ch != NULL);
    finish_runtime();
    assert_core_counters_balanced();
    assert_channel_counters_balanced();
}

int main(void) {
    test_worker_lifecycle_and_counts();
    test_parallel_workers_participate();
    test_external_mt_go_while_running();
    test_green_thread_mt_go_concurrency();
    test_mpmc_buffered_channel_and_close();
    test_unbuffered_channel_rendezvous_exactly_once();
    test_select_cross_worker_send_timeout_and_close();
    test_select_destroy_wakes_multiple_workers();
    test_join_and_cancel_across_workers();
    test_fault_hooks_and_reuse();
    printf("v0.6 tests passed\n");
    return 0;
}
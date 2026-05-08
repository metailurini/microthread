#include "gt.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define ASSERT_OK(expr) assert((expr) == GT_OK)
#define ASSERT_EQ(a, b) assert((a) == (b))
#define ASSERT_TRUE(expr) assert((expr))

#ifdef GT_FULL_STRESS
enum {
    STRESS_BUFFERED_VALUES = 8000,
    STRESS_UNBUFFERED_VALUES = 3000,
    STRESS_CHANNELS = 750,
    STRESS_CLOSE_WAITERS = 1000,
    STRESS_MIXED_TASKS = 300
};
#else
enum {
    STRESS_BUFFERED_VALUES = 1000,
    STRESS_UNBUFFERED_VALUES = 500,
    STRESS_CHANNELS = 120,
    STRESS_CLOSE_WAITERS = 160,
    STRESS_MIXED_TASKS = 80
};
#endif

static int g_events[20000];
static int g_event_count;
static int g_counter;
static gt_chan_t *g_ch;

static void reset_runtime(void) {
    gt_shutdown();
    gt_test_reset_faults();
    g_event_count = 0;
    g_counter = 0;
    g_ch = NULL;
}

static void record_event(int value) {
    assert(g_event_count < (int)(sizeof(g_events) / sizeof(g_events[0])));
    g_events[g_event_count++] = value;
}

static void simple_yielder(void *arg) {
    int base = *(int *)arg;
    record_event(base + 1);
    gt_yield();
    record_event(base + 2);
}

static void regression_sleep_task(void *arg) {
    (void)arg;
    record_event(1);
    gt_sleep_ms(1);
    record_event(3);
}

static void regression_ready_task(void *arg) {
    (void)arg;
    record_event(2);
}

static void run_regression_tests(void) {
    reset_runtime();
    int a = 10;
    int b = 20;
    ASSERT_OK(gt_init());
    ASSERT_TRUE(gt_go(simple_yielder, &a) > 0);
    ASSERT_TRUE(gt_go(simple_yielder, &b) > 0);
    ASSERT_OK(gt_run());
    ASSERT_EQ(g_event_count, 4);
    ASSERT_EQ(g_events[0], 11);
    ASSERT_EQ(g_events[1], 21);
    ASSERT_EQ(g_events[2], 12);
    ASSERT_EQ(g_events[3], 22);
    gt_shutdown();

    reset_runtime();
    ASSERT_OK(gt_init());
    ASSERT_TRUE(gt_go(regression_sleep_task, NULL) > 0);
    ASSERT_TRUE(gt_go(regression_ready_task, NULL) > 0);
    ASSERT_OK(gt_run());
    ASSERT_EQ(g_event_count, 3);
    ASSERT_EQ(g_events[0], 1);
    ASSERT_EQ(g_events[1], 2);
    ASSERT_EQ(g_events[2], 3);
    gt_shutdown();
}

static void run_channel_creation_tests(void) {
    reset_runtime();
    gt_chan_t *ch = gt_chan_create(sizeof(int), 0);
    ASSERT_TRUE(ch != NULL);
    ASSERT_EQ(gt_chan_capacity(ch), 0u);
    ASSERT_EQ(gt_chan_len(ch), 0u);
    ASSERT_EQ(gt_chan_is_closed(ch), 0);
    ASSERT_OK(gt_chan_destroy(ch));
    gt_shutdown();

    reset_runtime();
    ch = gt_chan_create(sizeof(int), 3);
    ASSERT_TRUE(ch != NULL);
    ASSERT_EQ(gt_chan_capacity(ch), 3u);
    ASSERT_EQ(gt_chan_len(ch), 0u);
    ASSERT_OK(gt_chan_destroy(ch));
    gt_shutdown();

    reset_runtime();
    ASSERT_TRUE(gt_chan_create(0, 1) == NULL);
    ASSERT_TRUE(gt_chan_create((size_t)-1, 2) == NULL);
    gt_shutdown();

    reset_runtime();
    ch = gt_chan_create(sizeof(int), 1);
    ASSERT_TRUE(ch != NULL);
    ASSERT_EQ(gt_debug_live_task_count(), 0u);
    ASSERT_OK(gt_chan_destroy(ch));
    gt_shutdown();

    reset_runtime();
    ASSERT_OK(gt_init());
    gt_test_fail_next_channel_alloc();
    ASSERT_TRUE(gt_chan_create(sizeof(int), 1) == NULL);
    gt_shutdown();

    reset_runtime();
    ASSERT_OK(gt_init());
    gt_test_fail_next_channel_buffer_alloc();
    ASSERT_TRUE(gt_chan_create(sizeof(int), 1) == NULL);
    gt_shutdown();
}

static void sender_buffer_full(void *arg) {
    gt_chan_t *ch = (gt_chan_t *)arg;
    int value = 22;
    record_event(1);
    ASSERT_OK(gt_chan_send(ch, &value));
    record_event(3);
}

static void receiver_after_yield(void *arg) {
    gt_chan_t *ch = (gt_chan_t *)arg;
    int value = 0;
    record_event(2);
    gt_yield();
    ASSERT_OK(gt_chan_recv(ch, &value));
    record_event(value);
    ASSERT_OK(gt_chan_recv(ch, &value));
    record_event(value);
}

static void run_buffered_channel_tests(void) {
    reset_runtime();
    ASSERT_OK(gt_init());
    gt_chan_t *ch = gt_chan_create(sizeof(int), 3);
    ASSERT_TRUE(ch != NULL);
    int a = 1, b = 2, c = 3;
    ASSERT_OK(gt_chan_send(ch, &a));
    ASSERT_EQ(gt_chan_len(ch), 1u);
    ASSERT_OK(gt_chan_send(ch, &b));
    ASSERT_EQ(gt_chan_len(ch), 2u);
    ASSERT_OK(gt_chan_send(ch, &c));
    ASSERT_EQ(gt_chan_len(ch), 3u);
    int out = 0;
    ASSERT_OK(gt_chan_recv(ch, &out)); ASSERT_EQ(out, 1);
    ASSERT_OK(gt_chan_recv(ch, &out)); ASSERT_EQ(out, 2);
    ASSERT_OK(gt_chan_recv(ch, &out)); ASSERT_EQ(out, 3);
    ASSERT_EQ(gt_chan_len(ch), 0u);
    ASSERT_OK(gt_chan_destroy(ch));
    gt_shutdown();

    reset_runtime();
    ASSERT_OK(gt_init());
    ch = gt_chan_create(sizeof(int), 1);
    ASSERT_TRUE(ch != NULL);
    int first = 11;
    ASSERT_OK(gt_chan_send(ch, &first));
    ASSERT_TRUE(gt_go(sender_buffer_full, ch) > 0);
    ASSERT_TRUE(gt_go(receiver_after_yield, ch) > 0);
    ASSERT_OK(gt_run());
    ASSERT_EQ(g_event_count, 5);
    ASSERT_EQ(g_events[0], 1);
    ASSERT_EQ(g_events[1], 2);
    ASSERT_EQ(g_events[2], 11);
    ASSERT_EQ(g_events[3], 22);
    ASSERT_EQ(g_events[4], 3);
    ASSERT_OK(gt_chan_destroy(ch));
    gt_shutdown();

    reset_runtime();
    ASSERT_OK(gt_init());
    ch = gt_chan_create(sizeof(int), 5);
    ASSERT_TRUE(ch != NULL);
    for (int i = 0; i < 40; ++i) {
        ASSERT_OK(gt_chan_send(ch, &i));
        ASSERT_OK(gt_chan_recv(ch, &out));
        ASSERT_EQ(out, i);
    }
    for (int i = 0; i < 5; ++i) {
        int value = 100 + i;
        ASSERT_OK(gt_chan_send(ch, &value));
    }
    for (int i = 0; i < 5; ++i) {
        ASSERT_OK(gt_chan_recv(ch, &out));
        ASSERT_EQ(out, 100 + i);
    }
    ASSERT_OK(gt_chan_destroy(ch));
    gt_shutdown();

    reset_runtime();
    ASSERT_OK(gt_init());
    ch = gt_chan_create(sizeof(int), 3);
    ASSERT_TRUE(ch != NULL);
    a = 7; b = 8;
    ASSERT_OK(gt_chan_send(ch, &a));
    ASSERT_OK(gt_chan_send(ch, &b));
    ASSERT_OK(gt_chan_close(ch));
    ASSERT_OK(gt_chan_recv(ch, &out)); ASSERT_EQ(out, 7);
    ASSERT_OK(gt_chan_recv(ch, &out)); ASSERT_EQ(out, 8);
    ASSERT_EQ(gt_chan_recv(ch, &out), GT_ERR_CLOSED);
    ASSERT_OK(gt_chan_destroy(ch));
    gt_shutdown();
}

static void unbuf_sender(void *arg) {
    int value = *(int *)arg;
    record_event(1);
    ASSERT_OK(gt_chan_send(g_ch, &value));
    record_event(3);
}

static void unbuf_receiver(void *arg) {
    int *out = (int *)arg;
    record_event(2);
    ASSERT_OK(gt_chan_recv(g_ch, out));
    record_event(4);
}

static void unbuf_receiver_first(void *arg) {
    int *out = (int *)arg;
    record_event(1);
    ASSERT_OK(gt_chan_recv(g_ch, out));
    record_event(3);
}

static void unbuf_sender_second(void *arg) {
    int value = *(int *)arg;
    record_event(2);
    ASSERT_OK(gt_chan_send(g_ch, &value));
    record_event(4);
}

static void fifo_sender(void *arg) {
    int value = *(int *)arg;
    ASSERT_OK(gt_chan_send(g_ch, &value));
}

static void fifo_receiver(void *arg) {
    int *slot = (int *)arg;
    ASSERT_OK(gt_chan_recv(g_ch, slot));
}

static void stack_local_sender(void *arg) {
    (void)arg;
    int local = 12345;
    ASSERT_OK(gt_chan_send(g_ch, &local));
    local = 0;
}

static void delayed_receiver(void *arg) {
    int *out = (int *)arg;
    gt_yield();
    ASSERT_OK(gt_chan_recv(g_ch, out));
}

static void run_unbuffered_channel_tests(void) {
    reset_runtime();
    ASSERT_OK(gt_init());
    g_ch = gt_chan_create(sizeof(int), 0);
    ASSERT_TRUE(g_ch != NULL);
    int value = 42;
    int out = 0;
    ASSERT_TRUE(gt_go(unbuf_sender, &value) > 0);
    ASSERT_TRUE(gt_go(unbuf_receiver, &out) > 0);
    ASSERT_OK(gt_run());
    ASSERT_EQ(out, 42);
    ASSERT_EQ(g_event_count, 4);
    ASSERT_EQ(g_events[0], 1);
    ASSERT_EQ(g_events[1], 2);
    ASSERT_EQ(g_events[2], 4);
    ASSERT_EQ(g_events[3], 3);
    ASSERT_OK(gt_chan_destroy(g_ch));
    gt_shutdown();

    reset_runtime();
    ASSERT_OK(gt_init());
    g_ch = gt_chan_create(sizeof(int), 0);
    ASSERT_TRUE(g_ch != NULL);
    value = 77; out = 0;
    ASSERT_TRUE(gt_go(unbuf_receiver_first, &out) > 0);
    ASSERT_TRUE(gt_go(unbuf_sender_second, &value) > 0);
    ASSERT_OK(gt_run());
    ASSERT_EQ(out, 77);
    ASSERT_EQ(g_event_count, 4);
    ASSERT_EQ(g_events[0], 1);
    ASSERT_EQ(g_events[1], 2);
    ASSERT_EQ(g_events[2], 4);
    ASSERT_EQ(g_events[3], 3);
    ASSERT_OK(gt_chan_destroy(g_ch));
    gt_shutdown();

    reset_runtime();
    ASSERT_OK(gt_init());
    g_ch = gt_chan_create(sizeof(int), 0);
    ASSERT_TRUE(g_ch != NULL);
    int values[] = { 10, 20, 30 };
    int outs[] = { 0, 0, 0 };
    for (int i = 0; i < 3; ++i) ASSERT_TRUE(gt_go(fifo_sender, &values[i]) > 0);
    for (int i = 0; i < 3; ++i) ASSERT_TRUE(gt_go(fifo_receiver, &outs[i]) > 0);
    ASSERT_OK(gt_run());
    ASSERT_EQ(outs[0], 10);
    ASSERT_EQ(outs[1], 20);
    ASSERT_EQ(outs[2], 30);
    ASSERT_OK(gt_chan_destroy(g_ch));
    gt_shutdown();

    reset_runtime();
    ASSERT_OK(gt_init());
    g_ch = gt_chan_create(sizeof(int), 0);
    ASSERT_TRUE(g_ch != NULL);
    memset(outs, 0, sizeof(outs));
    for (int i = 0; i < 3; ++i) ASSERT_TRUE(gt_go(fifo_receiver, &outs[i]) > 0);
    for (int i = 0; i < 3; ++i) ASSERT_TRUE(gt_go(fifo_sender, &values[i]) > 0);
    ASSERT_OK(gt_run());
    ASSERT_EQ(outs[0], 10);
    ASSERT_EQ(outs[1], 20);
    ASSERT_EQ(outs[2], 30);
    ASSERT_OK(gt_chan_destroy(g_ch));
    gt_shutdown();

    reset_runtime();
    ASSERT_OK(gt_init());
    g_ch = gt_chan_create(sizeof(int), 0);
    ASSERT_TRUE(g_ch != NULL);
    out = 0;
    ASSERT_TRUE(gt_go(stack_local_sender, NULL) > 0);
    ASSERT_TRUE(gt_go(delayed_receiver, &out) > 0);
    ASSERT_OK(gt_run());
    ASSERT_EQ(out, 12345);
    ASSERT_OK(gt_chan_destroy(g_ch));
    gt_shutdown();
}

static void blocked_sender_close(void *arg) {
    int value = *(int *)arg;
    int rc = gt_chan_send(g_ch, &value);
    record_event(rc);
}

static void blocked_receiver_close(void *arg) {
    (void)arg;
    int out = 0;
    int rc = gt_chan_recv(g_ch, &out);
    (void)out;
    record_event(rc);
}

static void task_closer(void *arg) {
    gt_chan_t *ch = (gt_chan_t *)arg;
    gt_yield();
    ASSERT_OK(gt_chan_close(ch));
}

static void run_close_behavior_tests(void) {
    reset_runtime();
    ASSERT_OK(gt_init());
    gt_chan_t *ch = gt_chan_create(sizeof(int), 1);
    ASSERT_TRUE(ch != NULL);
    ASSERT_EQ(gt_chan_is_closed(ch), 0);
    ASSERT_OK(gt_chan_close(ch));
    ASSERT_EQ(gt_chan_is_closed(ch), 1);
    ASSERT_EQ(gt_chan_close(ch), GT_ERR_CLOSED);
    int value = 1;
    ASSERT_EQ(gt_chan_send(ch, &value), GT_ERR_CLOSED);
    ASSERT_EQ(gt_chan_recv(ch, &value), GT_ERR_CLOSED);
    ASSERT_OK(gt_chan_destroy(ch));
    gt_shutdown();

    reset_runtime();
    ASSERT_OK(gt_init());
    g_ch = gt_chan_create(sizeof(int), 0);
    ASSERT_TRUE(g_ch != NULL);
    value = 5;
    ASSERT_TRUE(gt_go(blocked_sender_close, &value) > 0);
    ASSERT_TRUE(gt_go(task_closer, g_ch) > 0);
    ASSERT_OK(gt_run());
    ASSERT_EQ(g_event_count, 1);
    ASSERT_EQ(g_events[0], GT_ERR_CLOSED);
    ASSERT_OK(gt_chan_destroy(g_ch));
    gt_shutdown();

    reset_runtime();
    ASSERT_OK(gt_init());
    g_ch = gt_chan_create(sizeof(int), 0);
    ASSERT_TRUE(g_ch != NULL);
    ASSERT_TRUE(gt_go(blocked_receiver_close, NULL) > 0);
    ASSERT_TRUE(gt_go(task_closer, g_ch) > 0);
    ASSERT_OK(gt_run());
    ASSERT_EQ(g_event_count, 1);
    ASSERT_EQ(g_events[0], GT_ERR_CLOSED);
    ASSERT_OK(gt_chan_destroy(g_ch));
    gt_shutdown();

    reset_runtime();
    ASSERT_OK(gt_init());
    ch = gt_chan_create(sizeof(int), 2);
    ASSERT_TRUE(ch != NULL);
    int a = 10, b = 11, out = 0;
    ASSERT_OK(gt_chan_send(ch, &a));
    ASSERT_OK(gt_chan_send(ch, &b));
    ASSERT_OK(gt_chan_close(ch));
    ASSERT_OK(gt_chan_recv(ch, &out)); ASSERT_EQ(out, 10);
    ASSERT_OK(gt_chan_recv(ch, &out)); ASSERT_EQ(out, 11);
    ASSERT_EQ(gt_chan_recv(ch, &out), GT_ERR_CLOSED);
    ASSERT_OK(gt_chan_destroy(ch));
    gt_shutdown();
}

static void wait_forever_recv(void *arg) {
    gt_chan_t *ch = (gt_chan_t *)arg;
    int out = 0;
    int rc = gt_chan_recv(ch, &out);
    record_event(rc);
}

static void close_after_sleep(void *arg) {
    gt_chan_t *ch = (gt_chan_t *)arg;
    gt_sleep_ms(1);
    ASSERT_OK(gt_chan_close(ch));
}

static void wait_then_receive(void *arg) {
    gt_chan_t *ch = (gt_chan_t *)arg;
    int out = 0;
    ASSERT_OK(gt_chan_recv(ch, &out));
    record_event(out);
}

static void sleep_then_send(void *arg) {
    gt_chan_t *ch = (gt_chan_t *)arg;
    int value = 99;
    gt_sleep_ms(1);
    ASSERT_OK(gt_chan_send(ch, &value));
}

static void yield_sender(void *arg) {
    gt_chan_t *ch = (gt_chan_t *)arg;
    int value = 88;
    gt_yield();
    ASSERT_OK(gt_chan_send(ch, &value));
}

static void run_destroy_scheduler_lifecycle_tests(void) {
    reset_runtime();
    ASSERT_OK(gt_init());
    gt_chan_t *ch = gt_chan_create(sizeof(int), 1);
    ASSERT_TRUE(ch != NULL);
    ASSERT_OK(gt_chan_destroy(ch));
    gt_shutdown();

    reset_runtime();
    ASSERT_OK(gt_init());
    ch = gt_chan_create(sizeof(int), 1);
    ASSERT_TRUE(ch != NULL);
    ASSERT_OK(gt_chan_close(ch));
    ASSERT_OK(gt_chan_destroy(ch));
    gt_shutdown();

    reset_runtime();
    ASSERT_OK(gt_init());
    ch = gt_chan_create(sizeof(int), 0);
    ASSERT_TRUE(ch != NULL);
    ASSERT_TRUE(gt_go(wait_forever_recv, ch) > 0);
    ASSERT_EQ(gt_run(), GT_ERR_STATE);
    ASSERT_EQ(gt_debug_channel_waiting_task_count(), 1u);
    ASSERT_EQ(gt_chan_destroy(ch), GT_ERR_STATE);
    ASSERT_OK(gt_chan_close(ch));
    ASSERT_OK(gt_run());
    ASSERT_EQ(g_event_count, 1);
    ASSERT_EQ(g_events[0], GT_ERR_CLOSED);
    ASSERT_OK(gt_chan_destroy(ch));
    gt_shutdown();

    reset_runtime();
    ASSERT_OK(gt_init());
    ch = gt_chan_create(sizeof(int), 0);
    ASSERT_TRUE(ch != NULL);
    ASSERT_TRUE(gt_go(wait_forever_recv, ch) > 0);
    ASSERT_TRUE(gt_go(close_after_sleep, ch) > 0);
    ASSERT_OK(gt_run());
    ASSERT_EQ(g_event_count, 1);
    ASSERT_EQ(g_events[0], GT_ERR_CLOSED);
    ASSERT_OK(gt_chan_destroy(ch));
    gt_shutdown();

    reset_runtime();
    ASSERT_OK(gt_init());
    ch = gt_chan_create(sizeof(int), 0);
    ASSERT_TRUE(ch != NULL);
    ASSERT_TRUE(gt_go(wait_then_receive, ch) > 0);
    ASSERT_TRUE(gt_go(sleep_then_send, ch) > 0);
    ASSERT_OK(gt_run());
    ASSERT_EQ(g_event_count, 1);
    ASSERT_EQ(g_events[0], 99);
    ASSERT_OK(gt_chan_destroy(ch));
    gt_shutdown();

    reset_runtime();
    ASSERT_OK(gt_init());
    ch = gt_chan_create(sizeof(int), 0);
    ASSERT_TRUE(ch != NULL);
    ASSERT_TRUE(gt_go(wait_then_receive, ch) > 0);
    ASSERT_TRUE(gt_go(yield_sender, ch) > 0);
    ASSERT_OK(gt_run());
    ASSERT_EQ(g_event_count, 1);
    ASSERT_EQ(g_events[0], 88);
    ASSERT_OK(gt_chan_destroy(ch));
    gt_shutdown();

    reset_runtime();
    ASSERT_OK(gt_init());
    ch = gt_chan_create(sizeof(int), 0);
    ASSERT_TRUE(ch != NULL);
    ASSERT_TRUE(gt_go(wait_forever_recv, ch) > 0);
    ASSERT_EQ(gt_run(), GT_ERR_STATE);
    gt_shutdown();
    ASSERT_EQ(gt_debug_live_task_count(), 0u);
    ASSERT_OK(gt_chan_destroy(ch));
    gt_shutdown();

    for (int i = 0; i < 50; ++i) {
        reset_runtime();
        ASSERT_OK(gt_init());
        ch = gt_chan_create(sizeof(int), 1);
        ASSERT_TRUE(ch != NULL);
        int value = i;
        ASSERT_OK(gt_chan_send(ch, &value));
        int out = 0;
        ASSERT_OK(gt_chan_recv(ch, &out));
        ASSERT_EQ(out, i);
        ASSERT_OK(gt_chan_destroy(ch));
        gt_shutdown();
    }
}

static void run_error_misuse_tests(void) {
    reset_runtime();
    ASSERT_OK(gt_init());
    gt_chan_t *ch = gt_chan_create(sizeof(int), 1);
    ASSERT_TRUE(ch != NULL);
    int value = 123;
    int out = 0;
    ASSERT_EQ(gt_chan_send(NULL, &value), GT_ERR_INVALID);
    ASSERT_EQ(gt_chan_send(ch, NULL), GT_ERR_INVALID);
    ASSERT_EQ(gt_chan_recv(NULL, &out), GT_ERR_INVALID);
    ASSERT_EQ(gt_chan_recv(ch, NULL), GT_ERR_INVALID);
    ASSERT_EQ(gt_chan_destroy(NULL), GT_ERR_INVALID);
    ASSERT_OK(gt_chan_destroy(ch));
    gt_shutdown();

    reset_runtime();
    ASSERT_OK(gt_init());
    ch = gt_chan_create(sizeof(int), 0);
    ASSERT_TRUE(ch != NULL);
    ASSERT_EQ(gt_chan_send(ch, &value), GT_ERR_STATE);
    ASSERT_EQ(gt_chan_recv(ch, &out), GT_ERR_STATE);
    ASSERT_OK(gt_chan_destroy(ch));
    gt_shutdown();

    reset_runtime();
    ASSERT_OK(gt_init());
    ch = gt_chan_create(sizeof(int), 1);
    ASSERT_TRUE(ch != NULL);
    ASSERT_OK(gt_chan_send(ch, &value));
    ASSERT_OK(gt_chan_recv(ch, &out));
    ASSERT_EQ(out, value);
    ASSERT_OK(gt_chan_destroy(ch));
    gt_shutdown();
}

typedef struct pair_value {
    int a;
    int b;
    char tag[8];
} pair_value_t;

typedef struct large_value {
    unsigned char bytes[512];
} large_value_t;

static void run_data_copy_tests(void) {
    reset_runtime();
    ASSERT_OK(gt_init());
    gt_chan_t *ch = gt_chan_create(sizeof(pair_value_t), 2);
    ASSERT_TRUE(ch != NULL);
    pair_value_t in = { 7, 9, "pair" };
    ASSERT_OK(gt_chan_send(ch, &in));
    memset(&in, 0, sizeof(in));
    pair_value_t out;
    memset(&out, 0, sizeof(out));
    ASSERT_OK(gt_chan_recv(ch, &out));
    ASSERT_EQ(out.a, 7);
    ASSERT_EQ(out.b, 9);
    ASSERT_TRUE(strcmp(out.tag, "pair") == 0);
    ASSERT_OK(gt_chan_destroy(ch));
    gt_shutdown();

    reset_runtime();
    ASSERT_OK(gt_init());
    ch = gt_chan_create(sizeof(int *), 1);
    ASSERT_TRUE(ch != NULL);
    int target = 444;
    int *ptr = &target;
    int *ptr_out = NULL;
    ASSERT_OK(gt_chan_send(ch, &ptr));
    ASSERT_OK(gt_chan_recv(ch, &ptr_out));
    ASSERT_TRUE(ptr_out == &target);
    ASSERT_EQ(*ptr_out, 444);
    ASSERT_OK(gt_chan_destroy(ch));
    gt_shutdown();

    reset_runtime();
    ASSERT_OK(gt_init());
    ch = gt_chan_create(sizeof(large_value_t), 1);
    ASSERT_TRUE(ch != NULL);
    large_value_t big;
    for (size_t i = 0; i < sizeof(big.bytes); ++i) {
        big.bytes[i] = (unsigned char)(i & 0xffu);
    }
    ASSERT_OK(gt_chan_send(ch, &big));
    memset(&big, 0, sizeof(big));
    large_value_t big_out;
    memset(&big_out, 0, sizeof(big_out));
    ASSERT_OK(gt_chan_recv(ch, &big_out));
    for (size_t i = 0; i < sizeof(big_out.bytes); ++i) {
        ASSERT_EQ(big_out.bytes[i], (unsigned char)(i & 0xffu));
    }
    ASSERT_OK(gt_chan_destroy(ch));
    gt_shutdown();
}

typedef struct stress_arg {
    gt_chan_t *ch;
    int start;
    int count;
    int *sum;
} stress_arg_t;

static void buffered_producer(void *arg) {
    stress_arg_t *a = (stress_arg_t *)arg;
    for (int i = 0; i < a->count; ++i) {
        int value = a->start + i;
        ASSERT_OK(gt_chan_send(a->ch, &value));
        if ((i % 7) == 0) gt_yield();
    }
}

static void buffered_consumer(void *arg) {
    stress_arg_t *a = (stress_arg_t *)arg;
    for (int i = 0; i < a->count; ++i) {
        int value = 0;
        ASSERT_OK(gt_chan_recv(a->ch, &value));
        *a->sum += value;
        if ((i % 5) == 0) gt_yield();
    }
}

static void close_waiter_task(void *arg) {
    gt_chan_t *ch = (gt_chan_t *)arg;
    int out = 0;
    int rc = gt_chan_recv(ch, &out);
    if (rc == GT_ERR_CLOSED) {
        g_counter++;
    }
}

static void mixed_channel_task(void *arg) {
    gt_chan_t *ch = (gt_chan_t *)arg;
    int value = gt_debug_current_task_id();
    if ((value % 3) == 0) gt_sleep_ms(1);
    if ((value % 2) == 0) gt_yield();
    ASSERT_OK(gt_chan_send(ch, &value));
}

static void mixed_receiver_task(void *arg) {
    gt_chan_t *ch = (gt_chan_t *)arg;
    int out = 0;
    ASSERT_OK(gt_chan_recv(ch, &out));
    g_counter += out > 0 ? 1 : 0;
}

static void run_stress_tests(void) {
    reset_runtime();
    ASSERT_OK(gt_init());
    gt_chan_t *ch = gt_chan_create(sizeof(int), 31);
    ASSERT_TRUE(ch != NULL);
    int sum = 0;
    stress_arg_t prod = { ch, 1, STRESS_BUFFERED_VALUES, NULL };
    stress_arg_t cons = { ch, 0, STRESS_BUFFERED_VALUES, &sum };
    ASSERT_TRUE(gt_go(buffered_producer, &prod) > 0);
    ASSERT_TRUE(gt_go(buffered_consumer, &cons) > 0);
    ASSERT_OK(gt_run());
    ASSERT_EQ(sum, (STRESS_BUFFERED_VALUES * (STRESS_BUFFERED_VALUES + 1)) / 2);
    ASSERT_OK(gt_chan_destroy(ch));
    gt_shutdown();

    reset_runtime();
    ASSERT_OK(gt_init());
    g_ch = gt_chan_create(sizeof(int), 0);
    ASSERT_TRUE(g_ch != NULL);
    int *values = (int *)calloc((size_t)STRESS_UNBUFFERED_VALUES, sizeof(int));
    int *outs = (int *)calloc((size_t)STRESS_UNBUFFERED_VALUES, sizeof(int));
    ASSERT_TRUE(values != NULL && outs != NULL);
    for (int i = 0; i < STRESS_UNBUFFERED_VALUES; ++i) {
        values[i] = i + 1;
        ASSERT_TRUE(gt_go(fifo_sender, &values[i]) > 0);
    }
    for (int i = 0; i < STRESS_UNBUFFERED_VALUES; ++i) {
        ASSERT_TRUE(gt_go(fifo_receiver, &outs[i]) > 0);
    }
    ASSERT_OK(gt_run());
    for (int i = 0; i < STRESS_UNBUFFERED_VALUES; ++i) {
        ASSERT_EQ(outs[i], i + 1);
    }
    free(values);
    free(outs);
    ASSERT_OK(gt_chan_destroy(g_ch));
    gt_shutdown();

    reset_runtime();
    ASSERT_OK(gt_init());
    for (int i = 0; i < STRESS_CHANNELS; ++i) {
        ch = gt_chan_create(sizeof(int), (size_t)(i % 5));
        ASSERT_TRUE(ch != NULL);
        if (gt_chan_capacity(ch) > 0) {
            int value = i;
            ASSERT_OK(gt_chan_send(ch, &value));
            int out = 0;
            ASSERT_OK(gt_chan_recv(ch, &out));
            ASSERT_EQ(out, i);
        }
        ASSERT_OK(gt_chan_destroy(ch));
    }
    gt_shutdown();

    reset_runtime();
    ASSERT_OK(gt_init());
    ch = gt_chan_create(sizeof(int), 0);
    ASSERT_TRUE(ch != NULL);
    for (int i = 0; i < STRESS_CLOSE_WAITERS; ++i) {
        ASSERT_TRUE(gt_go(close_waiter_task, ch) > 0);
    }
    ASSERT_EQ(gt_run(), GT_ERR_STATE);
    ASSERT_EQ(gt_debug_channel_waiting_task_count(), (size_t)STRESS_CLOSE_WAITERS);
    ASSERT_OK(gt_chan_close(ch));
    ASSERT_OK(gt_run());
    ASSERT_EQ(g_counter, STRESS_CLOSE_WAITERS);
    ASSERT_OK(gt_chan_destroy(ch));
    gt_shutdown();

    reset_runtime();
    ASSERT_OK(gt_init());
    ch = gt_chan_create(sizeof(int), 11);
    ASSERT_TRUE(ch != NULL);
    for (int i = 0; i < STRESS_MIXED_TASKS; ++i) {
        ASSERT_TRUE(gt_go(mixed_channel_task, ch) > 0);
        ASSERT_TRUE(gt_go(mixed_receiver_task, ch) > 0);
    }
    ASSERT_OK(gt_run());
    ASSERT_EQ(g_counter, STRESS_MIXED_TASKS);
    ASSERT_OK(gt_chan_destroy(ch));
    gt_shutdown();
}

static void run_memory_counter_tests(void) {
    reset_runtime();
    ASSERT_OK(gt_init());
    size_t ca0, cf0, ba0, bf0;
    size_t ca1, cf1, ba1, bf1;
    gt_test_channel_memory_counters(&ca0, &cf0, &ba0, &bf0);
    gt_chan_t *ch = gt_chan_create(sizeof(int), 4);
    ASSERT_TRUE(ch != NULL);
    ASSERT_OK(gt_chan_destroy(ch));
    gt_test_channel_memory_counters(&ca1, &cf1, &ba1, &bf1);
    ASSERT_EQ(ca1 - ca0, 1u);
    ASSERT_EQ(cf1 - cf0, 1u);
    ASSERT_EQ(ba1 - ba0, 1u);
    ASSERT_EQ(bf1 - bf0, 1u);
    gt_shutdown();

    reset_runtime();
    ASSERT_OK(gt_init());
    size_t ta0, tf0, sa0, sf0, ti0, tif0;
    size_t ta1, tf1, sa1, sf1, ti1, tif1;
    gt_test_memory_counters(&ta0, &tf0, &sa0, &sf0, &ti0, &tif0);
    ch = gt_chan_create(sizeof(int), 0);
    ASSERT_TRUE(ch != NULL);
    ASSERT_TRUE(gt_go(wait_forever_recv, ch) > 0);
    ASSERT_EQ(gt_run(), GT_ERR_STATE);
    gt_shutdown();
    gt_test_memory_counters(&ta1, &tf1, &sa1, &sf1, &ti1, &tif1);
    ASSERT_EQ(ta1 - ta0, tf1 - tf0);
    ASSERT_EQ(sa1 - sa0, sf1 - sf0);
    ASSERT_OK(gt_chan_destroy(ch));
    gt_shutdown();
}

int main(void) {
    run_regression_tests();
    run_channel_creation_tests();
    run_buffered_channel_tests();
    run_unbuffered_channel_tests();
    run_close_behavior_tests();
    run_destroy_scheduler_lifecycle_tests();
    run_error_misuse_tests();
    run_data_copy_tests();
    run_stress_tests();
    run_memory_counter_tests();
    reset_runtime();
    printf("v0.3 channel tests passed\n");
    return 0;
}

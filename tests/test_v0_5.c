#ifndef MT_TESTING
#define MT_TESTING
#endif
#include "microthread.h"
#include "microthread_testing.h"
#include "microthread_debug.h"
#include "test_helpers.h"

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>


#define ARRAY_LEN(a) (sizeof(a) / sizeof((a)[0]))

static mt_chan_t *g_ch1;
static mt_chan_t *g_ch2;
static mt_chan_t *g_ch3;
static mt_task_handle_t *g_handle1;
static int g_rc1;
static int g_rc2;
static int g_rc3;
static int g_out1;
static int g_out2;
static int g_out3;
static int g_value1;
static int g_value2;
static int g_value3;
static size_t g_index1;
static size_t g_index2;
static size_t g_index3;
static int g_counter;
static int g_marker1;
static int g_marker2;

#define STRESS_MANY_CHANNELS 10

typedef struct select_many_arg {
    mt_chan_t **channels;
    int rc;
    size_t index;
    int out;
} select_many_arg_t;

static void reset_runtime(void) {
    mt_shutdown();
    mt_test_reset_faults();
    CHECK(mt_init() == MT_OK);
    g_ch1 = NULL;
    g_ch2 = NULL;
    g_ch3 = NULL;
    g_handle1 = NULL;
    g_rc1 = 12345;
    g_rc2 = 12345;
    g_rc3 = 12345;
    g_out1 = -1111;
    g_out2 = -2222;
    g_out3 = -3333;
    g_value1 = 11;
    g_value2 = 22;
    g_value3 = 33;
    g_index1 = 9999;
    g_index2 = 9999;
    g_index3 = 9999;
    g_counter = 0;
    g_marker1 = 0;
    g_marker2 = 0;
}

static void finish_runtime(void) {
    if (g_handle1) {
        mt_task_handle_release(g_handle1);
        g_handle1 = NULL;
    }
    mt_shutdown();
    mt_test_reset_faults();
}

static void assert_select_counters_balanced(void) {
    size_t allocs = 0;
    size_t frees = 0;
    mt_test_select_memory_counters(&allocs, &frees);
    CHECK(allocs == frees);
}

static void assert_timer_counters_balanced(void) {
    size_t task_allocs = 0;
    size_t task_frees = 0;
    size_t stack_allocs = 0;
    size_t stack_frees = 0;
    size_t timer_allocs = 0;
    size_t timer_frees = 0;
    mt_test_memory_counters(&task_allocs, &task_frees,
                            &stack_allocs, &stack_frees,
                            &timer_allocs, &timer_frees);
    (void)task_allocs;
    (void)task_frees;
    (void)stack_allocs;
    (void)stack_frees;
    CHECK(timer_allocs == timer_frees);
}

static void task_recv_ch1(void *arg) {
    (void)arg;
    g_rc1 = mt_chan_recv(g_ch1, &g_out1);
    g_counter++;
}

static void task_recv_ch2(void *arg) {
    (void)arg;
    g_rc2 = mt_chan_recv(g_ch2, &g_out2);
    g_counter++;
}

static void task_send_ch1(void *arg) {
    (void)arg;
    g_rc1 = mt_chan_send(g_ch1, &g_value1);
    g_counter++;
}

static void task_select_recv_ch1(void *arg) {
    (void)arg;
    mt_select_case_t cases[] = {
        { .op = MT_SELECT_RECV, .ch = g_ch1, .value = &g_out1, .timeout_ms = 0 }
    };
    g_rc1 = mt_select(cases, ARRAY_LEN(cases), &g_index1);
    g_counter++;
}

static void task_select_send_ch1(void *arg) {
    (void)arg;
    mt_select_case_t cases[] = {
        { .op = MT_SELECT_SEND, .ch = g_ch1, .value = &g_value1, .timeout_ms = 0 }
    };
    g_rc1 = mt_select(cases, ARRAY_LEN(cases), &g_index1);
    g_counter++;
}

static void task_select_recv_ch1_second(void *arg) {
    (void)arg;
    mt_select_case_t cases[] = {
        { .op = MT_SELECT_RECV, .ch = g_ch1, .value = &g_out2, .timeout_ms = 0 }
    };
    g_rc2 = mt_select(cases, ARRAY_LEN(cases), &g_index2);
    g_counter++;
}

static void task_select_recv_two_channels(void *arg) {
    (void)arg;
    mt_select_case_t cases[] = {
        { .op = MT_SELECT_RECV, .ch = g_ch1, .value = &g_out1, .timeout_ms = 0 },
        { .op = MT_SELECT_RECV, .ch = g_ch2, .value = &g_out2, .timeout_ms = 0 }
    };
    g_rc1 = mt_select(cases, ARRAY_LEN(cases), &g_index1);
    g_counter++;
}

static void task_select_recv_two_channels_timeout(void *arg) {
    uint64_t timeout_ms = arg ? *(uint64_t *)arg : 1u;
    mt_select_case_t cases[] = {
        { .op = MT_SELECT_RECV, .ch = g_ch1, .value = &g_out1, .timeout_ms = 0 },
        { .op = MT_SELECT_RECV, .ch = g_ch2, .value = &g_out2, .timeout_ms = 0 },
        { .op = MT_SELECT_TIMEOUT, .ch = NULL, .value = NULL, .timeout_ms = timeout_ms }
    };
    g_rc1 = mt_select(cases, ARRAY_LEN(cases), &g_index1);
    g_counter++;
}

static void task_select_recv_ch1_timeout(void *arg) {
    uint64_t timeout_ms = arg ? *(uint64_t *)arg : 1u;
    mt_select_case_t cases[] = {
        { .op = MT_SELECT_RECV, .ch = g_ch1, .value = &g_out1, .timeout_ms = 0 },
        { .op = MT_SELECT_TIMEOUT, .ch = NULL, .value = NULL, .timeout_ms = timeout_ms }
    };
    g_rc1 = mt_select(cases, ARRAY_LEN(cases), &g_index1);
    g_counter++;
}

static void task_yield_then_try_send_ch1(void *arg) {
    (void)arg;
    mt_yield();
    g_rc2 = mt_chan_try_send(g_ch1, &g_value2);
    g_counter++;
}

static void task_yield_then_recv_ch1(void *arg) {
    (void)arg;
    mt_yield();
    g_rc2 = mt_chan_recv(g_ch1, &g_out2);
    g_counter++;
}

static void task_ready_counter(void *arg) {
    int *counter = (int *)arg;
    (*counter)++;
}

static void task_join_target(void *arg) {
    (void)arg;
    mt_yield();
    g_marker1 = 1;
}

static void task_join_waiter(void *arg) {
    mt_task_handle_t *handle = (mt_task_handle_t *)arg;
    g_rc3 = mt_join(handle);
    g_marker2 = 1;
}

static void task_sleep_marker(void *arg) {
    uint64_t ms = arg ? *(uint64_t *)arg : 1u;
    mt_sleep_ms(ms);
    g_marker1++;
}

static void task_select_many_channels(void *arg) {
    select_many_arg_t *ctx = (select_many_arg_t *)arg;
    int outs[STRESS_MANY_CHANNELS];
    mt_select_case_t cases[STRESS_MANY_CHANNELS];

    ctx->rc = 12345;
    ctx->index = 9999;
    ctx->out = -1;
    for (int i = 0; i < STRESS_MANY_CHANNELS; ++i) {
        outs[i] = -1;
        cases[i].op = MT_SELECT_RECV;
        cases[i].ch = ctx->channels[i];
        cases[i].value = &outs[i];
        cases[i].timeout_ms = 0;
    }

    ctx->rc = mt_select(cases, ARRAY_LEN(cases), &ctx->index);
    if (ctx->rc == MT_OK && ctx->index < STRESS_MANY_CHANNELS) {
        ctx->out = outs[ctx->index];
    }
    g_counter++;
}

static void test_try_send_matrix(void) {
    /* TC-TRY-SEND-001 */
    reset_runtime();
    g_ch1 = mt_chan_create(sizeof(int), 1);
    CHECK(g_ch1 != NULL);
    CHECK(mt_chan_try_send(g_ch1, &g_value1) == MT_OK);
    CHECK(mt_chan_len(g_ch1) == 1);
    CHECK(mt_chan_recv(g_ch1, &g_out1) == MT_OK);
    CHECK(g_out1 == g_value1);
    CHECK(mt_chan_destroy(g_ch1) == MT_OK);
    g_ch1 = NULL;
    finish_runtime();

    /* TC-TRY-SEND-002 */
    reset_runtime();
    g_ch1 = mt_chan_create(sizeof(int), 1);
    CHECK(g_ch1 != NULL);
    CHECK(mt_chan_try_send(g_ch1, &g_value1) == MT_OK);
    CHECK(mt_chan_try_send(g_ch1, &g_value2) == MT_ERR_WOULD_BLOCK);
    CHECK(mt_debug_channel_waiting_task_count() == 0);
    CHECK(mt_chan_len(g_ch1) == 1);
    CHECK(mt_chan_recv(g_ch1, &g_out1) == MT_OK);
    CHECK(g_out1 == g_value1);
    CHECK(mt_chan_destroy(g_ch1) == MT_OK);
    g_ch1 = NULL;
    finish_runtime();

    /* TC-TRY-SEND-003 */
    reset_runtime();
    g_ch1 = mt_chan_create(sizeof(int), 0);
    CHECK(g_ch1 != NULL);
    CHECK(mt_go(task_recv_ch1, NULL) > 0);
    CHECK(mt_run() == MT_ERR_STATE);
    CHECK(mt_debug_channel_waiting_task_count() == 1);
    CHECK(mt_chan_try_send(g_ch1, &g_value1) == MT_OK);
    CHECK(mt_run() == MT_OK);
    CHECK(g_rc1 == MT_OK);
    CHECK(g_out1 == g_value1);
    CHECK(g_counter == 1);
    CHECK(mt_chan_destroy(g_ch1) == MT_OK);
    g_ch1 = NULL;
    finish_runtime();

    /* TC-TRY-SEND-004, TC-TRY-SEND-007 */
    reset_runtime();
    g_ch1 = mt_chan_create(sizeof(int), 0);
    CHECK(g_ch1 != NULL);
    CHECK(mt_chan_try_send(g_ch1, &g_value1) == MT_ERR_WOULD_BLOCK);
    CHECK(mt_debug_channel_waiting_task_count() == 0);
    CHECK(mt_chan_destroy(g_ch1) == MT_OK);
    g_ch1 = NULL;
    finish_runtime();

    /* TC-TRY-SEND-005, TC-TRY-SEND-006 */
    reset_runtime();
    g_ch1 = mt_chan_create(sizeof(int), 1);
    CHECK(g_ch1 != NULL);
    CHECK(mt_chan_close(g_ch1) == MT_OK);
    CHECK(mt_chan_try_send(g_ch1, &g_value1) == MT_ERR_CLOSED);
    CHECK(mt_chan_try_send(NULL, &g_value1) == MT_ERR_INVALID);
    CHECK(mt_chan_try_send(g_ch1, NULL) == MT_ERR_INVALID);
    CHECK(mt_chan_destroy(g_ch1) == MT_OK);
    g_ch1 = NULL;
    finish_runtime();
}

static void test_try_recv_matrix(void) {
    /* TC-TRY-RECV-001 */
    reset_runtime();
    g_ch1 = mt_chan_create(sizeof(int), 1);
    CHECK(g_ch1 != NULL);
    CHECK(mt_chan_send(g_ch1, &g_value1) == MT_OK);
    CHECK(mt_chan_try_recv(g_ch1, &g_out1) == MT_OK);
    CHECK(g_out1 == g_value1);
    CHECK(mt_chan_destroy(g_ch1) == MT_OK);
    g_ch1 = NULL;
    finish_runtime();

    /* TC-TRY-RECV-002, TC-TRY-RECV-004 */
    reset_runtime();
    g_ch1 = mt_chan_create(sizeof(int), 0);
    CHECK(g_ch1 != NULL);
    CHECK(mt_chan_try_recv(g_ch1, &g_out1) == MT_ERR_WOULD_BLOCK);
    CHECK(mt_debug_channel_waiting_task_count() == 0);
    CHECK(mt_chan_destroy(g_ch1) == MT_OK);
    g_ch1 = NULL;
    finish_runtime();

    /* TC-TRY-RECV-003 */
    reset_runtime();
    g_ch1 = mt_chan_create(sizeof(int), 0);
    CHECK(g_ch1 != NULL);
    CHECK(mt_go(task_send_ch1, NULL) > 0);
    CHECK(mt_run() == MT_ERR_STATE);
    CHECK(mt_debug_channel_waiting_task_count() == 1);
    CHECK(mt_chan_try_recv(g_ch1, &g_out1) == MT_OK);
    CHECK(g_out1 == g_value1);
    CHECK(mt_run() == MT_OK);
    CHECK(g_rc1 == MT_OK);
    CHECK(g_counter == 1);
    CHECK(mt_chan_destroy(g_ch1) == MT_OK);
    g_ch1 = NULL;
    finish_runtime();

    /* TC-TRY-RECV-005, TC-TRY-RECV-006, TC-TRY-RECV-007 */
    reset_runtime();
    g_ch1 = mt_chan_create(sizeof(int), 2);
    CHECK(g_ch1 != NULL);
    CHECK(mt_chan_send(g_ch1, &g_value1) == MT_OK);
    CHECK(mt_chan_close(g_ch1) == MT_OK);
    CHECK(mt_chan_try_recv(g_ch1, &g_out1) == MT_OK);
    CHECK(g_out1 == g_value1);
    CHECK(mt_chan_try_recv(g_ch1, &g_out2) == MT_ERR_CLOSED);
    CHECK(mt_chan_try_recv(NULL, &g_out1) == MT_ERR_INVALID);
    CHECK(mt_chan_try_recv(g_ch1, NULL) == MT_ERR_INVALID);
    CHECK(mt_chan_destroy(g_ch1) == MT_OK);
    g_ch1 = NULL;
    finish_runtime();
}

static void test_select_immediate_matrix(void) {
    /* TC-SEL-IMM-001, TC-SEL-IMM-006 */
    reset_runtime();
    g_ch1 = mt_chan_create(sizeof(int), 1);
    CHECK(g_ch1 != NULL);
    CHECK(mt_chan_send(g_ch1, &g_value1) == MT_OK);
    mt_select_case_t recv_ready[] = {
        { .op = MT_SELECT_RECV, .ch = g_ch1, .value = &g_out1, .timeout_ms = 0 }
    };
    CHECK(mt_select(recv_ready, ARRAY_LEN(recv_ready), &g_index1) == MT_OK);
    CHECK(g_index1 == 0);
    CHECK(g_out1 == g_value1);
    CHECK(mt_chan_destroy(g_ch1) == MT_OK);
    g_ch1 = NULL;
    finish_runtime();

    /* TC-SEL-IMM-002 */
    reset_runtime();
    g_ch1 = mt_chan_create(sizeof(int), 1);
    CHECK(g_ch1 != NULL);
    mt_select_case_t send_ready[] = {
        { .op = MT_SELECT_SEND, .ch = g_ch1, .value = &g_value1, .timeout_ms = 0 }
    };
    CHECK(mt_select(send_ready, ARRAY_LEN(send_ready), &g_index1) == MT_OK);
    CHECK(g_index1 == 0);
    CHECK(mt_chan_recv(g_ch1, &g_out1) == MT_OK);
    CHECK(g_out1 == g_value1);
    CHECK(mt_chan_destroy(g_ch1) == MT_OK);
    g_ch1 = NULL;
    finish_runtime();

    /* TC-SEL-IMM-003 */
    reset_runtime();
    g_ch1 = mt_chan_create(sizeof(int), 0);
    CHECK(g_ch1 != NULL);
    CHECK(mt_go(task_send_ch1, NULL) > 0);
    CHECK(mt_run() == MT_ERR_STATE);
    mt_select_case_t recv_sender[] = {
        { .op = MT_SELECT_RECV, .ch = g_ch1, .value = &g_out1, .timeout_ms = 0 }
    };
    CHECK(mt_select(recv_sender, ARRAY_LEN(recv_sender), &g_index1) == MT_OK);
    CHECK(g_index1 == 0);
    CHECK(g_out1 == g_value1);
    CHECK(mt_run() == MT_OK);
    CHECK(g_rc1 == MT_OK);
    CHECK(mt_chan_destroy(g_ch1) == MT_OK);
    g_ch1 = NULL;
    finish_runtime();

    /* TC-SEL-IMM-004 */
    reset_runtime();
    g_ch1 = mt_chan_create(sizeof(int), 0);
    CHECK(g_ch1 != NULL);
    CHECK(mt_go(task_recv_ch1, NULL) > 0);
    CHECK(mt_run() == MT_ERR_STATE);
    mt_select_case_t send_receiver[] = {
        { .op = MT_SELECT_SEND, .ch = g_ch1, .value = &g_value1, .timeout_ms = 0 }
    };
    CHECK(mt_select(send_receiver, ARRAY_LEN(send_receiver), &g_index1) == MT_OK);
    CHECK(g_index1 == 0);
    CHECK(mt_run() == MT_OK);
    CHECK(g_rc1 == MT_OK);
    CHECK(g_out1 == g_value1);
    CHECK(mt_chan_destroy(g_ch1) == MT_OK);
    g_ch1 = NULL;
    finish_runtime();

    /* TC-SEL-IMM-005, TC-SEL-IMM-007 */
    reset_runtime();
    g_ch1 = mt_chan_create(sizeof(int), 1);
    g_ch2 = mt_chan_create(sizeof(int), 1);
    CHECK(g_ch1 != NULL);
    CHECK(g_ch2 != NULL);
    CHECK(mt_chan_send(g_ch1, &g_value1) == MT_OK);
    CHECK(mt_chan_send(g_ch2, &g_value2) == MT_OK);
    g_out1 = 101;
    g_out2 = 202;
    mt_select_case_t two_ready[] = {
        { .op = MT_SELECT_RECV, .ch = g_ch1, .value = &g_out1, .timeout_ms = 0 },
        { .op = MT_SELECT_RECV, .ch = g_ch2, .value = &g_out2, .timeout_ms = 0 }
    };
    CHECK(mt_select(two_ready, ARRAY_LEN(two_ready), &g_index1) == MT_OK);
    CHECK(g_index1 == 0 || g_index1 == 1);
    if (g_index1 == 0) {
        CHECK(g_out1 == g_value1);
        CHECK(g_out2 == 202);
        CHECK(mt_chan_len(g_ch1) == 0);
        CHECK(mt_chan_len(g_ch2) == 1);
    } else {
        CHECK(g_out1 == 101);
        CHECK(g_out2 == g_value2);
        CHECK(mt_chan_len(g_ch1) == 1);
        CHECK(mt_chan_len(g_ch2) == 0);
    }
    CHECK(mt_chan_destroy(g_ch1) == MT_OK);
    CHECK(mt_chan_destroy(g_ch2) == MT_OK);
    g_ch1 = NULL;
    g_ch2 = NULL;
    finish_runtime();
}

static void test_select_blocking_matrix(void) {
    /* TC-SEL-BLOCK-001, TC-SEL-BLOCK-007 */
    reset_runtime();
    g_ch1 = mt_chan_create(sizeof(int), 0);
    CHECK(g_ch1 != NULL);
    CHECK(mt_go(task_select_recv_ch1, NULL) > 0);
    CHECK(mt_run() == MT_ERR_STATE);
    CHECK(g_counter == 0);
    CHECK(mt_debug_runnable_count() == 0);
    CHECK(mt_debug_channel_waiting_task_count() == 1);
    CHECK(mt_chan_close(g_ch1) == MT_OK);
    CHECK(mt_run() == MT_OK);
    CHECK(g_rc1 == MT_ERR_CLOSED);
    CHECK(g_index1 == 0);
    CHECK(g_counter == 1);
    CHECK(mt_chan_destroy(g_ch1) == MT_OK);
    g_ch1 = NULL;
    finish_runtime();

    /* TC-SEL-BLOCK-002 */
    reset_runtime();
    g_ch1 = mt_chan_create(sizeof(int), 0);
    CHECK(g_ch1 != NULL);
    CHECK(mt_go(task_select_recv_ch1, NULL) > 0);
    CHECK(mt_go(task_yield_then_try_send_ch1, NULL) > 0);
    CHECK(mt_run() == MT_OK);
    CHECK(g_rc1 == MT_OK);
    CHECK(g_rc2 == MT_OK);
    CHECK(g_out1 == g_value2);
    CHECK(g_index1 == 0);
    CHECK(g_counter == 2);
    CHECK(mt_chan_destroy(g_ch1) == MT_OK);
    g_ch1 = NULL;
    finish_runtime();

    /* TC-SEL-BLOCK-003 */
    reset_runtime();
    g_ch1 = mt_chan_create(sizeof(int), 0);
    CHECK(g_ch1 != NULL);
    CHECK(mt_go(task_select_send_ch1, NULL) > 0);
    CHECK(mt_go(task_yield_then_recv_ch1, NULL) > 0);
    CHECK(mt_run() == MT_OK);
    CHECK(g_rc1 == MT_OK);
    CHECK(g_rc2 == MT_OK);
    CHECK(g_out2 == g_value1);
    CHECK(g_index1 == 0);
    CHECK(g_counter == 2);
    CHECK(mt_chan_destroy(g_ch1) == MT_OK);
    g_ch1 = NULL;
    finish_runtime();

    /* TC-SEL-BLOCK-004 */
    reset_runtime();
    g_ch1 = mt_chan_create(sizeof(int), 0);
    CHECK(g_ch1 != NULL);
    CHECK(mt_go(task_select_recv_ch1, NULL) > 0);
    CHECK(mt_go(task_select_recv_ch1_second, NULL) > 0);
    CHECK(mt_run() == MT_ERR_STATE);
    CHECK(g_counter == 0);
    CHECK(mt_chan_try_send(g_ch1, &g_value1) == MT_OK);
    CHECK(mt_run() == MT_ERR_STATE);
    CHECK(g_counter == 1);
    CHECK(mt_chan_try_send(g_ch1, &g_value2) == MT_OK);
    CHECK(mt_run() == MT_OK);
    CHECK(g_counter == 2);
    CHECK(((g_out1 == g_value1) && (g_out2 == g_value2)) ||
          ((g_out1 == g_value2) && (g_out2 == g_value1)));
    CHECK(mt_chan_destroy(g_ch1) == MT_OK);
    g_ch1 = NULL;
    finish_runtime();

    /* TC-SEL-BLOCK-005 */
    reset_runtime();
    g_ch1 = mt_chan_create(sizeof(int), 0);
    g_ch2 = mt_chan_create(sizeof(int), 0);
    CHECK(g_ch1 != NULL);
    CHECK(g_ch2 != NULL);
    CHECK(mt_go(task_select_recv_two_channels, NULL) > 0);
    CHECK(mt_run() == MT_ERR_STATE);
    CHECK(mt_chan_try_send(g_ch2, &g_value2) == MT_OK);
    CHECK(mt_run() == MT_OK);
    CHECK(g_rc1 == MT_OK);
    CHECK(g_index1 == 1);
    CHECK(g_out2 == g_value2);
    CHECK(mt_debug_channel_waiting_task_count() == 0);
    CHECK(mt_chan_try_send(g_ch1, &g_value1) == MT_ERR_WOULD_BLOCK);
    CHECK(mt_chan_destroy(g_ch1) == MT_OK);
    CHECK(mt_chan_destroy(g_ch2) == MT_OK);
    g_ch1 = NULL;
    g_ch2 = NULL;
    finish_runtime();

    /* TC-SEL-BLOCK-006 */
    reset_runtime();
    g_ch1 = mt_chan_create(sizeof(int), 0);
    CHECK(g_ch1 != NULL);
    CHECK(mt_go(task_recv_ch1, NULL) > 0);
    CHECK(mt_go(task_select_recv_ch1_second, NULL) > 0);
    CHECK(mt_run() == MT_ERR_STATE);
    CHECK(mt_debug_channel_waiting_task_count() == 2);
    CHECK(mt_chan_try_send(g_ch1, &g_value1) == MT_OK);
    CHECK(mt_run() == MT_ERR_STATE);
    CHECK(g_counter == 1);
    CHECK(mt_chan_try_send(g_ch1, &g_value2) == MT_OK);
    CHECK(mt_run() == MT_OK);
    CHECK(g_counter == 2);
    CHECK(g_rc1 == MT_OK);
    CHECK(g_rc2 == MT_OK);
    CHECK(mt_chan_destroy(g_ch1) == MT_OK);
    g_ch1 = NULL;
    finish_runtime();
}

static void test_select_default_timeout_matrix(void) {
    /* TC-SEL-DEFAULT-001, TC-SEL-DEFAULT-004 */
    reset_runtime();
    g_ch1 = mt_chan_create(sizeof(int), 0);
    CHECK(g_ch1 != NULL);
    mt_select_case_t default_ready[] = {
        { .op = MT_SELECT_RECV, .ch = g_ch1, .value = &g_out1, .timeout_ms = 0 },
        { .op = MT_SELECT_DEFAULT, .ch = NULL, .value = NULL, .timeout_ms = 0 }
    };
    CHECK(mt_select(default_ready, ARRAY_LEN(default_ready), &g_index1) == MT_OK);
    CHECK(g_index1 == 1);
    CHECK(mt_debug_channel_waiting_task_count() == 0);
    CHECK(mt_chan_destroy(g_ch1) == MT_OK);
    g_ch1 = NULL;
    finish_runtime();

    /* TC-SEL-DEFAULT-002 */
    reset_runtime();
    g_ch1 = mt_chan_create(sizeof(int), 1);
    CHECK(g_ch1 != NULL);
    CHECK(mt_chan_send(g_ch1, &g_value1) == MT_OK);
    mt_select_case_t default_loses[] = {
        { .op = MT_SELECT_RECV, .ch = g_ch1, .value = &g_out1, .timeout_ms = 0 },
        { .op = MT_SELECT_DEFAULT, .ch = NULL, .value = NULL, .timeout_ms = 0 }
    };
    CHECK(mt_select(default_loses, ARRAY_LEN(default_loses), &g_index1) == MT_OK);
    CHECK(g_index1 == 0);
    CHECK(g_out1 == g_value1);
    CHECK(mt_chan_destroy(g_ch1) == MT_OK);
    g_ch1 = NULL;
    finish_runtime();

    /* TC-SEL-DEFAULT-003 */
    reset_runtime();
    mt_select_case_t duplicate_defaults[] = {
        { .op = MT_SELECT_DEFAULT, .ch = NULL, .value = NULL, .timeout_ms = 0 },
        { .op = MT_SELECT_DEFAULT, .ch = NULL, .value = NULL, .timeout_ms = 0 }
    };
    CHECK(mt_select(duplicate_defaults, ARRAY_LEN(duplicate_defaults), &g_index1) == MT_ERR_INVALID);
    finish_runtime();

    /* TC-SEL-TIMEOUT-001 */
    reset_runtime();
    uint64_t one_ms = 1;
    g_ch1 = mt_chan_create(sizeof(int), 0);
    CHECK(g_ch1 != NULL);
    CHECK(mt_go(task_select_recv_ch1_timeout, &one_ms) > 0);
    CHECK(mt_run() == MT_OK);
    CHECK(g_rc1 == MT_OK);
    CHECK(g_index1 == 1);
    CHECK(g_counter == 1);
    CHECK(mt_debug_channel_waiting_task_count() == 0);
    CHECK(mt_chan_destroy(g_ch1) == MT_OK);
    g_ch1 = NULL;
    finish_runtime();

    /* TC-SEL-TIMEOUT-002 */
    reset_runtime();
    uint64_t long_ms = 1000;
    one_ms = 1;
    g_ch1 = mt_chan_create(sizeof(int), 0);
    CHECK(g_ch1 != NULL);
    CHECK(mt_go(task_select_recv_ch1_timeout, &long_ms) > 0);
    CHECK(mt_go(task_yield_then_try_send_ch1, NULL) > 0);
    CHECK(mt_run() == MT_OK);
    CHECK(g_rc1 == MT_OK);
    CHECK(g_index1 == 0);
    CHECK(g_out1 == g_value2);
    CHECK(g_rc2 == MT_OK);
    CHECK(mt_debug_sleeping_task_count() == 0);
    CHECK(mt_chan_destroy(g_ch1) == MT_OK);
    g_ch1 = NULL;
    finish_runtime();

    /* TC-SEL-TIMEOUT-003 */
    reset_runtime();
    g_ch1 = mt_chan_create(sizeof(int), 0);
    CHECK(g_ch1 != NULL);
    mt_select_case_t zero_timeout[] = {
        { .op = MT_SELECT_RECV, .ch = g_ch1, .value = &g_out1, .timeout_ms = 0 },
        { .op = MT_SELECT_TIMEOUT, .ch = NULL, .value = NULL, .timeout_ms = 0 }
    };
    CHECK(mt_select(zero_timeout, ARRAY_LEN(zero_timeout), &g_index1) == MT_OK);
    CHECK(g_index1 == 1);
    CHECK(mt_debug_channel_waiting_task_count() == 0);
    CHECK(mt_chan_destroy(g_ch1) == MT_OK);
    g_ch1 = NULL;
    finish_runtime();

    /* TC-SEL-TIMEOUT-004 */
    reset_runtime();
    mt_select_case_t duplicate_timeouts[] = {
        { .op = MT_SELECT_TIMEOUT, .ch = NULL, .value = NULL, .timeout_ms = 1 },
        { .op = MT_SELECT_TIMEOUT, .ch = NULL, .value = NULL, .timeout_ms = 2 }
    };
    CHECK(mt_select(duplicate_timeouts, ARRAY_LEN(duplicate_timeouts), &g_index1) == MT_ERR_INVALID);
    finish_runtime();

    /* TC-SEL-TIMEOUT-005 */
    reset_runtime();
    one_ms = 1;
    g_ch1 = mt_chan_create(sizeof(int), 0);
    g_ch2 = mt_chan_create(sizeof(int), 0);
    CHECK(g_ch1 != NULL);
    CHECK(g_ch2 != NULL);
    CHECK(mt_go(task_select_recv_two_channels_timeout, &one_ms) > 0);
    CHECK(mt_run() == MT_OK);
    CHECK(g_rc1 == MT_OK);
    CHECK(g_index1 == 2);
    CHECK(g_counter == 1);
    CHECK(mt_debug_channel_waiting_task_count() == 0);
    CHECK(mt_chan_try_send(g_ch1, &g_value1) == MT_ERR_WOULD_BLOCK);
    CHECK(mt_chan_try_send(g_ch2, &g_value2) == MT_ERR_WOULD_BLOCK);
    assert_select_counters_balanced();
    CHECK(mt_chan_destroy(g_ch1) == MT_OK);
    CHECK(mt_chan_destroy(g_ch2) == MT_OK);
    g_ch1 = NULL;
    g_ch2 = NULL;
    finish_runtime();

    /* TC-SEL-TIMEOUT-006 */
    reset_runtime();
    long_ms = 1000000;
    g_ch1 = mt_chan_create(sizeof(int), 0);
    CHECK(g_ch1 != NULL);
    CHECK(mt_go(task_select_recv_ch1_timeout, &long_ms) > 0);
    CHECK(mt_test_run_until_blocked() == MT_ERR_STATE);
    CHECK(mt_debug_sleeping_task_count() == 1);
    CHECK(mt_debug_channel_waiting_task_count() == 1);
    mt_chan_t *pending_timeout_ch = g_ch1;
    finish_runtime();
    CHECK(mt_chan_destroy(pending_timeout_ch) == MT_OK);
    g_ch1 = NULL;
    reset_runtime();
    assert_select_counters_balanced();
    assert_timer_counters_balanced();
    finish_runtime();
}

static void test_select_close_destroy_matrix(void) {
    /* TC-SEL-CLOSE-001 */
    reset_runtime();
    g_ch1 = mt_chan_create(sizeof(int), 0);
    CHECK(g_ch1 != NULL);
    CHECK(mt_go(task_select_recv_ch1, NULL) > 0);
    CHECK(mt_run() == MT_ERR_STATE);
    CHECK(mt_chan_close(g_ch1) == MT_OK);
    CHECK(mt_run() == MT_OK);
    CHECK(g_rc1 == MT_ERR_CLOSED);
    CHECK(g_index1 == 0);
    CHECK(mt_chan_destroy(g_ch1) == MT_OK);
    g_ch1 = NULL;
    finish_runtime();

    /* TC-SEL-CLOSE-002 */
    reset_runtime();
    g_ch1 = mt_chan_create(sizeof(int), 0);
    CHECK(g_ch1 != NULL);
    CHECK(mt_go(task_select_send_ch1, NULL) > 0);
    CHECK(mt_run() == MT_ERR_STATE);
    CHECK(mt_chan_close(g_ch1) == MT_OK);
    CHECK(mt_run() == MT_OK);
    CHECK(g_rc1 == MT_ERR_CLOSED);
    CHECK(g_index1 == 0);
    CHECK(mt_chan_destroy(g_ch1) == MT_OK);
    g_ch1 = NULL;
    finish_runtime();

    /* TC-SEL-CLOSE-003, TC-SEL-CLOSE-004 */
    reset_runtime();
    g_ch1 = mt_chan_create(sizeof(int), 2);
    CHECK(g_ch1 != NULL);
    CHECK(mt_chan_send(g_ch1, &g_value1) == MT_OK);
    CHECK(mt_chan_close(g_ch1) == MT_OK);
    mt_select_case_t closed_buffered[] = {
        { .op = MT_SELECT_RECV, .ch = g_ch1, .value = &g_out1, .timeout_ms = 0 }
    };
    CHECK(mt_select(closed_buffered, ARRAY_LEN(closed_buffered), &g_index1) == MT_OK);
    CHECK(g_index1 == 0);
    CHECK(g_out1 == g_value1);
    CHECK(mt_select(closed_buffered, ARRAY_LEN(closed_buffered), &g_index1) == MT_ERR_CLOSED);
    CHECK(g_index1 == 0);
    CHECK(mt_chan_destroy(g_ch1) == MT_OK);
    g_ch1 = NULL;
    finish_runtime();

    /* TC-SEL-CLOSE-005 receive side */
    reset_runtime();
    g_ch1 = mt_chan_create(sizeof(int), 0);
    CHECK(g_ch1 != NULL);
    CHECK(mt_go(task_select_recv_ch1, NULL) > 0);
    CHECK(mt_run() == MT_ERR_STATE);
    CHECK(mt_chan_destroy(g_ch1) == MT_OK);
    g_ch1 = NULL;
    CHECK(mt_run() == MT_OK);
    CHECK(g_rc1 == MT_ERR_CLOSED);
    CHECK(g_index1 == 0);
    finish_runtime();

    /* TC-SEL-CLOSE-005 send side */
    reset_runtime();
    g_ch1 = mt_chan_create(sizeof(int), 0);
    CHECK(g_ch1 != NULL);
    CHECK(mt_go(task_select_send_ch1, NULL) > 0);
    CHECK(mt_run() == MT_ERR_STATE);
    CHECK(mt_chan_destroy(g_ch1) == MT_OK);
    g_ch1 = NULL;
    CHECK(mt_run() == MT_OK);
    CHECK(g_rc1 == MT_ERR_CLOSED);
    CHECK(g_index1 == 0);
    finish_runtime();

    /* TC-SEL-CLOSE-006 */
    reset_runtime();
    g_ch1 = mt_chan_create(sizeof(int), 0);
    g_ch2 = mt_chan_create(sizeof(int), 0);
    CHECK(g_ch1 != NULL);
    CHECK(g_ch2 != NULL);
    CHECK(mt_go(task_select_recv_two_channels, NULL) > 0);
    CHECK(mt_run() == MT_ERR_STATE);
    CHECK(mt_chan_destroy(g_ch1) == MT_OK);
    g_ch1 = NULL;
    CHECK(mt_run() == MT_OK);
    CHECK(g_rc1 == MT_ERR_CLOSED);
    CHECK(g_index1 == 0);
    CHECK(g_counter == 1);
    CHECK(mt_chan_destroy(g_ch2) == MT_OK);
    g_ch2 = NULL;
    finish_runtime();
}

static void test_select_scheduler_lifecycle_matrix(void) {
    /* TC-SEL-SCHED-001 */
    reset_runtime();
    g_ch1 = mt_chan_create(sizeof(int), 0);
    CHECK(g_ch1 != NULL);
    int ready_count = 0;
    CHECK(mt_go(task_select_recv_ch1, NULL) > 0);
    CHECK(mt_go(task_ready_counter, &ready_count) > 0);
    CHECK(mt_go(task_ready_counter, &ready_count) > 0);
    CHECK(mt_run() == MT_ERR_STATE);
    CHECK(ready_count == 2);
    CHECK(g_counter == 0);
    CHECK(mt_chan_close(g_ch1) == MT_OK);
    CHECK(mt_run() == MT_OK);
    CHECK(g_counter == 1);
    CHECK(mt_chan_destroy(g_ch1) == MT_OK);
    g_ch1 = NULL;
    finish_runtime();

    /* TC-SEL-SCHED-002 */
    reset_runtime();
    g_ch1 = mt_chan_create(sizeof(int), 0);
    CHECK(g_ch1 != NULL);
    CHECK(mt_go(task_select_recv_ch1, NULL) > 0);
    CHECK(mt_run() == MT_ERR_STATE);
    CHECK(mt_chan_try_send(g_ch1, &g_value1) == MT_OK);
    CHECK(mt_debug_runnable_count() == 1);
    CHECK(mt_chan_close(g_ch1) == MT_OK);
    CHECK(mt_debug_runnable_count() == 1);
    CHECK(mt_run() == MT_OK);
    CHECK(g_counter == 1);
    CHECK(g_rc1 == MT_OK);
    CHECK(g_out1 == g_value1);
    CHECK(mt_chan_destroy(g_ch1) == MT_OK);
    g_ch1 = NULL;
    finish_runtime();

    /* TC-SEL-SCHED-003 */
    reset_runtime();
    uint64_t one_ms = 1;
    g_ch1 = mt_chan_create(sizeof(int), 0);
    g_ch2 = mt_chan_create(sizeof(int), 0);
    CHECK(g_ch1 != NULL);
    CHECK(g_ch2 != NULL);
    g_handle1 = mt_go_handle(task_join_target, NULL);
    CHECK(g_handle1 != NULL);
    CHECK(mt_go(task_join_waiter, g_handle1) > 0);
    CHECK(mt_go(task_recv_ch2, NULL) > 0);
    CHECK(mt_go(task_select_recv_ch1_timeout, &one_ms) > 0);
    CHECK(mt_go(task_sleep_marker, &one_ms) > 0);
    CHECK(mt_run() == MT_ERR_STATE);
    CHECK(g_rc1 == MT_OK);
    CHECK(g_index1 == 1);
    CHECK(g_rc3 == MT_OK);
    CHECK(g_marker1 >= 2);
    CHECK(g_marker2 == 1);
    CHECK(mt_chan_close(g_ch2) == MT_OK);
    CHECK(mt_run() == MT_OK);
    CHECK(g_rc2 == MT_ERR_CLOSED);
    mt_task_handle_release(g_handle1);
    g_handle1 = NULL;
    CHECK(mt_chan_destroy(g_ch1) == MT_OK);
    CHECK(mt_chan_destroy(g_ch2) == MT_OK);
    g_ch1 = NULL;
    g_ch2 = NULL;
    finish_runtime();

    /* TC-SEL-SCHED-004 */
    reset_runtime();
    g_ch1 = mt_chan_create(sizeof(int), 0);
    CHECK(g_ch1 != NULL);
    CHECK(mt_go(task_select_recv_ch1, NULL) > 0);
    CHECK(mt_run() == MT_ERR_STATE);
    CHECK(mt_chan_close(g_ch1) == MT_OK);
    CHECK(mt_run() == MT_OK);
    CHECK(mt_chan_destroy(g_ch1) == MT_OK);
    g_ch1 = NULL;
    finish_runtime();

    /* TC-SEL-SCHED-005 */
    reset_runtime();
    one_ms = 1;
    g_ch1 = mt_chan_create(sizeof(int), 0);
    CHECK(g_ch1 != NULL);
    CHECK(mt_go(task_select_recv_ch1_timeout, &one_ms) > 0);
    CHECK(mt_run() == MT_OK);
    CHECK(g_rc1 == MT_OK);
    CHECK(g_index1 == 1);
    CHECK(mt_chan_destroy(g_ch1) == MT_OK);
    g_ch1 = NULL;
    finish_runtime();

    /* TC-SEL-LIFE-001 */
    reset_runtime();
    g_ch1 = mt_chan_create(sizeof(int), 0);
    CHECK(g_ch1 != NULL);
    CHECK(mt_go(task_select_recv_ch1, NULL) > 0);
    CHECK(mt_run() == MT_ERR_STATE);
    finish_runtime();
    reset_runtime();
    assert_select_counters_balanced();
    finish_runtime();

    /* TC-SEL-LIFE-002 */
    for (int i = 0; i < 10; ++i) {
        reset_runtime();
        g_ch1 = mt_chan_create(sizeof(int), 1);
        CHECK(g_ch1 != NULL);
        CHECK(mt_chan_send(g_ch1, &g_value1) == MT_OK);
        mt_select_case_t cases[] = {
            { .op = MT_SELECT_RECV, .ch = g_ch1, .value = &g_out1, .timeout_ms = 0 }
        };
        CHECK(mt_select(cases, ARRAY_LEN(cases), &g_index1) == MT_OK);
        CHECK(g_index1 == 0);
        CHECK(g_out1 == g_value1);
        CHECK(mt_chan_destroy(g_ch1) == MT_OK);
        g_ch1 = NULL;
        finish_runtime();
    }
}

static void test_select_error_fault_memory_matrix(void) {
    /* TC-SEL-ERR-001, TC-SEL-ERR-002, TC-SEL-ERR-003 */
    reset_runtime();
    mt_select_case_t invalid_op[] = {
        { .op = (mt_select_op_t)99, .ch = NULL, .value = NULL, .timeout_ms = 0 }
    };
    CHECK(mt_select(NULL, 1, &g_index1) == MT_ERR_INVALID);
    CHECK(mt_select(invalid_op, 0, &g_index1) == MT_ERR_INVALID);
    CHECK(mt_select(invalid_op, ARRAY_LEN(invalid_op), &g_index1) == MT_ERR_INVALID);
    CHECK(mt_select(invalid_op, ARRAY_LEN(invalid_op), NULL) == MT_ERR_INVALID);
    finish_runtime();

    /* TC-SEL-ERR-004 */
    reset_runtime();
    g_ch1 = mt_chan_create(sizeof(int), 0);
    CHECK(g_ch1 != NULL);
    mt_test_fail_next_select_alloc();
    CHECK(mt_go(task_select_recv_ch1, NULL) > 0);
    CHECK(mt_run() == MT_OK);
    CHECK(g_rc1 == MT_ERR_NOMEM);
    CHECK(mt_debug_channel_waiting_task_count() == 0);
    assert_select_counters_balanced();
    CHECK(mt_chan_destroy(g_ch1) == MT_OK);
    g_ch1 = NULL;
    finish_runtime();

    /* TC-SEL-ERR-005 */
    reset_runtime();
    g_ch1 = mt_chan_create(sizeof(int), 0);
    CHECK(g_ch1 != NULL);
    mt_test_fail_next_timer_alloc();
    uint64_t long_ms = 1000;
    CHECK(mt_go(task_select_recv_ch1_timeout, &long_ms) > 0);
    CHECK(mt_run() == MT_OK);
    CHECK(g_rc1 == MT_ERR_NOMEM);
    CHECK(mt_debug_channel_waiting_task_count() == 0);
    assert_select_counters_balanced();
    CHECK(mt_debug_sleeping_task_count() == 0);
    CHECK(mt_chan_destroy(g_ch1) == MT_OK);
    g_ch1 = NULL;
    finish_runtime();

    /* TC-SEL-ERR-006 */
    reset_runtime();
    g_ch1 = mt_chan_create(sizeof(int), 0);
    g_ch2 = mt_chan_create(sizeof(int), 0);
    CHECK(g_ch1 != NULL);
    CHECK(g_ch2 != NULL);
    mt_test_fail_next_select_alloc();
    CHECK(mt_go(task_select_recv_two_channels, NULL) > 0);
    CHECK(mt_run() == MT_OK);
    CHECK(g_rc1 == MT_ERR_NOMEM);
    CHECK(mt_debug_channel_waiting_task_count() == 0);
    CHECK(mt_chan_destroy(g_ch1) == MT_OK);
    CHECK(mt_chan_destroy(g_ch2) == MT_OK);
    g_ch1 = NULL;
    g_ch2 = NULL;
    assert_select_counters_balanced();
    finish_runtime();

    /* TC-SEL-MEM-001 */
    reset_runtime();
    g_ch1 = mt_chan_create(sizeof(int), 0);
    CHECK(g_ch1 != NULL);
    CHECK(mt_go(task_select_recv_ch1, NULL) > 0);
    CHECK(mt_run() == MT_ERR_STATE);
    CHECK(mt_chan_try_send(g_ch1, &g_value1) == MT_OK);
    CHECK(mt_run() == MT_OK);
    assert_select_counters_balanced();
    CHECK(mt_chan_destroy(g_ch1) == MT_OK);
    g_ch1 = NULL;
    finish_runtime();

    /* TC-SEL-MEM-002 */
    reset_runtime();
    uint64_t one_ms = 1;
    g_ch1 = mt_chan_create(sizeof(int), 0);
    CHECK(g_ch1 != NULL);
    CHECK(mt_go(task_select_recv_ch1_timeout, &one_ms) > 0);
    CHECK(mt_run() == MT_OK);
    assert_select_counters_balanced();
    CHECK(mt_chan_destroy(g_ch1) == MT_OK);
    g_ch1 = NULL;
    finish_runtime();

    /* TC-SEL-MEM-003 */
    reset_runtime();
    g_ch1 = mt_chan_create(sizeof(int), 0);
    CHECK(g_ch1 != NULL);
    CHECK(mt_go(task_select_recv_ch1, NULL) > 0);
    CHECK(mt_run() == MT_ERR_STATE);
    CHECK(mt_chan_close(g_ch1) == MT_OK);
    CHECK(mt_run() == MT_OK);
    CHECK(g_rc1 == MT_ERR_CLOSED);
    assert_select_counters_balanced();
    CHECK(mt_chan_destroy(g_ch1) == MT_OK);
    g_ch1 = NULL;
    finish_runtime();

    /* TC-SEL-MEM-003 */
    reset_runtime();
    g_ch1 = mt_chan_create(sizeof(int), 0);
    CHECK(g_ch1 != NULL);
    CHECK(mt_go(task_select_recv_ch1, NULL) > 0);
    CHECK(mt_run() == MT_ERR_STATE);
    CHECK(mt_chan_destroy(g_ch1) == MT_OK);
    g_ch1 = NULL;
    CHECK(mt_run() == MT_OK);
    assert_select_counters_balanced();
    finish_runtime();

    /* TC-SEL-MEM-004 */
    reset_runtime();
    g_ch1 = mt_chan_create(sizeof(int), 0);
    CHECK(g_ch1 != NULL);
    CHECK(mt_go(task_select_recv_ch1, NULL) > 0);
    CHECK(mt_run() == MT_ERR_STATE);
    finish_runtime();
    reset_runtime();
    assert_select_counters_balanced();
    finish_runtime();
}

static void test_select_stress_matrix(void) {
    /* TC-SEL-STRESS-001 */
    reset_runtime();
    g_ch1 = mt_chan_create(sizeof(int), 1);
    CHECK(g_ch1 != NULL);
    for (int i = 0; i < 1000; ++i) {
        int value = i;
        int out = -1;
        size_t index = 9999;
        CHECK(mt_chan_send(g_ch1, &value) == MT_OK);
        mt_select_case_t cases[] = {
            { .op = MT_SELECT_RECV, .ch = g_ch1, .value = &out, .timeout_ms = 0 }
        };
        CHECK(mt_select(cases, ARRAY_LEN(cases), &index) == MT_OK);
        CHECK(index == 0);
        CHECK(out == value);
    }
    CHECK(mt_chan_destroy(g_ch1) == MT_OK);
    g_ch1 = NULL;
    finish_runtime();

    /* TC-SEL-STRESS-002 */
    reset_runtime();
    g_ch1 = mt_chan_create(sizeof(int), 0);
    CHECK(g_ch1 != NULL);
    for (int i = 0; i < 1000; ++i) {
        g_value2 = i;
        CHECK(mt_go(task_select_recv_ch1, NULL) > 0);
        CHECK(mt_run() == MT_ERR_STATE);
        CHECK(mt_chan_try_send(g_ch1, &g_value2) == MT_OK);
        CHECK(mt_run() == MT_OK);
        CHECK(g_out1 == i);
        g_out1 = -1111;
    }
    CHECK(mt_chan_destroy(g_ch1) == MT_OK);
    g_ch1 = NULL;
    finish_runtime();

    /* TC-SEL-STRESS-003 */
    reset_runtime();
    mt_chan_t *channels[STRESS_MANY_CHANNELS];
    select_many_arg_t args[100];
    for (int i = 0; i < STRESS_MANY_CHANNELS; ++i) {
        channels[i] = mt_chan_create(sizeof(int), 100);
        CHECK(channels[i] != NULL);
    }
    for (int task = 0; task < 100; ++task) {
        int value = task;
        CHECK(mt_chan_send(channels[task % STRESS_MANY_CHANNELS], &value) == MT_OK);
    }
    for (int task = 0; task < 100; ++task) {
        args[task].channels = channels;
        args[task].rc = 12345;
        args[task].index = 9999;
        args[task].out = -1;
        CHECK(mt_go(task_select_many_channels, &args[task]) > 0);
    }
    CHECK(mt_run() == MT_OK);
    CHECK(g_counter == 100);
    for (int task = 0; task < 100; ++task) {
        CHECK(args[task].rc == MT_OK);
        CHECK(args[task].index < STRESS_MANY_CHANNELS);
        CHECK(args[task].out >= 0);
    }
    for (int i = 0; i < STRESS_MANY_CHANNELS; ++i) {
        CHECK(mt_chan_destroy(channels[i]) == MT_OK);
    }
    finish_runtime();

    /* TC-SEL-STRESS-004 */
    reset_runtime();
    uint64_t one_ms = 1;
    g_ch1 = mt_chan_create(sizeof(int), 0);
    g_ch2 = mt_chan_create(sizeof(int), 2);
    CHECK(g_ch1 != NULL);
    CHECK(g_ch2 != NULL);
    g_handle1 = mt_go_handle(task_join_target, NULL);
    CHECK(g_handle1 != NULL);
    CHECK(mt_go(task_join_waiter, g_handle1) > 0);
    CHECK(mt_go(task_sleep_marker, &one_ms) > 0);
    CHECK(mt_go(task_select_recv_ch1_timeout, &one_ms) > 0);
    CHECK(mt_go(task_yield_then_try_send_ch1, NULL) > 0);
    CHECK(mt_chan_try_send(g_ch2, &g_value3) == MT_OK);
    CHECK(mt_run() == MT_OK);
    CHECK(g_rc1 == MT_OK);
    CHECK(g_index1 == 0 || g_index1 == 1);
    CHECK(g_rc3 == MT_OK);
    CHECK(g_marker1 >= 2);
    CHECK(mt_chan_try_recv(g_ch2, &g_out3) == MT_OK);
    CHECK(g_out3 == g_value3);
    mt_task_handle_release(g_handle1);
    g_handle1 = NULL;
    CHECK(mt_chan_destroy(g_ch1) == MT_OK);
    CHECK(mt_chan_destroy(g_ch2) == MT_OK);
    g_ch1 = NULL;
    g_ch2 = NULL;
    finish_runtime();
}

int main(void) {
    test_try_send_matrix();
    test_try_recv_matrix();
    test_select_immediate_matrix();
    test_select_blocking_matrix();
    test_select_default_timeout_matrix();
    test_select_close_destroy_matrix();
    test_select_scheduler_lifecycle_matrix();
    test_select_error_fault_memory_matrix();
    test_select_stress_matrix();
    printf("v0.5 test plan suite passed\n");
    return 0;
}
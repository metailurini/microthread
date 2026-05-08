#include "gt.h"

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#define CHECK(cond) do { \
    if (!(cond)) { \
        fprintf(stderr, "CHECK failed at %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        abort(); \
    } \
} while (0)

#define ARRAY_LEN(a) (sizeof(a) / sizeof((a)[0]))

static gt_chan_t *g_ch1;
static gt_chan_t *g_ch2;
static gt_chan_t *g_ch3;
static gt_task_handle_t *g_handle1;
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
    gt_chan_t **channels;
    int rc;
    size_t index;
    int out;
} select_many_arg_t;

static void reset_runtime(void) {
    gt_shutdown();
    gt_test_reset_faults();
    CHECK(gt_init() == GT_OK);
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
        gt_task_handle_release(g_handle1);
        g_handle1 = NULL;
    }
    gt_shutdown();
    gt_test_reset_faults();
}

static void assert_select_counters_balanced(void) {
    size_t allocs = 0;
    size_t frees = 0;
    gt_test_select_memory_counters(&allocs, &frees);
    CHECK(allocs == frees);
}

static void assert_timer_counters_balanced(void) {
    size_t task_allocs = 0;
    size_t task_frees = 0;
    size_t stack_allocs = 0;
    size_t stack_frees = 0;
    size_t timer_allocs = 0;
    size_t timer_frees = 0;
    gt_test_memory_counters(&task_allocs, &task_frees,
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
    g_rc1 = gt_chan_recv(g_ch1, &g_out1);
    g_counter++;
}

static void task_recv_ch2(void *arg) {
    (void)arg;
    g_rc2 = gt_chan_recv(g_ch2, &g_out2);
    g_counter++;
}

static void task_send_ch1(void *arg) {
    (void)arg;
    g_rc1 = gt_chan_send(g_ch1, &g_value1);
    g_counter++;
}

static void task_select_recv_ch1(void *arg) {
    (void)arg;
    gt_select_case_t cases[] = {
        { .op = GT_SELECT_RECV, .ch = g_ch1, .value = &g_out1, .timeout_ms = 0 }
    };
    g_rc1 = gt_select(cases, ARRAY_LEN(cases), &g_index1);
    g_counter++;
}

static void task_select_send_ch1(void *arg) {
    (void)arg;
    gt_select_case_t cases[] = {
        { .op = GT_SELECT_SEND, .ch = g_ch1, .value = &g_value1, .timeout_ms = 0 }
    };
    g_rc1 = gt_select(cases, ARRAY_LEN(cases), &g_index1);
    g_counter++;
}

static void task_select_recv_ch1_second(void *arg) {
    (void)arg;
    gt_select_case_t cases[] = {
        { .op = GT_SELECT_RECV, .ch = g_ch1, .value = &g_out2, .timeout_ms = 0 }
    };
    g_rc2 = gt_select(cases, ARRAY_LEN(cases), &g_index2);
    g_counter++;
}

static void task_select_recv_two_channels(void *arg) {
    (void)arg;
    gt_select_case_t cases[] = {
        { .op = GT_SELECT_RECV, .ch = g_ch1, .value = &g_out1, .timeout_ms = 0 },
        { .op = GT_SELECT_RECV, .ch = g_ch2, .value = &g_out2, .timeout_ms = 0 }
    };
    g_rc1 = gt_select(cases, ARRAY_LEN(cases), &g_index1);
    g_counter++;
}

static void task_select_recv_two_channels_timeout(void *arg) {
    uint64_t timeout_ms = arg ? *(uint64_t *)arg : 1u;
    gt_select_case_t cases[] = {
        { .op = GT_SELECT_RECV, .ch = g_ch1, .value = &g_out1, .timeout_ms = 0 },
        { .op = GT_SELECT_RECV, .ch = g_ch2, .value = &g_out2, .timeout_ms = 0 },
        { .op = GT_SELECT_TIMEOUT, .ch = NULL, .value = NULL, .timeout_ms = timeout_ms }
    };
    g_rc1 = gt_select(cases, ARRAY_LEN(cases), &g_index1);
    g_counter++;
}

static void task_select_recv_ch1_timeout(void *arg) {
    uint64_t timeout_ms = arg ? *(uint64_t *)arg : 1u;
    gt_select_case_t cases[] = {
        { .op = GT_SELECT_RECV, .ch = g_ch1, .value = &g_out1, .timeout_ms = 0 },
        { .op = GT_SELECT_TIMEOUT, .ch = NULL, .value = NULL, .timeout_ms = timeout_ms }
    };
    g_rc1 = gt_select(cases, ARRAY_LEN(cases), &g_index1);
    g_counter++;
}

static void task_yield_then_try_send_ch1(void *arg) {
    (void)arg;
    gt_yield();
    g_rc2 = gt_chan_try_send(g_ch1, &g_value2);
    g_counter++;
}

static void task_yield_then_recv_ch1(void *arg) {
    (void)arg;
    gt_yield();
    g_rc2 = gt_chan_recv(g_ch1, &g_out2);
    g_counter++;
}

static void task_ready_counter(void *arg) {
    int *counter = (int *)arg;
    (*counter)++;
}

static void task_join_target(void *arg) {
    (void)arg;
    gt_yield();
    g_marker1 = 1;
}

static void task_join_waiter(void *arg) {
    gt_task_handle_t *handle = (gt_task_handle_t *)arg;
    g_rc3 = gt_join(handle);
    g_marker2 = 1;
}

static void task_sleep_marker(void *arg) {
    uint64_t ms = arg ? *(uint64_t *)arg : 1u;
    gt_sleep_ms(ms);
    g_marker1++;
}

static void task_select_many_channels(void *arg) {
    select_many_arg_t *ctx = (select_many_arg_t *)arg;
    int outs[STRESS_MANY_CHANNELS];
    gt_select_case_t cases[STRESS_MANY_CHANNELS];

    ctx->rc = 12345;
    ctx->index = 9999;
    ctx->out = -1;
    for (int i = 0; i < STRESS_MANY_CHANNELS; ++i) {
        outs[i] = -1;
        cases[i].op = GT_SELECT_RECV;
        cases[i].ch = ctx->channels[i];
        cases[i].value = &outs[i];
        cases[i].timeout_ms = 0;
    }

    ctx->rc = gt_select(cases, ARRAY_LEN(cases), &ctx->index);
    if (ctx->rc == GT_OK && ctx->index < STRESS_MANY_CHANNELS) {
        ctx->out = outs[ctx->index];
    }
    g_counter++;
}

static void test_try_send_matrix(void) {
    /* TC-TRY-SEND-001 */
    reset_runtime();
    g_ch1 = gt_chan_create(sizeof(int), 1);
    CHECK(g_ch1 != NULL);
    CHECK(gt_chan_try_send(g_ch1, &g_value1) == GT_OK);
    CHECK(gt_chan_len(g_ch1) == 1);
    CHECK(gt_chan_recv(g_ch1, &g_out1) == GT_OK);
    CHECK(g_out1 == g_value1);
    CHECK(gt_chan_destroy(g_ch1) == GT_OK);
    g_ch1 = NULL;
    finish_runtime();

    /* TC-TRY-SEND-002 */
    reset_runtime();
    g_ch1 = gt_chan_create(sizeof(int), 1);
    CHECK(g_ch1 != NULL);
    CHECK(gt_chan_try_send(g_ch1, &g_value1) == GT_OK);
    CHECK(gt_chan_try_send(g_ch1, &g_value2) == GT_ERR_WOULD_BLOCK);
    CHECK(gt_debug_channel_waiting_task_count() == 0);
    CHECK(gt_chan_len(g_ch1) == 1);
    CHECK(gt_chan_recv(g_ch1, &g_out1) == GT_OK);
    CHECK(g_out1 == g_value1);
    CHECK(gt_chan_destroy(g_ch1) == GT_OK);
    g_ch1 = NULL;
    finish_runtime();

    /* TC-TRY-SEND-003 */
    reset_runtime();
    g_ch1 = gt_chan_create(sizeof(int), 0);
    CHECK(g_ch1 != NULL);
    CHECK(gt_go(task_recv_ch1, NULL) > 0);
    CHECK(gt_run() == GT_ERR_STATE);
    CHECK(gt_debug_channel_waiting_task_count() == 1);
    CHECK(gt_chan_try_send(g_ch1, &g_value1) == GT_OK);
    CHECK(gt_run() == GT_OK);
    CHECK(g_rc1 == GT_OK);
    CHECK(g_out1 == g_value1);
    CHECK(g_counter == 1);
    CHECK(gt_chan_destroy(g_ch1) == GT_OK);
    g_ch1 = NULL;
    finish_runtime();

    /* TC-TRY-SEND-004, TC-TRY-SEND-007 */
    reset_runtime();
    g_ch1 = gt_chan_create(sizeof(int), 0);
    CHECK(g_ch1 != NULL);
    CHECK(gt_chan_try_send(g_ch1, &g_value1) == GT_ERR_WOULD_BLOCK);
    CHECK(gt_debug_channel_waiting_task_count() == 0);
    CHECK(gt_chan_destroy(g_ch1) == GT_OK);
    g_ch1 = NULL;
    finish_runtime();

    /* TC-TRY-SEND-005, TC-TRY-SEND-006 */
    reset_runtime();
    g_ch1 = gt_chan_create(sizeof(int), 1);
    CHECK(g_ch1 != NULL);
    CHECK(gt_chan_close(g_ch1) == GT_OK);
    CHECK(gt_chan_try_send(g_ch1, &g_value1) == GT_ERR_CLOSED);
    CHECK(gt_chan_try_send(NULL, &g_value1) == GT_ERR_INVALID);
    CHECK(gt_chan_try_send(g_ch1, NULL) == GT_ERR_INVALID);
    CHECK(gt_chan_destroy(g_ch1) == GT_OK);
    g_ch1 = NULL;
    finish_runtime();
}

static void test_try_recv_matrix(void) {
    /* TC-TRY-RECV-001 */
    reset_runtime();
    g_ch1 = gt_chan_create(sizeof(int), 1);
    CHECK(g_ch1 != NULL);
    CHECK(gt_chan_send(g_ch1, &g_value1) == GT_OK);
    CHECK(gt_chan_try_recv(g_ch1, &g_out1) == GT_OK);
    CHECK(g_out1 == g_value1);
    CHECK(gt_chan_destroy(g_ch1) == GT_OK);
    g_ch1 = NULL;
    finish_runtime();

    /* TC-TRY-RECV-002, TC-TRY-RECV-004 */
    reset_runtime();
    g_ch1 = gt_chan_create(sizeof(int), 0);
    CHECK(g_ch1 != NULL);
    CHECK(gt_chan_try_recv(g_ch1, &g_out1) == GT_ERR_WOULD_BLOCK);
    CHECK(gt_debug_channel_waiting_task_count() == 0);
    CHECK(gt_chan_destroy(g_ch1) == GT_OK);
    g_ch1 = NULL;
    finish_runtime();

    /* TC-TRY-RECV-003 */
    reset_runtime();
    g_ch1 = gt_chan_create(sizeof(int), 0);
    CHECK(g_ch1 != NULL);
    CHECK(gt_go(task_send_ch1, NULL) > 0);
    CHECK(gt_run() == GT_ERR_STATE);
    CHECK(gt_debug_channel_waiting_task_count() == 1);
    CHECK(gt_chan_try_recv(g_ch1, &g_out1) == GT_OK);
    CHECK(g_out1 == g_value1);
    CHECK(gt_run() == GT_OK);
    CHECK(g_rc1 == GT_OK);
    CHECK(g_counter == 1);
    CHECK(gt_chan_destroy(g_ch1) == GT_OK);
    g_ch1 = NULL;
    finish_runtime();

    /* TC-TRY-RECV-005, TC-TRY-RECV-006, TC-TRY-RECV-007 */
    reset_runtime();
    g_ch1 = gt_chan_create(sizeof(int), 2);
    CHECK(g_ch1 != NULL);
    CHECK(gt_chan_send(g_ch1, &g_value1) == GT_OK);
    CHECK(gt_chan_close(g_ch1) == GT_OK);
    CHECK(gt_chan_try_recv(g_ch1, &g_out1) == GT_OK);
    CHECK(g_out1 == g_value1);
    CHECK(gt_chan_try_recv(g_ch1, &g_out2) == GT_ERR_CLOSED);
    CHECK(gt_chan_try_recv(NULL, &g_out1) == GT_ERR_INVALID);
    CHECK(gt_chan_try_recv(g_ch1, NULL) == GT_ERR_INVALID);
    CHECK(gt_chan_destroy(g_ch1) == GT_OK);
    g_ch1 = NULL;
    finish_runtime();
}

static void test_select_immediate_matrix(void) {
    /* TC-SEL-IMM-001, TC-SEL-IMM-006 */
    reset_runtime();
    g_ch1 = gt_chan_create(sizeof(int), 1);
    CHECK(g_ch1 != NULL);
    CHECK(gt_chan_send(g_ch1, &g_value1) == GT_OK);
    gt_select_case_t recv_ready[] = {
        { .op = GT_SELECT_RECV, .ch = g_ch1, .value = &g_out1, .timeout_ms = 0 }
    };
    CHECK(gt_select(recv_ready, ARRAY_LEN(recv_ready), &g_index1) == GT_OK);
    CHECK(g_index1 == 0);
    CHECK(g_out1 == g_value1);
    CHECK(gt_chan_destroy(g_ch1) == GT_OK);
    g_ch1 = NULL;
    finish_runtime();

    /* TC-SEL-IMM-002 */
    reset_runtime();
    g_ch1 = gt_chan_create(sizeof(int), 1);
    CHECK(g_ch1 != NULL);
    gt_select_case_t send_ready[] = {
        { .op = GT_SELECT_SEND, .ch = g_ch1, .value = &g_value1, .timeout_ms = 0 }
    };
    CHECK(gt_select(send_ready, ARRAY_LEN(send_ready), &g_index1) == GT_OK);
    CHECK(g_index1 == 0);
    CHECK(gt_chan_recv(g_ch1, &g_out1) == GT_OK);
    CHECK(g_out1 == g_value1);
    CHECK(gt_chan_destroy(g_ch1) == GT_OK);
    g_ch1 = NULL;
    finish_runtime();

    /* TC-SEL-IMM-003 */
    reset_runtime();
    g_ch1 = gt_chan_create(sizeof(int), 0);
    CHECK(g_ch1 != NULL);
    CHECK(gt_go(task_send_ch1, NULL) > 0);
    CHECK(gt_run() == GT_ERR_STATE);
    gt_select_case_t recv_sender[] = {
        { .op = GT_SELECT_RECV, .ch = g_ch1, .value = &g_out1, .timeout_ms = 0 }
    };
    CHECK(gt_select(recv_sender, ARRAY_LEN(recv_sender), &g_index1) == GT_OK);
    CHECK(g_index1 == 0);
    CHECK(g_out1 == g_value1);
    CHECK(gt_run() == GT_OK);
    CHECK(g_rc1 == GT_OK);
    CHECK(gt_chan_destroy(g_ch1) == GT_OK);
    g_ch1 = NULL;
    finish_runtime();

    /* TC-SEL-IMM-004 */
    reset_runtime();
    g_ch1 = gt_chan_create(sizeof(int), 0);
    CHECK(g_ch1 != NULL);
    CHECK(gt_go(task_recv_ch1, NULL) > 0);
    CHECK(gt_run() == GT_ERR_STATE);
    gt_select_case_t send_receiver[] = {
        { .op = GT_SELECT_SEND, .ch = g_ch1, .value = &g_value1, .timeout_ms = 0 }
    };
    CHECK(gt_select(send_receiver, ARRAY_LEN(send_receiver), &g_index1) == GT_OK);
    CHECK(g_index1 == 0);
    CHECK(gt_run() == GT_OK);
    CHECK(g_rc1 == GT_OK);
    CHECK(g_out1 == g_value1);
    CHECK(gt_chan_destroy(g_ch1) == GT_OK);
    g_ch1 = NULL;
    finish_runtime();

    /* TC-SEL-IMM-005, TC-SEL-IMM-007 */
    reset_runtime();
    g_ch1 = gt_chan_create(sizeof(int), 1);
    g_ch2 = gt_chan_create(sizeof(int), 1);
    CHECK(g_ch1 != NULL);
    CHECK(g_ch2 != NULL);
    CHECK(gt_chan_send(g_ch1, &g_value1) == GT_OK);
    CHECK(gt_chan_send(g_ch2, &g_value2) == GT_OK);
    g_out1 = 101;
    g_out2 = 202;
    gt_select_case_t two_ready[] = {
        { .op = GT_SELECT_RECV, .ch = g_ch1, .value = &g_out1, .timeout_ms = 0 },
        { .op = GT_SELECT_RECV, .ch = g_ch2, .value = &g_out2, .timeout_ms = 0 }
    };
    CHECK(gt_select(two_ready, ARRAY_LEN(two_ready), &g_index1) == GT_OK);
    CHECK(g_index1 == 0 || g_index1 == 1);
    if (g_index1 == 0) {
        CHECK(g_out1 == g_value1);
        CHECK(g_out2 == 202);
        CHECK(gt_chan_len(g_ch1) == 0);
        CHECK(gt_chan_len(g_ch2) == 1);
    } else {
        CHECK(g_out1 == 101);
        CHECK(g_out2 == g_value2);
        CHECK(gt_chan_len(g_ch1) == 1);
        CHECK(gt_chan_len(g_ch2) == 0);
    }
    CHECK(gt_chan_destroy(g_ch1) == GT_OK);
    CHECK(gt_chan_destroy(g_ch2) == GT_OK);
    g_ch1 = NULL;
    g_ch2 = NULL;
    finish_runtime();
}

static void test_select_blocking_matrix(void) {
    /* TC-SEL-BLOCK-001, TC-SEL-BLOCK-007 */
    reset_runtime();
    g_ch1 = gt_chan_create(sizeof(int), 0);
    CHECK(g_ch1 != NULL);
    CHECK(gt_go(task_select_recv_ch1, NULL) > 0);
    CHECK(gt_run() == GT_ERR_STATE);
    CHECK(g_counter == 0);
    CHECK(gt_debug_runnable_count() == 0);
    CHECK(gt_debug_channel_waiting_task_count() == 1);
    CHECK(gt_chan_close(g_ch1) == GT_OK);
    CHECK(gt_run() == GT_OK);
    CHECK(g_rc1 == GT_ERR_CLOSED);
    CHECK(g_index1 == 0);
    CHECK(g_counter == 1);
    CHECK(gt_chan_destroy(g_ch1) == GT_OK);
    g_ch1 = NULL;
    finish_runtime();

    /* TC-SEL-BLOCK-002 */
    reset_runtime();
    g_ch1 = gt_chan_create(sizeof(int), 0);
    CHECK(g_ch1 != NULL);
    CHECK(gt_go(task_select_recv_ch1, NULL) > 0);
    CHECK(gt_go(task_yield_then_try_send_ch1, NULL) > 0);
    CHECK(gt_run() == GT_OK);
    CHECK(g_rc1 == GT_OK);
    CHECK(g_rc2 == GT_OK);
    CHECK(g_out1 == g_value2);
    CHECK(g_index1 == 0);
    CHECK(g_counter == 2);
    CHECK(gt_chan_destroy(g_ch1) == GT_OK);
    g_ch1 = NULL;
    finish_runtime();

    /* TC-SEL-BLOCK-003 */
    reset_runtime();
    g_ch1 = gt_chan_create(sizeof(int), 0);
    CHECK(g_ch1 != NULL);
    CHECK(gt_go(task_select_send_ch1, NULL) > 0);
    CHECK(gt_go(task_yield_then_recv_ch1, NULL) > 0);
    CHECK(gt_run() == GT_OK);
    CHECK(g_rc1 == GT_OK);
    CHECK(g_rc2 == GT_OK);
    CHECK(g_out2 == g_value1);
    CHECK(g_index1 == 0);
    CHECK(g_counter == 2);
    CHECK(gt_chan_destroy(g_ch1) == GT_OK);
    g_ch1 = NULL;
    finish_runtime();

    /* TC-SEL-BLOCK-004 */
    reset_runtime();
    g_ch1 = gt_chan_create(sizeof(int), 0);
    CHECK(g_ch1 != NULL);
    CHECK(gt_go(task_select_recv_ch1, NULL) > 0);
    CHECK(gt_go(task_select_recv_ch1_second, NULL) > 0);
    CHECK(gt_run() == GT_ERR_STATE);
    CHECK(g_counter == 0);
    CHECK(gt_chan_try_send(g_ch1, &g_value1) == GT_OK);
    CHECK(gt_run() == GT_ERR_STATE);
    CHECK(g_counter == 1);
    CHECK(gt_chan_try_send(g_ch1, &g_value2) == GT_OK);
    CHECK(gt_run() == GT_OK);
    CHECK(g_counter == 2);
    CHECK(((g_out1 == g_value1) && (g_out2 == g_value2)) ||
          ((g_out1 == g_value2) && (g_out2 == g_value1)));
    CHECK(gt_chan_destroy(g_ch1) == GT_OK);
    g_ch1 = NULL;
    finish_runtime();

    /* TC-SEL-BLOCK-005 */
    reset_runtime();
    g_ch1 = gt_chan_create(sizeof(int), 0);
    g_ch2 = gt_chan_create(sizeof(int), 0);
    CHECK(g_ch1 != NULL);
    CHECK(g_ch2 != NULL);
    CHECK(gt_go(task_select_recv_two_channels, NULL) > 0);
    CHECK(gt_run() == GT_ERR_STATE);
    CHECK(gt_chan_try_send(g_ch2, &g_value2) == GT_OK);
    CHECK(gt_run() == GT_OK);
    CHECK(g_rc1 == GT_OK);
    CHECK(g_index1 == 1);
    CHECK(g_out2 == g_value2);
    CHECK(gt_debug_channel_waiting_task_count() == 0);
    CHECK(gt_chan_try_send(g_ch1, &g_value1) == GT_ERR_WOULD_BLOCK);
    CHECK(gt_chan_destroy(g_ch1) == GT_OK);
    CHECK(gt_chan_destroy(g_ch2) == GT_OK);
    g_ch1 = NULL;
    g_ch2 = NULL;
    finish_runtime();

    /* TC-SEL-BLOCK-006 */
    reset_runtime();
    g_ch1 = gt_chan_create(sizeof(int), 0);
    CHECK(g_ch1 != NULL);
    CHECK(gt_go(task_recv_ch1, NULL) > 0);
    CHECK(gt_go(task_select_recv_ch1_second, NULL) > 0);
    CHECK(gt_run() == GT_ERR_STATE);
    CHECK(gt_debug_channel_waiting_task_count() == 2);
    CHECK(gt_chan_try_send(g_ch1, &g_value1) == GT_OK);
    CHECK(gt_run() == GT_ERR_STATE);
    CHECK(g_counter == 1);
    CHECK(gt_chan_try_send(g_ch1, &g_value2) == GT_OK);
    CHECK(gt_run() == GT_OK);
    CHECK(g_counter == 2);
    CHECK(g_rc1 == GT_OK);
    CHECK(g_rc2 == GT_OK);
    CHECK(gt_chan_destroy(g_ch1) == GT_OK);
    g_ch1 = NULL;
    finish_runtime();
}

static void test_select_default_timeout_matrix(void) {
    /* TC-SEL-DEFAULT-001, TC-SEL-DEFAULT-004 */
    reset_runtime();
    g_ch1 = gt_chan_create(sizeof(int), 0);
    CHECK(g_ch1 != NULL);
    gt_select_case_t default_ready[] = {
        { .op = GT_SELECT_RECV, .ch = g_ch1, .value = &g_out1, .timeout_ms = 0 },
        { .op = GT_SELECT_DEFAULT, .ch = NULL, .value = NULL, .timeout_ms = 0 }
    };
    CHECK(gt_select(default_ready, ARRAY_LEN(default_ready), &g_index1) == GT_OK);
    CHECK(g_index1 == 1);
    CHECK(gt_debug_channel_waiting_task_count() == 0);
    CHECK(gt_chan_destroy(g_ch1) == GT_OK);
    g_ch1 = NULL;
    finish_runtime();

    /* TC-SEL-DEFAULT-002 */
    reset_runtime();
    g_ch1 = gt_chan_create(sizeof(int), 1);
    CHECK(g_ch1 != NULL);
    CHECK(gt_chan_send(g_ch1, &g_value1) == GT_OK);
    gt_select_case_t default_loses[] = {
        { .op = GT_SELECT_RECV, .ch = g_ch1, .value = &g_out1, .timeout_ms = 0 },
        { .op = GT_SELECT_DEFAULT, .ch = NULL, .value = NULL, .timeout_ms = 0 }
    };
    CHECK(gt_select(default_loses, ARRAY_LEN(default_loses), &g_index1) == GT_OK);
    CHECK(g_index1 == 0);
    CHECK(g_out1 == g_value1);
    CHECK(gt_chan_destroy(g_ch1) == GT_OK);
    g_ch1 = NULL;
    finish_runtime();

    /* TC-SEL-DEFAULT-003 */
    reset_runtime();
    gt_select_case_t duplicate_defaults[] = {
        { .op = GT_SELECT_DEFAULT, .ch = NULL, .value = NULL, .timeout_ms = 0 },
        { .op = GT_SELECT_DEFAULT, .ch = NULL, .value = NULL, .timeout_ms = 0 }
    };
    CHECK(gt_select(duplicate_defaults, ARRAY_LEN(duplicate_defaults), &g_index1) == GT_ERR_INVALID);
    finish_runtime();

    /* TC-SEL-TIMEOUT-001 */
    reset_runtime();
    uint64_t one_ms = 1;
    g_ch1 = gt_chan_create(sizeof(int), 0);
    CHECK(g_ch1 != NULL);
    CHECK(gt_go(task_select_recv_ch1_timeout, &one_ms) > 0);
    CHECK(gt_run() == GT_OK);
    CHECK(g_rc1 == GT_OK);
    CHECK(g_index1 == 1);
    CHECK(g_counter == 1);
    CHECK(gt_debug_channel_waiting_task_count() == 0);
    CHECK(gt_chan_destroy(g_ch1) == GT_OK);
    g_ch1 = NULL;
    finish_runtime();

    /* TC-SEL-TIMEOUT-002 */
    reset_runtime();
    uint64_t long_ms = 1000;
    one_ms = 1;
    g_ch1 = gt_chan_create(sizeof(int), 0);
    CHECK(g_ch1 != NULL);
    CHECK(gt_go(task_select_recv_ch1_timeout, &long_ms) > 0);
    CHECK(gt_go(task_yield_then_try_send_ch1, NULL) > 0);
    CHECK(gt_run() == GT_OK);
    CHECK(g_rc1 == GT_OK);
    CHECK(g_index1 == 0);
    CHECK(g_out1 == g_value2);
    CHECK(g_rc2 == GT_OK);
    CHECK(gt_debug_sleeping_task_count() == 0);
    CHECK(gt_chan_destroy(g_ch1) == GT_OK);
    g_ch1 = NULL;
    finish_runtime();

    /* TC-SEL-TIMEOUT-003 */
    reset_runtime();
    g_ch1 = gt_chan_create(sizeof(int), 0);
    CHECK(g_ch1 != NULL);
    gt_select_case_t zero_timeout[] = {
        { .op = GT_SELECT_RECV, .ch = g_ch1, .value = &g_out1, .timeout_ms = 0 },
        { .op = GT_SELECT_TIMEOUT, .ch = NULL, .value = NULL, .timeout_ms = 0 }
    };
    CHECK(gt_select(zero_timeout, ARRAY_LEN(zero_timeout), &g_index1) == GT_OK);
    CHECK(g_index1 == 1);
    CHECK(gt_debug_channel_waiting_task_count() == 0);
    CHECK(gt_chan_destroy(g_ch1) == GT_OK);
    g_ch1 = NULL;
    finish_runtime();

    /* TC-SEL-TIMEOUT-004 */
    reset_runtime();
    gt_select_case_t duplicate_timeouts[] = {
        { .op = GT_SELECT_TIMEOUT, .ch = NULL, .value = NULL, .timeout_ms = 1 },
        { .op = GT_SELECT_TIMEOUT, .ch = NULL, .value = NULL, .timeout_ms = 2 }
    };
    CHECK(gt_select(duplicate_timeouts, ARRAY_LEN(duplicate_timeouts), &g_index1) == GT_ERR_INVALID);
    finish_runtime();

    /* TC-SEL-TIMEOUT-005 */
    reset_runtime();
    one_ms = 1;
    g_ch1 = gt_chan_create(sizeof(int), 0);
    g_ch2 = gt_chan_create(sizeof(int), 0);
    CHECK(g_ch1 != NULL);
    CHECK(g_ch2 != NULL);
    CHECK(gt_go(task_select_recv_two_channels_timeout, &one_ms) > 0);
    CHECK(gt_run() == GT_OK);
    CHECK(g_rc1 == GT_OK);
    CHECK(g_index1 == 2);
    CHECK(g_counter == 1);
    CHECK(gt_debug_channel_waiting_task_count() == 0);
    CHECK(gt_chan_try_send(g_ch1, &g_value1) == GT_ERR_WOULD_BLOCK);
    CHECK(gt_chan_try_send(g_ch2, &g_value2) == GT_ERR_WOULD_BLOCK);
    assert_select_counters_balanced();
    CHECK(gt_chan_destroy(g_ch1) == GT_OK);
    CHECK(gt_chan_destroy(g_ch2) == GT_OK);
    g_ch1 = NULL;
    g_ch2 = NULL;
    finish_runtime();

    /* TC-SEL-TIMEOUT-006 */
    reset_runtime();
    long_ms = 1000000;
    g_ch1 = gt_chan_create(sizeof(int), 0);
    CHECK(g_ch1 != NULL);
    CHECK(gt_go(task_select_recv_ch1_timeout, &long_ms) > 0);
    CHECK(gt_test_run_until_blocked() == GT_ERR_STATE);
    CHECK(gt_debug_sleeping_task_count() == 1);
    CHECK(gt_debug_channel_waiting_task_count() == 1);
    gt_chan_t *pending_timeout_ch = g_ch1;
    finish_runtime();
    CHECK(gt_chan_destroy(pending_timeout_ch) == GT_OK);
    g_ch1 = NULL;
    reset_runtime();
    assert_select_counters_balanced();
    assert_timer_counters_balanced();
    finish_runtime();
}

static void test_select_close_destroy_matrix(void) {
    /* TC-SEL-CLOSE-001 */
    reset_runtime();
    g_ch1 = gt_chan_create(sizeof(int), 0);
    CHECK(g_ch1 != NULL);
    CHECK(gt_go(task_select_recv_ch1, NULL) > 0);
    CHECK(gt_run() == GT_ERR_STATE);
    CHECK(gt_chan_close(g_ch1) == GT_OK);
    CHECK(gt_run() == GT_OK);
    CHECK(g_rc1 == GT_ERR_CLOSED);
    CHECK(g_index1 == 0);
    CHECK(gt_chan_destroy(g_ch1) == GT_OK);
    g_ch1 = NULL;
    finish_runtime();

    /* TC-SEL-CLOSE-002 */
    reset_runtime();
    g_ch1 = gt_chan_create(sizeof(int), 0);
    CHECK(g_ch1 != NULL);
    CHECK(gt_go(task_select_send_ch1, NULL) > 0);
    CHECK(gt_run() == GT_ERR_STATE);
    CHECK(gt_chan_close(g_ch1) == GT_OK);
    CHECK(gt_run() == GT_OK);
    CHECK(g_rc1 == GT_ERR_CLOSED);
    CHECK(g_index1 == 0);
    CHECK(gt_chan_destroy(g_ch1) == GT_OK);
    g_ch1 = NULL;
    finish_runtime();

    /* TC-SEL-CLOSE-003, TC-SEL-CLOSE-004 */
    reset_runtime();
    g_ch1 = gt_chan_create(sizeof(int), 2);
    CHECK(g_ch1 != NULL);
    CHECK(gt_chan_send(g_ch1, &g_value1) == GT_OK);
    CHECK(gt_chan_close(g_ch1) == GT_OK);
    gt_select_case_t closed_buffered[] = {
        { .op = GT_SELECT_RECV, .ch = g_ch1, .value = &g_out1, .timeout_ms = 0 }
    };
    CHECK(gt_select(closed_buffered, ARRAY_LEN(closed_buffered), &g_index1) == GT_OK);
    CHECK(g_index1 == 0);
    CHECK(g_out1 == g_value1);
    CHECK(gt_select(closed_buffered, ARRAY_LEN(closed_buffered), &g_index1) == GT_ERR_CLOSED);
    CHECK(g_index1 == 0);
    CHECK(gt_chan_destroy(g_ch1) == GT_OK);
    g_ch1 = NULL;
    finish_runtime();

    /* TC-SEL-CLOSE-005 receive side */
    reset_runtime();
    g_ch1 = gt_chan_create(sizeof(int), 0);
    CHECK(g_ch1 != NULL);
    CHECK(gt_go(task_select_recv_ch1, NULL) > 0);
    CHECK(gt_run() == GT_ERR_STATE);
    CHECK(gt_chan_destroy(g_ch1) == GT_OK);
    g_ch1 = NULL;
    CHECK(gt_run() == GT_OK);
    CHECK(g_rc1 == GT_ERR_CLOSED);
    CHECK(g_index1 == 0);
    finish_runtime();

    /* TC-SEL-CLOSE-005 send side */
    reset_runtime();
    g_ch1 = gt_chan_create(sizeof(int), 0);
    CHECK(g_ch1 != NULL);
    CHECK(gt_go(task_select_send_ch1, NULL) > 0);
    CHECK(gt_run() == GT_ERR_STATE);
    CHECK(gt_chan_destroy(g_ch1) == GT_OK);
    g_ch1 = NULL;
    CHECK(gt_run() == GT_OK);
    CHECK(g_rc1 == GT_ERR_CLOSED);
    CHECK(g_index1 == 0);
    finish_runtime();

    /* TC-SEL-CLOSE-006 */
    reset_runtime();
    g_ch1 = gt_chan_create(sizeof(int), 0);
    g_ch2 = gt_chan_create(sizeof(int), 0);
    CHECK(g_ch1 != NULL);
    CHECK(g_ch2 != NULL);
    CHECK(gt_go(task_select_recv_two_channels, NULL) > 0);
    CHECK(gt_run() == GT_ERR_STATE);
    CHECK(gt_chan_destroy(g_ch1) == GT_OK);
    g_ch1 = NULL;
    CHECK(gt_run() == GT_OK);
    CHECK(g_rc1 == GT_ERR_CLOSED);
    CHECK(g_index1 == 0);
    CHECK(g_counter == 1);
    CHECK(gt_chan_destroy(g_ch2) == GT_OK);
    g_ch2 = NULL;
    finish_runtime();
}

static void test_select_scheduler_lifecycle_matrix(void) {
    /* TC-SEL-SCHED-001 */
    reset_runtime();
    g_ch1 = gt_chan_create(sizeof(int), 0);
    CHECK(g_ch1 != NULL);
    int ready_count = 0;
    CHECK(gt_go(task_select_recv_ch1, NULL) > 0);
    CHECK(gt_go(task_ready_counter, &ready_count) > 0);
    CHECK(gt_go(task_ready_counter, &ready_count) > 0);
    CHECK(gt_run() == GT_ERR_STATE);
    CHECK(ready_count == 2);
    CHECK(g_counter == 0);
    CHECK(gt_chan_close(g_ch1) == GT_OK);
    CHECK(gt_run() == GT_OK);
    CHECK(g_counter == 1);
    CHECK(gt_chan_destroy(g_ch1) == GT_OK);
    g_ch1 = NULL;
    finish_runtime();

    /* TC-SEL-SCHED-002 */
    reset_runtime();
    g_ch1 = gt_chan_create(sizeof(int), 0);
    CHECK(g_ch1 != NULL);
    CHECK(gt_go(task_select_recv_ch1, NULL) > 0);
    CHECK(gt_run() == GT_ERR_STATE);
    CHECK(gt_chan_try_send(g_ch1, &g_value1) == GT_OK);
    CHECK(gt_debug_runnable_count() == 1);
    CHECK(gt_chan_close(g_ch1) == GT_OK);
    CHECK(gt_debug_runnable_count() == 1);
    CHECK(gt_run() == GT_OK);
    CHECK(g_counter == 1);
    CHECK(g_rc1 == GT_OK);
    CHECK(g_out1 == g_value1);
    CHECK(gt_chan_destroy(g_ch1) == GT_OK);
    g_ch1 = NULL;
    finish_runtime();

    /* TC-SEL-SCHED-003 */
    reset_runtime();
    uint64_t one_ms = 1;
    g_ch1 = gt_chan_create(sizeof(int), 0);
    g_ch2 = gt_chan_create(sizeof(int), 0);
    CHECK(g_ch1 != NULL);
    CHECK(g_ch2 != NULL);
    g_handle1 = gt_go_handle(task_join_target, NULL);
    CHECK(g_handle1 != NULL);
    CHECK(gt_go(task_join_waiter, g_handle1) > 0);
    CHECK(gt_go(task_recv_ch2, NULL) > 0);
    CHECK(gt_go(task_select_recv_ch1_timeout, &one_ms) > 0);
    CHECK(gt_go(task_sleep_marker, &one_ms) > 0);
    CHECK(gt_run() == GT_ERR_STATE);
    CHECK(g_rc1 == GT_OK);
    CHECK(g_index1 == 1);
    CHECK(g_rc3 == GT_OK);
    CHECK(g_marker1 >= 2);
    CHECK(g_marker2 == 1);
    CHECK(gt_chan_close(g_ch2) == GT_OK);
    CHECK(gt_run() == GT_OK);
    CHECK(g_rc2 == GT_ERR_CLOSED);
    gt_task_handle_release(g_handle1);
    g_handle1 = NULL;
    CHECK(gt_chan_destroy(g_ch1) == GT_OK);
    CHECK(gt_chan_destroy(g_ch2) == GT_OK);
    g_ch1 = NULL;
    g_ch2 = NULL;
    finish_runtime();

    /* TC-SEL-SCHED-004 */
    reset_runtime();
    g_ch1 = gt_chan_create(sizeof(int), 0);
    CHECK(g_ch1 != NULL);
    CHECK(gt_go(task_select_recv_ch1, NULL) > 0);
    CHECK(gt_run() == GT_ERR_STATE);
    CHECK(gt_chan_close(g_ch1) == GT_OK);
    CHECK(gt_run() == GT_OK);
    CHECK(gt_chan_destroy(g_ch1) == GT_OK);
    g_ch1 = NULL;
    finish_runtime();

    /* TC-SEL-SCHED-005 */
    reset_runtime();
    one_ms = 1;
    g_ch1 = gt_chan_create(sizeof(int), 0);
    CHECK(g_ch1 != NULL);
    CHECK(gt_go(task_select_recv_ch1_timeout, &one_ms) > 0);
    CHECK(gt_run() == GT_OK);
    CHECK(g_rc1 == GT_OK);
    CHECK(g_index1 == 1);
    CHECK(gt_chan_destroy(g_ch1) == GT_OK);
    g_ch1 = NULL;
    finish_runtime();

    /* TC-SEL-LIFE-001 */
    reset_runtime();
    g_ch1 = gt_chan_create(sizeof(int), 0);
    CHECK(g_ch1 != NULL);
    CHECK(gt_go(task_select_recv_ch1, NULL) > 0);
    CHECK(gt_run() == GT_ERR_STATE);
    finish_runtime();
    reset_runtime();
    assert_select_counters_balanced();
    finish_runtime();

    /* TC-SEL-LIFE-002 */
    for (int i = 0; i < 10; ++i) {
        reset_runtime();
        g_ch1 = gt_chan_create(sizeof(int), 1);
        CHECK(g_ch1 != NULL);
        CHECK(gt_chan_send(g_ch1, &g_value1) == GT_OK);
        gt_select_case_t cases[] = {
            { .op = GT_SELECT_RECV, .ch = g_ch1, .value = &g_out1, .timeout_ms = 0 }
        };
        CHECK(gt_select(cases, ARRAY_LEN(cases), &g_index1) == GT_OK);
        CHECK(g_index1 == 0);
        CHECK(g_out1 == g_value1);
        CHECK(gt_chan_destroy(g_ch1) == GT_OK);
        g_ch1 = NULL;
        finish_runtime();
    }
}

static void test_select_error_fault_memory_matrix(void) {
    /* TC-SEL-ERR-001, TC-SEL-ERR-002, TC-SEL-ERR-003 */
    reset_runtime();
    gt_select_case_t invalid_op[] = {
        { .op = (gt_select_op_t)99, .ch = NULL, .value = NULL, .timeout_ms = 0 }
    };
    CHECK(gt_select(NULL, 1, &g_index1) == GT_ERR_INVALID);
    CHECK(gt_select(invalid_op, 0, &g_index1) == GT_ERR_INVALID);
    CHECK(gt_select(invalid_op, ARRAY_LEN(invalid_op), &g_index1) == GT_ERR_INVALID);
    CHECK(gt_select(invalid_op, ARRAY_LEN(invalid_op), NULL) == GT_ERR_INVALID);
    finish_runtime();

    /* TC-SEL-ERR-004 */
    reset_runtime();
    g_ch1 = gt_chan_create(sizeof(int), 0);
    CHECK(g_ch1 != NULL);
    gt_test_fail_next_select_alloc();
    CHECK(gt_go(task_select_recv_ch1, NULL) > 0);
    CHECK(gt_run() == GT_OK);
    CHECK(g_rc1 == GT_ERR_NOMEM);
    CHECK(gt_debug_channel_waiting_task_count() == 0);
    assert_select_counters_balanced();
    CHECK(gt_chan_destroy(g_ch1) == GT_OK);
    g_ch1 = NULL;
    finish_runtime();

    /* TC-SEL-ERR-005 */
    reset_runtime();
    g_ch1 = gt_chan_create(sizeof(int), 0);
    CHECK(g_ch1 != NULL);
    gt_test_fail_next_timer_alloc();
    uint64_t long_ms = 1000;
    CHECK(gt_go(task_select_recv_ch1_timeout, &long_ms) > 0);
    CHECK(gt_run() == GT_OK);
    CHECK(g_rc1 == GT_ERR_NOMEM);
    CHECK(gt_debug_channel_waiting_task_count() == 0);
    assert_select_counters_balanced();
    CHECK(gt_debug_sleeping_task_count() == 0);
    CHECK(gt_chan_destroy(g_ch1) == GT_OK);
    g_ch1 = NULL;
    finish_runtime();

    /* TC-SEL-ERR-006 */
    reset_runtime();
    g_ch1 = gt_chan_create(sizeof(int), 0);
    g_ch2 = gt_chan_create(sizeof(int), 0);
    CHECK(g_ch1 != NULL);
    CHECK(g_ch2 != NULL);
    gt_test_fail_next_select_alloc();
    CHECK(gt_go(task_select_recv_two_channels, NULL) > 0);
    CHECK(gt_run() == GT_OK);
    CHECK(g_rc1 == GT_ERR_NOMEM);
    CHECK(gt_debug_channel_waiting_task_count() == 0);
    CHECK(gt_chan_destroy(g_ch1) == GT_OK);
    CHECK(gt_chan_destroy(g_ch2) == GT_OK);
    g_ch1 = NULL;
    g_ch2 = NULL;
    assert_select_counters_balanced();
    finish_runtime();

    /* TC-SEL-MEM-001 */
    reset_runtime();
    g_ch1 = gt_chan_create(sizeof(int), 0);
    CHECK(g_ch1 != NULL);
    CHECK(gt_go(task_select_recv_ch1, NULL) > 0);
    CHECK(gt_run() == GT_ERR_STATE);
    CHECK(gt_chan_try_send(g_ch1, &g_value1) == GT_OK);
    CHECK(gt_run() == GT_OK);
    assert_select_counters_balanced();
    CHECK(gt_chan_destroy(g_ch1) == GT_OK);
    g_ch1 = NULL;
    finish_runtime();

    /* TC-SEL-MEM-002 */
    reset_runtime();
    uint64_t one_ms = 1;
    g_ch1 = gt_chan_create(sizeof(int), 0);
    CHECK(g_ch1 != NULL);
    CHECK(gt_go(task_select_recv_ch1_timeout, &one_ms) > 0);
    CHECK(gt_run() == GT_OK);
    assert_select_counters_balanced();
    CHECK(gt_chan_destroy(g_ch1) == GT_OK);
    g_ch1 = NULL;
    finish_runtime();

    /* TC-SEL-MEM-003 */
    reset_runtime();
    g_ch1 = gt_chan_create(sizeof(int), 0);
    CHECK(g_ch1 != NULL);
    CHECK(gt_go(task_select_recv_ch1, NULL) > 0);
    CHECK(gt_run() == GT_ERR_STATE);
    CHECK(gt_chan_close(g_ch1) == GT_OK);
    CHECK(gt_run() == GT_OK);
    CHECK(g_rc1 == GT_ERR_CLOSED);
    assert_select_counters_balanced();
    CHECK(gt_chan_destroy(g_ch1) == GT_OK);
    g_ch1 = NULL;
    finish_runtime();

    /* TC-SEL-MEM-003 */
    reset_runtime();
    g_ch1 = gt_chan_create(sizeof(int), 0);
    CHECK(g_ch1 != NULL);
    CHECK(gt_go(task_select_recv_ch1, NULL) > 0);
    CHECK(gt_run() == GT_ERR_STATE);
    CHECK(gt_chan_destroy(g_ch1) == GT_OK);
    g_ch1 = NULL;
    CHECK(gt_run() == GT_OK);
    assert_select_counters_balanced();
    finish_runtime();

    /* TC-SEL-MEM-004 */
    reset_runtime();
    g_ch1 = gt_chan_create(sizeof(int), 0);
    CHECK(g_ch1 != NULL);
    CHECK(gt_go(task_select_recv_ch1, NULL) > 0);
    CHECK(gt_run() == GT_ERR_STATE);
    finish_runtime();
    reset_runtime();
    assert_select_counters_balanced();
    finish_runtime();
}

static void test_select_stress_matrix(void) {
    /* TC-SEL-STRESS-001 */
    reset_runtime();
    g_ch1 = gt_chan_create(sizeof(int), 1);
    CHECK(g_ch1 != NULL);
    for (int i = 0; i < 1000; ++i) {
        int value = i;
        int out = -1;
        size_t index = 9999;
        CHECK(gt_chan_send(g_ch1, &value) == GT_OK);
        gt_select_case_t cases[] = {
            { .op = GT_SELECT_RECV, .ch = g_ch1, .value = &out, .timeout_ms = 0 }
        };
        CHECK(gt_select(cases, ARRAY_LEN(cases), &index) == GT_OK);
        CHECK(index == 0);
        CHECK(out == value);
    }
    CHECK(gt_chan_destroy(g_ch1) == GT_OK);
    g_ch1 = NULL;
    finish_runtime();

    /* TC-SEL-STRESS-002 */
    reset_runtime();
    g_ch1 = gt_chan_create(sizeof(int), 0);
    CHECK(g_ch1 != NULL);
    for (int i = 0; i < 1000; ++i) {
        g_value2 = i;
        CHECK(gt_go(task_select_recv_ch1, NULL) > 0);
        CHECK(gt_run() == GT_ERR_STATE);
        CHECK(gt_chan_try_send(g_ch1, &g_value2) == GT_OK);
        CHECK(gt_run() == GT_OK);
        CHECK(g_out1 == i);
        g_out1 = -1111;
    }
    CHECK(gt_chan_destroy(g_ch1) == GT_OK);
    g_ch1 = NULL;
    finish_runtime();

    /* TC-SEL-STRESS-003 */
    reset_runtime();
    gt_chan_t *channels[STRESS_MANY_CHANNELS];
    select_many_arg_t args[100];
    for (int i = 0; i < STRESS_MANY_CHANNELS; ++i) {
        channels[i] = gt_chan_create(sizeof(int), 100);
        CHECK(channels[i] != NULL);
    }
    for (int task = 0; task < 100; ++task) {
        int value = task;
        CHECK(gt_chan_send(channels[task % STRESS_MANY_CHANNELS], &value) == GT_OK);
    }
    for (int task = 0; task < 100; ++task) {
        args[task].channels = channels;
        args[task].rc = 12345;
        args[task].index = 9999;
        args[task].out = -1;
        CHECK(gt_go(task_select_many_channels, &args[task]) > 0);
    }
    CHECK(gt_run() == GT_OK);
    CHECK(g_counter == 100);
    for (int task = 0; task < 100; ++task) {
        CHECK(args[task].rc == GT_OK);
        CHECK(args[task].index < STRESS_MANY_CHANNELS);
        CHECK(args[task].out >= 0);
    }
    for (int i = 0; i < STRESS_MANY_CHANNELS; ++i) {
        CHECK(gt_chan_destroy(channels[i]) == GT_OK);
    }
    finish_runtime();

    /* TC-SEL-STRESS-004 */
    reset_runtime();
    uint64_t one_ms = 1;
    g_ch1 = gt_chan_create(sizeof(int), 0);
    g_ch2 = gt_chan_create(sizeof(int), 2);
    CHECK(g_ch1 != NULL);
    CHECK(g_ch2 != NULL);
    g_handle1 = gt_go_handle(task_join_target, NULL);
    CHECK(g_handle1 != NULL);
    CHECK(gt_go(task_join_waiter, g_handle1) > 0);
    CHECK(gt_go(task_sleep_marker, &one_ms) > 0);
    CHECK(gt_go(task_select_recv_ch1_timeout, &one_ms) > 0);
    CHECK(gt_go(task_yield_then_try_send_ch1, NULL) > 0);
    CHECK(gt_chan_try_send(g_ch2, &g_value3) == GT_OK);
    CHECK(gt_run() == GT_OK);
    CHECK(g_rc1 == GT_OK);
    CHECK(g_index1 == 0 || g_index1 == 1);
    CHECK(g_rc3 == GT_OK);
    CHECK(g_marker1 >= 2);
    CHECK(gt_chan_try_recv(g_ch2, &g_out3) == GT_OK);
    CHECK(g_out3 == g_value3);
    gt_task_handle_release(g_handle1);
    g_handle1 = NULL;
    CHECK(gt_chan_destroy(g_ch1) == GT_OK);
    CHECK(gt_chan_destroy(g_ch2) == GT_OK);
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
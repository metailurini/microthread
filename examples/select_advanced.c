#include "gt.h"

#include <stdio.h>

static gt_chan_t *g_data;
static gt_chan_t *g_signal;

static const char *rc_name(int rc) {
    switch (rc) {
    case GT_OK: return "GT_OK";
    case GT_ERR_CLOSED: return "GT_ERR_CLOSED";
    case GT_ERR_WOULD_BLOCK: return "GT_ERR_WOULD_BLOCK";
    default: return "other error";
    }
}

static void producer(void *arg) {
    (void)arg;
    for (int i = 1; i <= 3; ++i) {
        gt_sleep_ms(5);
        int value = i * 10;
        printf("producer send %d\n", value);
        (void)gt_chan_send(g_data, &value);
    }
    gt_chan_close(g_data);
}

static void consumer(void *arg) {
    (void)arg;
    for (;;) {
        int value = 0;
        size_t selected = 9999;
        gt_select_case_t cases[] = {
            { .op = GT_SELECT_RECV, .ch = g_data, .value = &value, .timeout_ms = 0 },
            { .op = GT_SELECT_TIMEOUT, .ch = NULL, .value = NULL, .timeout_ms = 20 }
        };

        int rc = gt_select(cases, 2, &selected);
        if (rc == GT_OK && selected == 0) {
            printf("consumer recv %d\n", value);
        } else if (rc == GT_OK && selected == 1) {
            printf("consumer timeout waiting for data\n");
        } else if (rc == GT_ERR_CLOSED && selected == 0) {
            printf("consumer saw data channel close\n");
            return;
        } else {
            printf("consumer select failed: %s\n", rc_name(rc));
            return;
        }
    }
}

static int demo_default_and_send_select(void) {
    int value = 123;
    int out = 0;
    size_t selected = 9999;

    gt_select_case_t recv_or_default[] = {
        { .op = GT_SELECT_RECV, .ch = g_data, .value = &out, .timeout_ms = 0 },
        { .op = GT_SELECT_DEFAULT, .ch = NULL, .value = NULL, .timeout_ms = 0 }
    };
    int rc = gt_select(recv_or_default, 2, &selected);
    if (rc != GT_OK || selected != 1) {
        return 0;
    }
    printf("default selected because data is not ready\n");

    gt_select_case_t send_or_default[] = {
        { .op = GT_SELECT_SEND, .ch = g_signal, .value = &value, .timeout_ms = 0 },
        { .op = GT_SELECT_DEFAULT, .ch = NULL, .value = NULL, .timeout_ms = 0 }
    };
    rc = gt_select(send_or_default, 2, &selected);
    if (rc != GT_OK || selected != 0) {
        return 0;
    }
    printf("send case selected while signal buffer had space\n");

    value = 456;
    rc = gt_select(send_or_default, 2, &selected);
    if (rc != GT_OK || selected != 1) {
        return 0;
    }
    printf("default selected because signal buffer is full\n");

    return gt_chan_try_recv(g_signal, &out) == GT_OK && out == 123;
}

int main(void) {
    if (gt_init() != GT_OK) {
        return 1;
    }

    g_data = gt_chan_create(sizeof(int), 0);
    g_signal = gt_chan_create(sizeof(int), 1);
    if (!g_data || !g_signal) {
        gt_shutdown();
        return 1;
    }

    if (!demo_default_and_send_select()) {
        gt_shutdown();
        return 1;
    }

    gt_go(consumer, NULL);
    gt_go(producer, NULL);

    int rc = gt_run();
    int data_destroy_rc = gt_chan_destroy(g_data);
    int signal_destroy_rc = gt_chan_destroy(g_signal);
    gt_shutdown();

    return rc == GT_OK && data_destroy_rc == GT_OK && signal_destroy_rc == GT_OK ? 0 : 1;
}

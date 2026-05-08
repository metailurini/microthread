#include "microthread.h"

#include <stdio.h>

static mt_chan_t *g_data;
static mt_chan_t *g_signal;

static const char *rc_name(int rc) {
    switch (rc) {
    case MT_OK: return "MT_OK";
    case MT_ERR_CLOSED: return "MT_ERR_CLOSED";
    case MT_ERR_WOULD_BLOCK: return "MT_ERR_WOULD_BLOCK";
    default: return "other error";
    }
}

static void producer(void *arg) {
    (void)arg;
    for (int i = 1; i <= 3; ++i) {
        mt_sleep_ms(5);
        int value = i * 10;
        printf("producer send %d\n", value);
        (void)mt_chan_send(g_data, &value);
    }
    mt_chan_close(g_data);
}

static void consumer(void *arg) {
    (void)arg;
    for (;;) {
        int value = 0;
        size_t selected = 9999;
        mt_select_case_t cases[] = {
            { .op = MT_SELECT_RECV, .ch = g_data, .value = &value, .timeout_ms = 0 },
            { .op = MT_SELECT_TIMEOUT, .ch = NULL, .value = NULL, .timeout_ms = 20 }
        };

        int rc = mt_select(cases, 2, &selected);
        if (rc == MT_OK && selected == 0) {
            printf("consumer recv %d\n", value);
        } else if (rc == MT_OK && selected == 1) {
            printf("consumer timeout waiting for data\n");
        } else if (rc == MT_ERR_CLOSED && selected == 0) {
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

    mt_select_case_t recv_or_default[] = {
        { .op = MT_SELECT_RECV, .ch = g_data, .value = &out, .timeout_ms = 0 },
        { .op = MT_SELECT_DEFAULT, .ch = NULL, .value = NULL, .timeout_ms = 0 }
    };
    int rc = mt_select(recv_or_default, 2, &selected);
    if (rc != MT_OK || selected != 1) {
        return 0;
    }
    printf("default selected because data is not ready\n");

    mt_select_case_t send_or_default[] = {
        { .op = MT_SELECT_SEND, .ch = g_signal, .value = &value, .timeout_ms = 0 },
        { .op = MT_SELECT_DEFAULT, .ch = NULL, .value = NULL, .timeout_ms = 0 }
    };
    rc = mt_select(send_or_default, 2, &selected);
    if (rc != MT_OK || selected != 0) {
        return 0;
    }
    printf("send case selected while signal buffer had space\n");

    value = 456;
    rc = mt_select(send_or_default, 2, &selected);
    if (rc != MT_OK || selected != 1) {
        return 0;
    }
    printf("default selected because signal buffer is full\n");

    return mt_chan_try_recv(g_signal, &out) == MT_OK && out == 123;
}

int main(void) {
    if (mt_init() != MT_OK) {
        return 1;
    }

    g_data = mt_chan_create(sizeof(int), 0);
    g_signal = mt_chan_create(sizeof(int), 1);
    if (!g_data || !g_signal) {
        mt_shutdown();
        return 1;
    }

    if (!demo_default_and_send_select()) {
        mt_shutdown();
        return 1;
    }

    mt_go(consumer, NULL);
    mt_go(producer, NULL);

    int rc = mt_run();
    int data_destroy_rc = mt_chan_destroy(g_data);
    int signal_destroy_rc = mt_chan_destroy(g_signal);
    mt_shutdown();

    return rc == MT_OK && data_destroy_rc == MT_OK && signal_destroy_rc == MT_OK ? 0 : 1;
}

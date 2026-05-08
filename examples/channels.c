#include "microthread.h"

#include <stdio.h>

static mt_chan_t *g_ch;

static void producer(void *arg) {
    (void)arg;
    for (int i = 1; i <= 5; ++i) {
        printf("send %d\n", i);
        if (mt_chan_send(g_ch, &i) != MT_OK) {
            printf("send failed\n");
            return;
        }
    }
    mt_chan_close(g_ch);
}

static void consumer(void *arg) {
    (void)arg;
    int value = 0;
    while (mt_chan_recv(g_ch, &value) == MT_OK) {
        printf("recv %d\n", value);
        mt_yield();
    }
    printf("channel closed\n");
}

int main(void) {
    if (mt_init() != MT_OK) {
        return 1;
    }

    g_ch = mt_chan_create(sizeof(int), 2);
    if (!g_ch) {
        return 1;
    }

    mt_go(producer, NULL);
    mt_go(consumer, NULL);

    int rc = mt_run();
    int destroy_rc = mt_chan_destroy(g_ch);
    mt_shutdown();

    return rc == MT_OK && destroy_rc == MT_OK ? 0 : 1;
}

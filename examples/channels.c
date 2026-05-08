#include "gt.h"

#include <stdio.h>

static gt_chan_t *g_ch;

static void producer(void *arg) {
    (void)arg;
    for (int i = 1; i <= 5; ++i) {
        printf("send %d\n", i);
        if (gt_chan_send(g_ch, &i) != GT_OK) {
            printf("send failed\n");
            return;
        }
    }
    gt_chan_close(g_ch);
}

static void consumer(void *arg) {
    (void)arg;
    int value = 0;
    while (gt_chan_recv(g_ch, &value) == GT_OK) {
        printf("recv %d\n", value);
        gt_yield();
    }
    printf("channel closed\n");
}

int main(void) {
    if (gt_init() != GT_OK) {
        return 1;
    }

    g_ch = gt_chan_create(sizeof(int), 2);
    if (!g_ch) {
        return 1;
    }

    gt_go(producer, NULL);
    gt_go(consumer, NULL);

    int rc = gt_run();
    int destroy_rc = gt_chan_destroy(g_ch);
    gt_shutdown();

    return rc == GT_OK && destroy_rc == GT_OK ? 0 : 1;
}

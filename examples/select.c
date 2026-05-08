#include "gt.h"

#include <stdio.h>

static void producer(void *arg) {
    gt_chan_t *ch = (gt_chan_t *)arg;
    int value = 42;

    gt_sleep_ms(10);
    (void)gt_chan_send(ch, &value);
}

static void consumer(void *arg) {
    gt_chan_t *ch = (gt_chan_t *)arg;
    int value = 0;
    size_t selected = 0;
    gt_select_case_t cases[2];

    cases[0].op = GT_SELECT_RECV;
    cases[0].ch = ch;
    cases[0].value = &value;
    cases[0].timeout_ms = 0;

    cases[1].op = GT_SELECT_TIMEOUT;
    cases[1].ch = NULL;
    cases[1].value = NULL;
    cases[1].timeout_ms = 100;

    int rc = gt_select(cases, 2, &selected);
    if (rc == GT_OK && selected == 0) {
        printf("select received %d\n", value);
    } else if (rc == GT_OK && selected == 1) {
        printf("select timed out\n");
    } else {
        printf("select failed: %d\n", rc);
    }
}

int main(void) {
    gt_init();

    gt_chan_t *ch = gt_chan_create(sizeof(int), 0);
    if (!ch) {
        return 1;
    }

    gt_go(consumer, ch);
    gt_go(producer, ch);

    int rc = gt_run();
    gt_chan_destroy(ch);
    gt_shutdown();

    return rc == GT_OK ? 0 : 1;
}

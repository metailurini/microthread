#include "microthread.h"

#include <stdio.h>

static void producer(void *arg) {
    mt_chan_t *ch = (mt_chan_t *)arg;
    int value = 42;

    mt_sleep_ms(10);
    (void)mt_chan_send(ch, &value);
}

static void consumer(void *arg) {
    mt_chan_t *ch = (mt_chan_t *)arg;
    int value = 0;
    size_t selected = 0;
    mt_select_case_t cases[2];

    cases[0].op = MT_SELECT_RECV;
    cases[0].ch = ch;
    cases[0].value = &value;
    cases[0].timeout_ms = 0;

    cases[1].op = MT_SELECT_TIMEOUT;
    cases[1].ch = NULL;
    cases[1].value = NULL;
    cases[1].timeout_ms = 100;

    int rc = mt_select(cases, 2, &selected);
    if (rc == MT_OK && selected == 0) {
        printf("select received %d\n", value);
    } else if (rc == MT_OK && selected == 1) {
        printf("select timed out\n");
    } else {
        printf("select failed: %d\n", rc);
    }
}

int main(void) {
    mt_init();

    mt_chan_t *ch = mt_chan_create(sizeof(int), 0);
    if (!ch) {
        return 1;
    }

    mt_go(consumer, ch);
    mt_go(producer, ch);

    int rc = mt_run();
    mt_chan_destroy(ch);
    mt_shutdown();

    return rc == MT_OK ? 0 : 1;
}

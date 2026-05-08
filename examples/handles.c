#include "microthread.h"

#include <stdio.h>

static void child(void *arg) {
    int *value = (int *)arg;
    for (int i = 0; i < 3; ++i) {
        (*value)++;
        mt_yield();
    }
}

static void parent(void *arg) {
    int *value = (int *)arg;
    mt_task_handle_t *h = mt_go_handle(child, value);
    if (!h) {
        printf("failed to create child\n");
        return;
    }

    int rc = mt_join(h);
    printf("child joined rc=%d value=%d\n", rc, *value);
    mt_task_handle_release(h);
}

static void cancellable(void *arg) {
    (void)arg;
    while (!mt_task_cancelled()) {
        mt_yield();
    }
    printf("cancel observed\n");
}

static void canceller(void *arg) {
    mt_task_handle_t *h = (mt_task_handle_t *)arg;
    mt_yield();
    mt_task_cancel(h);
}

int main(void) {
    mt_init();

    int value = 0;
    mt_go(parent, &value);

    mt_task_handle_t *h = mt_go_handle(cancellable, NULL);
    mt_go(canceller, h);

    mt_run();

    mt_task_status_t status;
    if (mt_task_status(h, &status) == MT_OK) {
        printf("cancelled task status=%d\n", (int)status);
    }
    mt_task_handle_release(h);

    mt_shutdown();
    return 0;
}

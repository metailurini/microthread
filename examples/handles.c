#include "gt.h"

#include <stdio.h>

static void child(void *arg) {
    int *value = (int *)arg;
    for (int i = 0; i < 3; ++i) {
        (*value)++;
        gt_yield();
    }
}

static void parent(void *arg) {
    int *value = (int *)arg;
    gt_task_handle_t *h = gt_go_handle(child, value);
    if (!h) {
        printf("failed to create child\n");
        return;
    }

    int rc = gt_join(h);
    printf("child joined rc=%d value=%d\n", rc, *value);
    gt_task_handle_release(h);
}

static void cancellable(void *arg) {
    (void)arg;
    while (!gt_task_cancelled()) {
        gt_yield();
    }
    printf("cancel observed\n");
}

static void canceller(void *arg) {
    gt_task_handle_t *h = (gt_task_handle_t *)arg;
    gt_yield();
    gt_task_cancel(h);
}

int main(void) {
    gt_init();

    int value = 0;
    gt_go(parent, &value);

    gt_task_handle_t *h = gt_go_handle(cancellable, NULL);
    gt_go(canceller, h);

    gt_run();

    gt_task_status_t status;
    if (gt_task_status(h, &status) == GT_OK) {
        printf("cancelled task status=%d\n", (int)status);
    }
    gt_task_handle_release(h);

    gt_shutdown();
    return 0;
}

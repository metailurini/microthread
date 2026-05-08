#include "gt.h"

#include <stdio.h>

static void worker(void *arg) {
    const char *name = (const char *)arg;
    for (int i = 0; i < 3; ++i) {
        printf("%s: step %d\n", name, i);
        gt_yield();
    }
}

int main(void) {
    if (gt_init() != GT_OK) {
        fprintf(stderr, "gt_init failed\n");
        return 1;
    }

    gt_go(worker, "green-A");
    gt_go(worker, "green-B");

    int rc = gt_run();
    gt_shutdown();
    return rc == GT_OK ? 0 : 1;
}
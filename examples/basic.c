#include "microthread.h"

#include <stdio.h>

static void worker(void *arg) {
    const char *name = (const char *)arg;
    for (int i = 0; i < 3; ++i) {
        printf("%s: step %d\n", name, i);
        mt_yield();
    }
}

int main(void) {
    if (mt_init() != MT_OK) {
        fprintf(stderr, "mt_init failed\n");
        return 1;
    }

    mt_go(worker, "green-A");
    mt_go(worker, "green-B");

    int rc = mt_run();
    mt_shutdown();
    return rc == MT_OK ? 0 : 1;
}
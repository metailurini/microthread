#include "gt.h"

#include <stdio.h>

static void sleeper(void *arg) {
    const char *name = (const char *)arg;
    printf("%s: before sleep\n", name);
    gt_sleep_ms(25);
    printf("%s: after sleep\n", name);
}

static void yielder(void *arg) {
    const char *name = (const char *)arg;
    for (int i = 0; i < 3; ++i) {
        printf("%s: step %d\n", name, i);
        gt_yield();
    }
}

int main(void) {
    if (gt_init() != GT_OK) {
        return 1;
    }

    if (gt_go_with_stack(sleeper, "sleep-A", 128u * 1024u) < 0) {
        return 1;
    }
    if (gt_go(yielder, "yield-B") < 0) {
        return 1;
    }

    if (gt_run() != GT_OK) {
        return 1;
    }

    gt_shutdown();
    return 0;
}

#include "gt.h"

#include <stdio.h>

static const char *rc_name(int rc) {
    switch (rc) {
    case GT_OK: return "GT_OK";
    case GT_ERR_WOULD_BLOCK: return "GT_ERR_WOULD_BLOCK";
    case GT_ERR_CLOSED: return "GT_ERR_CLOSED";
    default: return "other error";
    }
}

int main(void) {
    if (gt_init() != GT_OK) {
        fprintf(stderr, "gt_init failed\n");
        return 1;
    }

    gt_chan_t *ch = gt_chan_create(sizeof(int), 1);
    if (!ch) {
        gt_shutdown();
        return 1;
    }

    int value = 0;
    int rc = gt_chan_try_recv(ch, &value);
    printf("try_recv empty: %s\n", rc_name(rc));

    value = 7;
    rc = gt_chan_try_send(ch, &value);
    printf("try_send 7: %s len=%zu\n", rc_name(rc), gt_chan_len(ch));

    value = 8;
    rc = gt_chan_try_send(ch, &value);
    printf("try_send full: %s len=%zu\n", rc_name(rc), gt_chan_len(ch));

    value = 0;
    rc = gt_chan_try_recv(ch, &value);
    printf("try_recv value: %s value=%d len=%zu\n", rc_name(rc), value, gt_chan_len(ch));

    gt_chan_close(ch);
    rc = gt_chan_try_recv(ch, &value);
    printf("try_recv closed+empty: %s\n", rc_name(rc));

    int destroy_rc = gt_chan_destroy(ch);
    gt_shutdown();
    return destroy_rc == GT_OK ? 0 : 1;
}

#include "microthread.h"

#include <stdio.h>

static const char *rc_name(int rc) {
    switch (rc) {
    case MT_OK: return "MT_OK";
    case MT_ERR_WOULD_BLOCK: return "MT_ERR_WOULD_BLOCK";
    case MT_ERR_CLOSED: return "MT_ERR_CLOSED";
    default: return "other error";
    }
}

int main(void) {
    if (mt_init() != MT_OK) {
        fprintf(stderr, "mt_init failed\n");
        return 1;
    }

    mt_chan_t *ch = mt_chan_create(sizeof(int), 1);
    if (!ch) {
        mt_shutdown();
        return 1;
    }

    int value = 0;
    int rc = mt_chan_try_recv(ch, &value);
    printf("try_recv empty: %s\n", rc_name(rc));

    value = 7;
    rc = mt_chan_try_send(ch, &value);
    printf("try_send 7: %s len=%zu\n", rc_name(rc), mt_chan_len(ch));

    value = 8;
    rc = mt_chan_try_send(ch, &value);
    printf("try_send full: %s len=%zu\n", rc_name(rc), mt_chan_len(ch));

    value = 0;
    rc = mt_chan_try_recv(ch, &value);
    printf("try_recv value: %s value=%d len=%zu\n", rc_name(rc), value, mt_chan_len(ch));

    mt_chan_close(ch);
    rc = mt_chan_try_recv(ch, &value);
    printf("try_recv closed+empty: %s\n", rc_name(rc));

    int destroy_rc = mt_chan_destroy(ch);
    mt_shutdown();
    return destroy_rc == MT_OK ? 0 : 1;
}

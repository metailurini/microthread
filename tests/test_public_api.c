#include "microthread.h"
#include "microthread_debug.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

#if !defined(_WIN32)
#include <sys/socket.h>
#include <unistd.h>
#endif

static int g_ran = 0;

static void simple_task(void *arg) {
    (void)arg;
    g_ran = 1;
}

int main(void) {
    mt_options_t bad = {0};
    bad.stack_size = MT_MIN_STACK_SIZE - 1;
    assert(mt_init_with_options(&bad) == MT_ERR_INVALID);

    mt_options_t opts = {0};
    opts.stack_size = MT_DEFAULT_STACK_SIZE;
    assert(mt_init_with_options(&opts) == MT_OK);
    assert(strcmp(mt_strerror(MT_OK), "ok") == 0);
    assert(mt_strerror(MT_ERR_TIMEOUT) != NULL);
    assert(mt_debug_live_task_count() == 0);

    assert(mt_go(simple_task, NULL) > 0);
    assert(mt_run() == MT_OK);
    assert(g_ran == 1);
    mt_shutdown();

#if !defined(_WIN32)
    int sv[2];
    assert(socketpair(AF_UNIX, SOCK_STREAM, 0, sv) == 0);
    assert(mt_init() == MT_OK);
    assert(mt_fd_adopt(sv[0]) == MT_OK);
    assert(mt_fd_release(sv[0]) == MT_OK);
    assert(mt_fd_close(sv[1]) == MT_OK);
    assert(close(sv[0]) == 0);
    mt_shutdown();
#endif

    puts("public API compatibility test passed");
    return 0;
}

#include "microthread.h"

#include <assert.h>
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>

#if !defined(_WIN32)
#include <sys/socket.h>
#include <unistd.h>
#endif

#if !defined(_WIN32)
static int g_sv[2] = {-1, -1};
static atomic_int g_read_ok;
static atomic_int g_write_ok;

static void public_reader(void *arg) {
    (void)arg;
    char buf[6] = {0};
    ssize_t n = mt_fd_read(g_sv[0], buf, 5, 1000);
    atomic_store(&g_read_ok, n == 5 && memcmp(buf, "hello", 5) == 0);
}

static void public_writer(void *arg) {
    (void)arg;
    ssize_t n = mt_net_write(g_sv[1], "hello", 5, 1000);
    atomic_store(&g_write_ok, n == 5);
}
#endif

int main(void) {
    assert(strcmp(mt_strerror(MT_ERR_IO), "i/o error") == 0);
    assert(strcmp(mt_strerror(MT_ERR_BACKEND), "i/o backend error") == 0);
    assert(strcmp(mt_task_status_name(MT_TASK_STATUS_WAITING_FD), "waiting_fd") == 0);

#if !defined(_WIN32)
    assert(socketpair(AF_UNIX, SOCK_STREAM, 0, g_sv) == 0);
    assert(mt_init() == MT_OK);
    assert(mt_fd_adopt(g_sv[0]) == MT_OK);
    assert(mt_fd_adopt(g_sv[1]) == MT_OK);
    assert(mt_go(public_reader, NULL) > 0);
    assert(mt_go(public_writer, NULL) > 0);
    assert(mt_runtime_start(2) == MT_OK);
    assert(atomic_load(&g_read_ok) == 1);
    assert(atomic_load(&g_write_ok) == 1);
    assert(mt_fd_release(g_sv[0]) == MT_OK);
    assert(mt_fd_close(g_sv[0]) == MT_OK);
    assert(mt_net_close(g_sv[1]) == MT_OK);
    mt_shutdown();

    assert(mt_init() == MT_OK);
    int bad = mt_net_listen_tcp("invalid.invalid.invalid", "80", 1);
    assert(bad == MT_ERR_ADDRINFO || bad == MT_ERR_IO);
    assert(mt_last_os_error() != 0);
    mt_shutdown();
#endif

    puts("public I/O API test passed");
    return 0;
}

#ifndef MT_TESTING
#define MT_TESTING
#endif
#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif

#include "microthread.h"
#include "microthread_testing.h"
#include "microthread_debug.h"
#include "test_helpers.h"

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <sched.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#if !defined(_WIN32)
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
#endif


#if !defined(_WIN32)
#define ARRAY_LEN(a) (sizeof(a) / sizeof((a)[0]))

enum {
    WORKERS_1 = 1,
    WORKERS_2 = 2,
    WORKERS_4 = 4,
    SHORT_TIMEOUT_MS = 20,
    LONG_TIMEOUT_MS = 1000,
#if defined(MT_IO_STRESS)
    STRESS_PAIRS = 128,
    TCP_CLIENTS = 64,
    ECHO_CLIENTS = 32,
    LARGE_BYTES = 1024 * 1024
#else
    STRESS_PAIRS = 24,
    TCP_CLIENTS = 16,
    ECHO_CLIENTS = 8,
    LARGE_BYTES = 128 * 1024
#endif
};

static atomic_int g_fd0 = ATOMIC_VAR_INIT(-1);
static int g_fd1 = -1;
static int g_listen_fd = -1;
static mt_task_handle_t *g_handle = NULL;
static mt_chan_t *g_ch = NULL;
static atomic_int g_rc;
static atomic_int g_rc2;
static atomic_int g_ready;
static atomic_int g_ready2;
static atomic_int g_value;
static atomic_int g_counter;
static atomic_int g_counter2;
static atomic_int g_started;
static atomic_int g_done;
static atomic_int g_stop;
static atomic_long g_bytes;
static atomic_int g_listener_port;
static int g_old_fd = -1;

static uint64_t monotonic_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * UINT64_C(1000) + (uint64_t)ts.tv_nsec / UINT64_C(1000000);
}

static void close_if_open(int *fd) {
    if (*fd >= 0) {
        close(*fd);
        *fd = -1;
    }
}

static int load_fd0(void) {
    return atomic_load(&g_fd0);
}

static void store_fd0(int fd) {
    atomic_store(&g_fd0, fd);
}

static void close_fd0_if_open(void) {
    int fd = atomic_exchange(&g_fd0, -1);
    if (fd >= 0) {
        close(fd);
    }
}

static void reset_runtime(void) {
    if (g_handle) {
        mt_task_handle_release(g_handle);
        g_handle = NULL;
    }
    if (g_ch) {
        (void)mt_chan_destroy(g_ch);
        g_ch = NULL;
    }
    close_fd0_if_open();
    close_if_open(&g_fd1);
    close_if_open(&g_listen_fd);
    mt_shutdown();
    mt_test_reset_faults();
    CHECK(mt_init() == MT_OK);
    atomic_store(&g_rc, 12345);
    atomic_store(&g_rc2, 12345);
    atomic_store(&g_ready, 0);
    atomic_store(&g_ready2, 0);
    atomic_store(&g_value, 0);
    atomic_store(&g_counter, 0);
    atomic_store(&g_counter2, 0);
    atomic_store(&g_started, 0);
    atomic_store(&g_done, 0);
    atomic_store(&g_stop, 0);
    atomic_store(&g_bytes, 0);
    atomic_store(&g_listener_port, 0);
    g_old_fd = -1;
}

static void assert_fd_nonblocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    CHECK(flags >= 0);
    CHECK((flags & O_NONBLOCK) != 0);
}

static void finish_runtime(void) {
    if (g_handle) {
        mt_task_handle_release(g_handle);
        g_handle = NULL;
    }
    if (g_ch) {
        CHECK(mt_chan_destroy(g_ch) == MT_OK);
        g_ch = NULL;
    }
    close_fd0_if_open();
    close_if_open(&g_fd1);
    close_if_open(&g_listen_fd);
    mt_shutdown();
    mt_test_reset_faults();
}

static void assert_core_counters_balanced(void) {
    size_t task_allocs = 0, task_frees = 0;
    size_t stack_allocs = 0, stack_frees = 0;
    size_t timer_allocs = 0, timer_frees = 0;
    mt_test_memory_counters(&task_allocs, &task_frees,
                            &stack_allocs, &stack_frees,
                            &timer_allocs, &timer_frees);
    CHECK(task_allocs == task_frees);
    CHECK(stack_allocs == stack_frees);
    CHECK(timer_allocs == timer_frees);
}

static void assert_io_counters_balanced(void) {
    size_t fd_allocs = 0, fd_frees = 0;
    size_t backend_inits = 0, backend_shutdowns = 0;
    size_t backend_registers = 0, backend_unregisters = 0;
    mt_test_io_memory_counters(&fd_allocs, &fd_frees,
                               &backend_inits, &backend_shutdowns,
                               &backend_registers, &backend_unregisters);
    CHECK(fd_allocs == fd_frees);
    if (backend_inits != backend_shutdowns || backend_registers != backend_unregisters) {
        fprintf(stderr,
                "io counters: fd=%zu/%zu backend=%zu/%zu reg=%zu/%zu\n",
                fd_allocs, fd_frees, backend_inits, backend_shutdowns,
                backend_registers, backend_unregisters);
    }
    CHECK(backend_inits == backend_shutdowns);
    CHECK(backend_registers == backend_unregisters);
}

static void make_socketpair(void) {
    int fds[2] = { -1, -1 };
    CHECK(socketpair(AF_UNIX, SOCK_STREAM, 0, fds) == 0);
    store_fd0(fds[0]);
    g_fd1 = fds[1];
    CHECK(mt_fd_set_nonblocking(load_fd0()) == MT_OK);
    CHECK(mt_fd_set_nonblocking(g_fd1) == MT_OK);
}

static void make_blocking_socketpair(void) {
    int fds[2] = { -1, -1 };
    CHECK(socketpair(AF_UNIX, SOCK_STREAM, 0, fds) == 0);
    store_fd0(fds[0]);
    g_fd1 = fds[1];
}

static void get_listener_port(int fd) {
    struct sockaddr_in sin;
    socklen_t len = sizeof(sin);
    CHECK(getsockname(fd, (struct sockaddr *)&sin, &len) == 0);
    atomic_store(&g_listener_port, (int)ntohs(sin.sin_port));
}

static int count_open_fds(void) {
    long max_fd = sysconf(_SC_OPEN_MAX);
    if (max_fd < 0 || max_fd > 4096) {
        max_fd = 4096;
    }
    int count = 0;
    for (int fd = 0; fd < (int)max_fd; ++fd) {
        errno = 0;
        if (fcntl(fd, F_GETFD) >= 0 || errno != EBADF) {
            count++;
        }
    }
    return count;
}

static void require_file_contains(const char *path, const char *needle) {
    FILE *fp = fopen(path, "rb");
    CHECK(fp != NULL);
    CHECK(fseek(fp, 0, SEEK_END) == 0);
    long len = ftell(fp);
    CHECK(len >= 0);
    CHECK(fseek(fp, 0, SEEK_SET) == 0);
    char *buf = (char *)malloc((size_t)len + 1u);
    CHECK(buf != NULL);
    size_t n = fread(buf, 1, (size_t)len, fp);
    CHECK(n == (size_t)len);
    buf[n] = '\0';
    CHECK(strstr(buf, needle) != NULL);
    free(buf);
    fclose(fp);
}

static int connect_loopback_port(int port) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    CHECK(fd >= 0);
    struct sockaddr_in sin;
    memset(&sin, 0, sizeof(sin));
    sin.sin_family = AF_INET;
    sin.sin_port = htons((uint16_t)port);
    CHECK(inet_pton(AF_INET, "127.0.0.1", &sin.sin_addr) == 1);
    int rc;
    do {
        rc = connect(fd, (struct sockaddr *)&sin, sizeof(sin));
    } while (rc != 0 && errno == EINTR);
    CHECK(rc == 0);
    return fd;
}

static void fill_send_buffer(int fd) {
    char buf[4096];
    memset(buf, 'x', sizeof(buf));
    for (;;) {
        ssize_t n = write(fd, buf, sizeof(buf));
        if (n > 0) {
            continue;
        }
        if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            return;
        }
        if (n < 0 && errno == EINTR) {
            continue;
        }
        CHECK(0 && "unexpected fill_send_buffer result");
    }
}

static void drain_some(int fd) {
    char buf[8192];
    int drained = 0;
    for (;;) {
        ssize_t n = read(fd, buf, sizeof(buf));
        if (n > 0) {
            drained = 1;
            continue;
        }
        if (n < 0 && errno == EINTR) {
            continue;
        }
        if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            if (drained) {
                return;
            }
            sched_yield();
            continue;
        }
        CHECK(0 && "unexpected drain result");
    }
}

static void task_wait_read(void *arg) {
    (void)arg;
    int ready = 0;
    atomic_store(&g_started, 1);
    int rc = mt_fd_wait(load_fd0(), MT_FD_READ, LONG_TIMEOUT_MS, &ready);
    atomic_store(&g_rc, rc);
    atomic_store(&g_ready, ready);
}

static void task_wait_read_rc2(void *arg) {
    (void)arg;
    int ready = 0;
    atomic_fetch_add(&g_started, 1);
    int rc = mt_fd_wait(load_fd0(), MT_FD_READ, LONG_TIMEOUT_MS, &ready);
    atomic_store(&g_rc2, rc);
    atomic_store(&g_ready2, ready);
}

static void task_wait_write(void *arg) {
    (void)arg;
    int ready = 0;
    atomic_store(&g_started, 1);
    int rc = mt_fd_wait(load_fd0(), MT_FD_WRITE, LONG_TIMEOUT_MS, &ready);
    atomic_store(&g_rc, rc);
    atomic_store(&g_ready, ready);
}

static void task_wait_read_write(void *arg) {
    (void)arg;
    int ready = 0;
    atomic_store(&g_started, 1);
    int rc = mt_fd_wait(load_fd0(), MT_FD_READ | MT_FD_WRITE, LONG_TIMEOUT_MS, &ready);
    atomic_store(&g_rc, rc);
    atomic_store(&g_ready, ready);
}

static void task_wait_read_write_timeout(void *arg) {
    (void)arg;
    int ready = 111;
    int rc = mt_fd_wait(load_fd0(), MT_FD_READ | MT_FD_WRITE, SHORT_TIMEOUT_MS, &ready);
    atomic_store(&g_rc, rc);
    atomic_store(&g_ready, ready);
}

static void task_write_peer_after_sleep(void *arg) {
    const char *msg = (const char *)arg;
    while (!atomic_load(&g_started)) {
        mt_yield();
    }
    mt_sleep_ms(5);
    ssize_t n;
    do {
        n = write(g_fd1, msg, strlen(msg));
    } while (n < 0 && errno == EINTR);
    CHECK(n == (ssize_t)strlen(msg));
}

static void task_write_peer_after_longer_sleep(void *arg) {
    const char *msg = (const char *)arg;
    while (!atomic_load(&g_started)) {
        mt_yield();
    }
    mt_sleep_ms(SHORT_TIMEOUT_MS + 20);
    ssize_t n;
    do {
        n = write(g_fd1, msg, strlen(msg));
    } while (n < 0 && errno == EINTR);
    if (n == (ssize_t)strlen(msg)) {
        atomic_fetch_add(&g_counter, 1);
    }
}

static void task_cancel_then_write_peer(void *arg) {
    mt_task_handle_t *handle = (mt_task_handle_t *)arg;
    while (!atomic_load(&g_started)) {
        mt_yield();
    }
    mt_sleep_ms(5);
    CHECK(mt_task_cancel(handle) == MT_OK);
    mt_sleep_ms(10);
    ssize_t n;
    do {
        n = write(g_fd1, "c", 1);
    } while (n < 0 && errno == EINTR);
    if (n == 1) {
        atomic_fetch_add(&g_counter, 1);
    }
}

static void task_close_waited_fd_after_sleep(void *arg) {
    (void)arg;
    while (!atomic_load(&g_started)) {
        mt_yield();
    }
    mt_sleep_ms(5);
    CHECK(mt_fd_close(load_fd0()) == MT_OK);
    store_fd0(-1);
}

static void task_drain_peer_after_sleep(void *arg) {
    (void)arg;
    while (!atomic_load(&g_started)) {
        mt_yield();
    }
    mt_sleep_ms(5);
    drain_some(g_fd1);
}

static void task_busy_counter(void *arg) {
    (void)arg;
    uint64_t until = monotonic_ms() + 30;
    while (monotonic_ms() < until) {
        atomic_fetch_add(&g_counter, 1);
        mt_yield();
    }
}

static void task_read_wrapper(void *arg) {
    (void)arg;
    char buf[16] = {0};
    atomic_store(&g_started, 1);
    ssize_t n = mt_fd_read(load_fd0(), buf, sizeof(buf), LONG_TIMEOUT_MS);
    atomic_store(&g_rc, (int)n);
    if (n > 0 && memcmp(buf, "hello", 5) == 0) {
        atomic_store(&g_value, 1);
    }
}

static void task_read_eof_wrapper(void *arg) {
    (void)arg;
    char buf[16];
    ssize_t n = mt_fd_read(load_fd0(), buf, sizeof(buf), LONG_TIMEOUT_MS);
    atomic_store(&g_rc, (int)n);
}

static void task_write_wrapper(void *arg) {
    const char *msg = (const char *)arg;
    atomic_store(&g_started, 1);
    ssize_t n = mt_fd_write(load_fd0(), msg, strlen(msg), LONG_TIMEOUT_MS);
    atomic_store(&g_rc, (int)n);
}

static void task_net_write_wrapper(void *arg) {
    const char *msg = (const char *)arg;
    ssize_t n = mt_net_write(load_fd0(), msg, strlen(msg), LONG_TIMEOUT_MS);
    atomic_store(&g_rc, (int)n);
}

static void task_wait_read_timeout(void *arg) {
    (void)arg;
    int ready = 111;
    atomic_store(&g_started, 1);
    int rc = mt_fd_wait(load_fd0(), MT_FD_READ, SHORT_TIMEOUT_MS, &ready);
    atomic_store(&g_rc, rc);
    atomic_store(&g_ready, ready);
}

static void task_wait_write_timeout(void *arg) {
    (void)arg;
    int ready = 111;
    atomic_store(&g_started, 1);
    int rc = mt_fd_wait(load_fd0(), MT_FD_WRITE, SHORT_TIMEOUT_MS, &ready);
    atomic_store(&g_rc, rc);
    atomic_store(&g_ready, ready);
}

static void task_drain_and_write_peer_when_two_waiters(void *arg) {
    (void)arg;
    while (mt_debug_fd_waiting_task_count() < 2) {
        mt_yield();
    }
    drain_some(g_fd1);
    char c = 'q';
    ssize_t n;
    do {
        n = write(g_fd1, &c, 1);
    } while (n < 0 && errno == EINTR);
    CHECK(n == 1);
}

static void task_cancel_waiter(void *arg) {
    mt_task_handle_t *handle = (mt_task_handle_t *)arg;
    while (!atomic_load(&g_started)) {
        mt_yield();
    }
    mt_sleep_ms(5);
    CHECK(mt_task_cancel(handle) == MT_OK);
}

static void task_wait_read_cancellable(void *arg) {
    (void)arg;
    int ready = 0;
    atomic_store(&g_started, 1);
    int rc = mt_fd_wait(load_fd0(), MT_FD_READ, LONG_TIMEOUT_MS, &ready);
    atomic_store(&g_rc, rc);
    atomic_store(&g_ready, ready);
    atomic_store(&g_value, mt_task_cancelled());
}

static void task_duplicate_waiter(void *arg) {
    (void)arg;
    while (mt_debug_fd_waiting_task_count() == 0) {
        mt_yield();
    }
    int ready = 0;
    int rc = mt_fd_wait(load_fd0(), MT_FD_READ, SHORT_TIMEOUT_MS, &ready);
    atomic_store(&g_rc2, rc);
    CHECK(mt_fd_close(load_fd0()) == MT_OK);
    store_fd0(-1);
}

static void task_duplicate_ready_waiter(void *arg) {
    (void)arg;
    while (mt_debug_fd_waiting_task_count() == 0) {
        mt_yield();
    }
    char c = 'r';
    CHECK(write(g_fd1, &c, 1) == 1);
    int ready = 0;
    int rc = mt_fd_wait(load_fd0(), MT_FD_READ, SHORT_TIMEOUT_MS, &ready);
    atomic_store(&g_rc2, rc);
    atomic_store(&g_ready, ready);
    CHECK(mt_fd_close(load_fd0()) == MT_OK);
    store_fd0(-1);
}

static void task_blocking_fd_read_timeout(void *arg) {
    (void)arg;
    char c = 0;
    atomic_store(&g_started, 1);
    ssize_t n = mt_fd_read(load_fd0(), &c, 1, SHORT_TIMEOUT_MS);
    atomic_store(&g_rc, (int)n);
}

static void task_close_race_waiter(void *arg) {
    (void)arg;
    while (mt_debug_fd_waiting_task_count() == 0) {
        mt_yield();
    }
    mt_sleep_ms(5);
    int ready = 0;
    int rc = mt_fd_wait(load_fd0(), MT_FD_READ, SHORT_TIMEOUT_MS, &ready);
    atomic_store(&g_rc2, rc);
}

static void task_release_active_waiter(void *arg) {
    (void)arg;
    while (mt_debug_fd_waiting_task_count() == 0) {
        mt_yield();
    }
    atomic_store(&g_rc2, mt_fd_release(load_fd0()));
    CHECK(mt_fd_close(load_fd0()) == MT_OK);
    store_fd0(-1);
}

static void task_wait_read_then_joinable(void *arg) {
    (void)arg;
    char c = 0;
    ssize_t n = mt_fd_read(load_fd0(), &c, 1, LONG_TIMEOUT_MS);
    CHECK(n == 1);
    atomic_store(&g_value, (int)c);
}

static void task_join_waiter(void *arg) {
    mt_task_handle_t *handle = (mt_task_handle_t *)arg;
    int rc = mt_join(handle);
    atomic_store(&g_rc2, rc);
}

static void task_channel_while_fd_pending(void *arg) {
    (void)arg;
    int v = 42;
    CHECK(mt_chan_send(g_ch, &v) == MT_OK);
}

static void task_channel_receiver(void *arg) {
    (void)arg;
    int v = 0;
    CHECK(mt_chan_recv(g_ch, &v) == MT_OK);
    atomic_store(&g_value, v);
}

static void task_select_while_fd_pending(void *arg) {
    (void)arg;
    int v = 0;
    size_t idx = 99;
    mt_select_case_t cases[] = {
        { .op = MT_SELECT_RECV, .ch = g_ch, .value = &v, .timeout_ms = 0 },
        { .op = MT_SELECT_TIMEOUT, .ch = NULL, .value = NULL, .timeout_ms = LONG_TIMEOUT_MS }
    };
    int rc = mt_select(cases, ARRAY_LEN(cases), &idx);
    CHECK(rc == MT_OK);
    CHECK(idx == 0);
    atomic_store(&g_value, v);
}

static void task_select_sender_after_sleep(void *arg) {
    (void)arg;
    int v = 77;
    mt_sleep_ms(5);
    CHECK(mt_chan_send(g_ch, &v) == MT_OK);
}

static void task_fd_reader_sends_channel(void *arg) {
    (void)arg;
    char c = 0;
    ssize_t n = mt_fd_read(load_fd0(), &c, 1, LONG_TIMEOUT_MS);
    CHECK(n == 1);
    int v = (int)c;
    CHECK(mt_chan_send(g_ch, &v) == MT_OK);
}

static void task_accept_waiter(void *arg) {
    (void)arg;
    atomic_store(&g_started, 1);
    int fd = mt_net_accept(g_listen_fd, NULL, NULL, LONG_TIMEOUT_MS);
    atomic_store(&g_rc, fd >= 0 ? MT_OK : fd);
    if (fd >= 0) {
        assert_fd_nonblocking(fd);
        char c = 0;
        ssize_t n = mt_net_read(fd, &c, 1, LONG_TIMEOUT_MS);
        CHECK(n == 1);
        atomic_store(&g_value, (int)c);
        CHECK(mt_net_close(fd) == MT_OK);
    }
}

static void task_accept_cancellable(void *arg) {
    (void)arg;
    atomic_store(&g_started, 1);
    int fd = mt_net_accept(g_listen_fd, NULL, NULL, LONG_TIMEOUT_MS);
    atomic_store(&g_rc, fd);
    if (fd >= 0) {
        CHECK(mt_net_close(fd) == MT_OK);
    }
}

static void task_close_listener_after_sleep(void *arg) {
    (void)arg;
    while (!atomic_load(&g_started)) {
        mt_yield();
    }
    mt_sleep_ms(5);
    CHECK(mt_fd_close(g_listen_fd) == MT_OK);
    g_listen_fd = -1;
}

static void task_echo_server(void *arg) {
    (void)arg;
    atomic_store(&g_started, 1);
    for (int i = 0; i < ECHO_CLIENTS; ++i) {
        int fd = mt_net_accept(g_listen_fd, NULL, NULL, LONG_TIMEOUT_MS);
        CHECK(fd >= 0);
        char c = 0;
        ssize_t n = mt_net_read(fd, &c, 1, LONG_TIMEOUT_MS);
        CHECK(n == 1);
        n = mt_net_write(fd, &c, 1, LONG_TIMEOUT_MS);
        CHECK(n == 1);
        CHECK(mt_net_close(fd) == MT_OK);
        atomic_fetch_add(&g_counter, 1);
    }
}

static void task_echo_clients(void *arg) {
    (void)arg;
    while (!atomic_load(&g_started)) {
        mt_yield();
    }
    int port = atomic_load(&g_listener_port);
    for (int i = 0; i < ECHO_CLIENTS; ++i) {
        int fd = connect_loopback_port(port);
        char out = (char)('a' + i);
        CHECK(write(fd, &out, 1) == 1);
        char in = 0;
        CHECK(read(fd, &in, 1) == 1);
        CHECK(in == out);
        close(fd);
        atomic_fetch_add(&g_counter2, 1);
    }
}

static void task_accept_timeout(void *arg) {
    (void)arg;
    int fd = mt_net_accept(g_listen_fd, NULL, NULL, SHORT_TIMEOUT_MS);
    atomic_store(&g_rc, fd);
}

static void task_accept_many(void *arg) {
    (void)arg;
    for (int i = 0; i < TCP_CLIENTS; ++i) {
        int fd = mt_net_accept(g_listen_fd, NULL, NULL, LONG_TIMEOUT_MS);
        CHECK(fd >= 0);
        char c = 0;
        ssize_t n = mt_net_read(fd, &c, 1, LONG_TIMEOUT_MS);
        CHECK(n == 1);
        atomic_fetch_add(&g_counter, 1);
        atomic_fetch_add(&g_value, (int)c);
        CHECK(mt_net_close(fd) == MT_OK);
    }
}

static void task_connect_client_after_sleep(void *arg) {
    int byte = *(int *)arg;
    while (!atomic_load(&g_started)) {
        mt_yield();
    }
    mt_sleep_ms(5);
    int fd = connect_loopback_port(atomic_load(&g_listener_port));
    char c = (char)byte;
    CHECK(write(fd, &c, 1) == 1);
    close(fd);
}

static void task_connect_many_clients(void *arg) {
    (void)arg;
    int port = atomic_load(&g_listener_port);
    for (int i = 0; i < TCP_CLIENTS; ++i) {
        int fd = connect_loopback_port(port);
        char c = (char)(i + 1);
        CHECK(write(fd, &c, 1) == 1);
        close(fd);
    }
}

static void task_runtime_anchor(void *arg) {
    (void)arg;
    atomic_store(&g_started, 1);
    while (!atomic_load(&g_stop)) {
        mt_sleep_ms(1);
    }
}

static void *runtime_thread_main(void *arg) {
    size_t workers = *(size_t *)arg;
    int rc = mt_runtime_start(workers);
    atomic_store(&g_rc2, rc);
    return NULL;
}

static void task_submit_increment(void *arg) {
    (void)arg;
    atomic_fetch_add(&g_counter, 1);
}

static void *external_submitter_main(void *arg) {
    (void)arg;
    CHECK(mt_go(task_submit_increment, NULL) > 0);
    return NULL;
}

static void task_large_reader(void *arg) {
    (void)arg;
    char buf[4096];
    long total = 0;
    while (total < LARGE_BYTES) {
        ssize_t n = mt_fd_read(load_fd0(), buf, sizeof(buf), LONG_TIMEOUT_MS);
        CHECK(n > 0);
        total += n;
    }
    atomic_store(&g_bytes, total);
}

static void task_large_writer(void *arg) {
    (void)arg;
    char buf[4096];
    memset(buf, 'z', sizeof(buf));
    long total = 0;
    while (total < LARGE_BYTES) {
        size_t want = sizeof(buf);
        if (LARGE_BYTES - total < (long)want) {
            want = (size_t)(LARGE_BYTES - total);
        }
        ssize_t n = mt_fd_write(g_fd1, buf, want, LONG_TIMEOUT_MS);
        CHECK(n > 0);
        total += n;
    }
    atomic_store(&g_done, 1);
}

static void task_stress_reader(void *arg) {
    int *fds = (int *)arg;
    int fd = fds[0];
    char c = 0;
    ssize_t n = mt_fd_read(fd, &c, 1, LONG_TIMEOUT_MS);
    CHECK(n == 1);
    atomic_fetch_add(&g_counter, (int)c);
    CHECK(mt_fd_close(fd) == MT_OK);
    fds[0] = -1;
}

static void task_stress_writer(void *arg) {
    int *fds = (int *)arg;
    int fd = fds[1];
    char c = 1;
    ssize_t n = mt_fd_write(fd, &c, 1, LONG_TIMEOUT_MS);
    CHECK(n == 1);
    CHECK(mt_fd_close(fd) == MT_OK);
    fds[1] = -1;
}

static void test_backend_lifecycle_and_reporting(void) {
    reset_runtime();
    const char *backend = mt_io_backend_name();
#if defined(MT_FORCE_POLL_BACKEND)
    CHECK(strcmp(backend, "poll") == 0);
#elif defined(__linux__)
    CHECK(strcmp(backend, "epoll") == 0 || strcmp(backend, "poll") == 0);
#elif defined(__APPLE__) || defined(__FreeBSD__) || defined(__OpenBSD__) || defined(__NetBSD__)
    CHECK(strcmp(backend, "kqueue") == 0 || strcmp(backend, "poll") == 0);
#else
    CHECK(strcmp(backend, "poll") == 0 || strcmp(backend, "unsupported") == 0);
#endif
    CHECK(mt_runtime_start(WORKERS_1) == MT_OK);
    finish_runtime();
    assert_core_counters_balanced();
    assert_io_counters_balanced();

    reset_runtime();
    mt_test_fail_next_io_backend_init();
    mt_shutdown();
    CHECK(mt_init() == MT_ERR_BACKEND);
    CHECK(mt_init() == MT_OK);
    finish_runtime();
    assert_io_counters_balanced();
}

static void test_nonblocking_validation_and_immediate_ready(void) {
    reset_runtime();
    make_socketpair();
    CHECK(mt_fd_set_nonblocking(load_fd0()) == MT_OK);
    CHECK(mt_fd_set_nonblocking(load_fd0()) == MT_OK);
    CHECK(mt_fd_adopt(load_fd0()) == MT_OK);
    CHECK(mt_fd_release(load_fd0()) == MT_OK);
    CHECK(mt_fd_adopt(load_fd0()) == MT_OK);
    assert_fd_nonblocking(load_fd0());
    CHECK(mt_fd_set_nonblocking(-1) == MT_ERR_INVALID);
    int ready = 99;
    CHECK(mt_fd_wait(-1, MT_FD_READ, 0, &ready) == MT_ERR_INVALID);
    CHECK(mt_fd_wait(load_fd0(), 0, 0, &ready) == MT_ERR_INVALID);
    CHECK(mt_fd_wait(load_fd0(), 16, 0, &ready) == MT_ERR_INVALID);
    CHECK(mt_fd_wait(load_fd0(), MT_FD_READ, 0, NULL) == MT_ERR_INVALID);
    CHECK(mt_fd_wait(load_fd0(), MT_FD_READ, 0, &ready) == MT_ERR_STATE);
    CHECK(mt_fd_read(-1, &ready, 1, 0) == MT_ERR_INVALID);
    CHECK(mt_fd_read(load_fd0(), NULL, 1, 0) == MT_ERR_INVALID);
    CHECK(mt_fd_write(-1, &ready, 1, 0) == MT_ERR_INVALID);
    CHECK(mt_fd_write(load_fd0(), NULL, 1, 0) == MT_ERR_INVALID);
    CHECK(mt_fd_read(load_fd0(), NULL, 0, 0) == 0);
    CHECK(mt_fd_write(load_fd0(), NULL, 0, 0) == 0);
    const char c = 'a';
    CHECK(write(g_fd1, &c, 1) == 1);
    CHECK(mt_go(task_wait_read, NULL) > 0);
    CHECK(mt_runtime_start(WORKERS_1) == MT_OK);
    CHECK(atomic_load(&g_rc) == MT_OK);
    CHECK((atomic_load(&g_ready) & MT_FD_READ) != 0);
    finish_runtime();

    reset_runtime();
    make_socketpair();
    CHECK(mt_go(task_wait_read, NULL) > 0);
    CHECK(mt_go(task_release_active_waiter, NULL) > 0);
    CHECK(mt_runtime_start(WORKERS_1) == MT_OK);
    CHECK(atomic_load(&g_rc) == MT_ERR_CLOSED);
    CHECK(atomic_load(&g_rc2) == MT_ERR_STATE);
    finish_runtime();

    assert_core_counters_balanced();
    assert_io_counters_balanced();
}

static void test_combined_fd_events_and_hangup(void) {
    reset_runtime();
    make_socketpair();
    const char c = 'r';
    CHECK(write(g_fd1, &c, 1) == 1);
    CHECK(mt_go(task_wait_read_write, NULL) > 0);
    CHECK(mt_runtime_start(WORKERS_1) == MT_OK);
    CHECK(atomic_load(&g_rc) == MT_OK);
    CHECK((atomic_load(&g_ready) & MT_FD_READ) != 0);
    CHECK((atomic_load(&g_ready) & MT_FD_WRITE) != 0);
    finish_runtime();

    reset_runtime();
    make_socketpair();
    CHECK(mt_go(task_wait_read_write, NULL) > 0);
    CHECK(mt_runtime_start(WORKERS_1) == MT_OK);
    CHECK(atomic_load(&g_rc) == MT_OK);
    CHECK((atomic_load(&g_ready) & MT_FD_WRITE) != 0);
    CHECK((atomic_load(&g_ready) & MT_FD_READ) == 0);
    finish_runtime();

    reset_runtime();
    make_socketpair();
    fill_send_buffer(load_fd0());
    CHECK(write(g_fd1, &c, 1) == 1);
    CHECK(mt_go(task_wait_read_write, NULL) > 0);
    CHECK(mt_runtime_start(WORKERS_1) == MT_OK);
    CHECK(atomic_load(&g_rc) == MT_OK);
    CHECK((atomic_load(&g_ready) & MT_FD_READ) != 0);
    CHECK((atomic_load(&g_ready) & MT_FD_WRITE) == 0);
    finish_runtime();

    reset_runtime();
    make_socketpair();
    fill_send_buffer(load_fd0());
    CHECK(mt_go(task_wait_read_write_timeout, NULL) > 0);
    CHECK(mt_runtime_start(WORKERS_1) == MT_OK);
    CHECK(atomic_load(&g_rc) == MT_ERR_TIMEOUT);
    CHECK(atomic_load(&g_ready) == 0);
    finish_runtime();

    reset_runtime();
    make_socketpair();
    CHECK(mt_fd_close(g_fd1) == MT_OK);
    g_fd1 = -1;
    CHECK(mt_go(task_wait_read_write, NULL) > 0);
    CHECK(mt_runtime_start(WORKERS_1) == MT_OK);
    CHECK(atomic_load(&g_rc) == MT_OK);
    CHECK((atomic_load(&g_ready) & MT_FD_READ) != 0);
    finish_runtime();

    assert_core_counters_balanced();
    assert_io_counters_balanced();
}

static void test_read_wait_wakeup_productivity_close_and_timeout(void) {
    reset_runtime();
    make_socketpair();
    CHECK(mt_go(task_wait_read, NULL) > 0);
    CHECK(mt_go(task_write_peer_after_sleep, "x") > 0);
    CHECK(mt_go(task_busy_counter, NULL) > 0);
    CHECK(mt_runtime_start(WORKERS_2) == MT_OK);
    CHECK(atomic_load(&g_rc) == MT_OK);
    CHECK((atomic_load(&g_ready) & MT_FD_READ) != 0);
    CHECK(atomic_load(&g_counter) > 0);
    finish_runtime();

    reset_runtime();
    make_socketpair();
    CHECK(mt_go(task_wait_read_timeout, NULL) > 0);
    CHECK(mt_runtime_start(WORKERS_1) == MT_OK);
    CHECK(atomic_load(&g_rc) == MT_ERR_TIMEOUT);
    CHECK(atomic_load(&g_ready) == 0);
    finish_runtime();

    reset_runtime();
    make_socketpair();
    CHECK(mt_go(task_wait_read, NULL) > 0);
    CHECK(mt_go(task_close_waited_fd_after_sleep, NULL) > 0);
    CHECK(mt_runtime_start(WORKERS_2) == MT_OK);
    CHECK(atomic_load(&g_rc) == MT_ERR_CLOSED);
    finish_runtime();

    for (int i = 0; i < 32; ++i) {
        reset_runtime();
        make_socketpair();
        CHECK(mt_go(task_wait_read, NULL) > 0);
        CHECK(mt_go(task_close_waited_fd_after_sleep, NULL) > 0);
        CHECK(mt_go(task_close_race_waiter, NULL) > 0);
        CHECK(mt_runtime_start(WORKERS_4) == MT_OK);
        CHECK(atomic_load(&g_rc) == MT_ERR_CLOSED);
        CHECK(atomic_load(&g_rc2) != MT_ERR_TIMEOUT);
        CHECK(atomic_load(&g_rc2) != MT_OK);
        finish_runtime();
    }

    assert_core_counters_balanced();
    assert_io_counters_balanced();
}

static void test_write_wait_wakeup_and_timeout(void) {
    reset_runtime();
    make_socketpair();
    fill_send_buffer(load_fd0());
    CHECK(mt_go(task_wait_write, NULL) > 0);
    CHECK(mt_go(task_drain_peer_after_sleep, NULL) > 0);
    CHECK(mt_go(task_busy_counter, NULL) > 0);
    CHECK(mt_runtime_start(WORKERS_2) == MT_OK);
    CHECK(atomic_load(&g_rc) == MT_OK);
    CHECK((atomic_load(&g_ready) & MT_FD_WRITE) != 0);
    CHECK(atomic_load(&g_counter) > 0);
    finish_runtime();

    reset_runtime();
    make_socketpair();
    fill_send_buffer(load_fd0());
    CHECK(mt_go(task_wait_write_timeout, NULL) > 0);
    CHECK(mt_runtime_start(WORKERS_1) == MT_OK);
    CHECK(atomic_load(&g_rc) == MT_ERR_TIMEOUT);
    CHECK(atomic_load(&g_ready) == 0);
    finish_runtime();

    assert_core_counters_balanced();
    assert_io_counters_balanced();
}

static void test_fd_read_write_wrappers_and_large_transfer(void) {
    reset_runtime();
    make_socketpair();
    CHECK(mt_go(task_read_wrapper, NULL) > 0);
    CHECK(mt_go(task_write_peer_after_sleep, "hello") > 0);
    CHECK(mt_runtime_start(WORKERS_2) == MT_OK);
    CHECK(atomic_load(&g_rc) == 5);
    CHECK(atomic_load(&g_value) == 1);
    finish_runtime();

    reset_runtime();
    make_socketpair();
    CHECK(mt_fd_close(g_fd1) == MT_OK);
    g_fd1 = -1;
    CHECK(mt_go(task_read_eof_wrapper, NULL) > 0);
    CHECK(mt_runtime_start(WORKERS_1) == MT_OK);
    CHECK(atomic_load(&g_rc) == 0);
    finish_runtime();

    reset_runtime();
    make_socketpair();
    fill_send_buffer(load_fd0());
    CHECK(mt_go(task_write_wrapper, "hello") > 0);
    CHECK(mt_go(task_drain_peer_after_sleep, NULL) > 0);
    CHECK(mt_runtime_start(WORKERS_2) == MT_OK);
    CHECK(atomic_load(&g_rc) == 5);
    finish_runtime();

    reset_runtime();
    make_socketpair();
    CHECK(mt_go(task_large_reader, NULL) > 0);
    CHECK(mt_go(task_large_writer, NULL) > 0);
    CHECK(mt_runtime_start(WORKERS_2) == MT_OK);
    CHECK(atomic_load(&g_done) == 1);
    CHECK(atomic_load(&g_bytes) == LARGE_BYTES);
    finish_runtime();

    reset_runtime();
    make_blocking_socketpair();
    CHECK(mt_go(task_blocking_fd_read_timeout, NULL) > 0);
    CHECK(mt_go(task_busy_counter, NULL) > 0);
    CHECK(mt_runtime_start(WORKERS_2) == MT_OK);
    CHECK(atomic_load(&g_rc) == MT_ERR_TIMEOUT);
    CHECK(atomic_load(&g_counter) > 0);
    assert_fd_nonblocking(load_fd0());
    finish_runtime();

    assert_core_counters_balanced();
    assert_io_counters_balanced();
}

static void test_cancellation_duplicate_waiter_join_and_integration(void) {
    reset_runtime();
    make_socketpair();
    g_handle = mt_go_handle(task_wait_read_cancellable, NULL);
    CHECK(g_handle != NULL);
    CHECK(mt_go(task_cancel_waiter, g_handle) > 0);
    CHECK(mt_runtime_start(WORKERS_2) == MT_OK);
    CHECK(atomic_load(&g_rc) == MT_ERR_CANCELLED);
    CHECK(atomic_load(&g_value) != 0);
    finish_runtime();

    reset_runtime();
    make_socketpair();
    CHECK(mt_go(task_wait_read, NULL) > 0);
    CHECK(mt_go(task_duplicate_waiter, NULL) > 0);
    CHECK(mt_runtime_start(WORKERS_2) == MT_OK);
    CHECK(atomic_load(&g_rc) == MT_ERR_CLOSED || atomic_load(&g_rc) == MT_OK);
    CHECK(atomic_load(&g_rc2) == MT_ERR_STATE);
    finish_runtime();

    reset_runtime();
    make_socketpair();
    CHECK(mt_go(task_wait_read, NULL) > 0);
    CHECK(mt_go(task_duplicate_ready_waiter, NULL) > 0);
    CHECK(mt_runtime_start(WORKERS_2) == MT_OK);
    CHECK(atomic_load(&g_rc) == MT_ERR_CLOSED || atomic_load(&g_rc) == MT_OK);
#if defined(MT_TSAN)
    /*
     * This scenario is intentionally trying to hit the narrow window where an
     * fd waiter has been readied by the backend but has not yet resumed and
     * consumed its reservation.  TSan instrumentation can slow the original
     * waiter enough, or speed the cleanup path enough relative to this task,
     * that the reservation is gone before the duplicate wait attempt runs.  In
     * that legal interleaving the duplicate wait has no peer write left to
     * consume and times out instead of observing MT_ERR_STATE.
     */
    CHECK(atomic_load(&g_rc2) == MT_ERR_STATE || atomic_load(&g_rc2) == MT_ERR_TIMEOUT);
#else
    CHECK(atomic_load(&g_rc2) == MT_ERR_STATE);
#endif
    finish_runtime();

    reset_runtime();
    make_socketpair();
    fill_send_buffer(load_fd0());
    CHECK(mt_go(task_wait_write, NULL) > 0);
    CHECK(mt_go(task_wait_read_rc2, NULL) > 0);
    CHECK(mt_go(task_drain_and_write_peer_when_two_waiters, NULL) > 0);
    CHECK(mt_runtime_start(WORKERS_4) == MT_OK);
    CHECK(atomic_load(&g_rc) == MT_OK);
    CHECK((atomic_load(&g_ready) & MT_FD_WRITE) != 0);
    CHECK(atomic_load(&g_rc2) == MT_OK);
    CHECK((atomic_load(&g_ready2) & MT_FD_READ) != 0);
    finish_runtime();

    reset_runtime();
    make_socketpair();
    g_handle = mt_go_handle(task_wait_read, NULL);
    CHECK(g_handle != NULL);
    size_t workers_for_status = WORKERS_1;
    pthread_t status_rt;
    CHECK(pthread_create(&status_rt, NULL, runtime_thread_main, &workers_for_status) == 0);
    while (!atomic_load(&g_started) || mt_runtime_workers() == 0 ||
           mt_debug_fd_waiting_task_count() == 0) {
        sched_yield();
    }
    mt_task_status_t status = MT_TASK_STATUS_DONE;
    CHECK(mt_task_status(g_handle, &status) == MT_OK);
    CHECK(status == MT_TASK_STATUS_WAITING_FD);
    CHECK(mt_fd_close(load_fd0()) == MT_OK);
    store_fd0(-1);
    CHECK(pthread_join(status_rt, NULL) == 0);
    CHECK(atomic_load(&g_rc) == MT_ERR_CLOSED);
    finish_runtime();

    reset_runtime();
    make_socketpair();
    atomic_store(&g_started, 1);
    g_handle = mt_go_handle(task_wait_read_then_joinable, NULL);
    CHECK(g_handle != NULL);
    CHECK(mt_go(task_join_waiter, g_handle) > 0);
    CHECK(mt_go(task_write_peer_after_sleep, "Q") > 0);
    CHECK(mt_runtime_start(WORKERS_2) == MT_OK);
    CHECK(atomic_load(&g_rc2) == MT_OK);
    CHECK(atomic_load(&g_value) == 'Q');
    finish_runtime();

    reset_runtime();
    make_socketpair();
    g_ch = mt_chan_create(sizeof(int), 0);
    CHECK(g_ch != NULL);
    CHECK(mt_go(task_wait_read_timeout, NULL) > 0);
    CHECK(mt_go(task_channel_receiver, NULL) > 0);
    CHECK(mt_go(task_channel_while_fd_pending, NULL) > 0);
    CHECK(mt_runtime_start(WORKERS_2) == MT_OK);
    CHECK(atomic_load(&g_value) == 42);
    CHECK(atomic_load(&g_rc) == MT_ERR_TIMEOUT);
    finish_runtime();

    reset_runtime();
    make_socketpair();
    g_ch = mt_chan_create(sizeof(int), 0);
    CHECK(g_ch != NULL);
    CHECK(mt_go(task_wait_read_timeout, NULL) > 0);
    CHECK(mt_go(task_select_while_fd_pending, NULL) > 0);
    CHECK(mt_go(task_select_sender_after_sleep, NULL) > 0);
    CHECK(mt_runtime_start(WORKERS_2) == MT_OK);
    CHECK(atomic_load(&g_value) == 77);
    CHECK(atomic_load(&g_rc) == MT_ERR_TIMEOUT);
    finish_runtime();

    reset_runtime();
    make_socketpair();
    g_ch = mt_chan_create(sizeof(int), 0);
    CHECK(g_ch != NULL);
    atomic_store(&g_started, 1);
    CHECK(mt_go(task_fd_reader_sends_channel, NULL) > 0);
    CHECK(mt_go(task_channel_receiver, NULL) > 0);
    CHECK(mt_go(task_write_peer_after_sleep, "B") > 0);
    CHECK(mt_runtime_start(WORKERS_2) == MT_OK);
    CHECK(atomic_load(&g_value) == 'B');
    finish_runtime();

    assert_core_counters_balanced();
    assert_io_counters_balanced();
}

static void test_tcp_accept_and_net_wrappers(void) {
    reset_runtime();
    g_listen_fd = mt_net_listen_tcp("127.0.0.1", "0", 64);
    CHECK(g_listen_fd >= 0);
    assert_fd_nonblocking(g_listen_fd);
    get_listener_port(g_listen_fd);
    CHECK(mt_go(task_accept_waiter, NULL) > 0);
    int byte = 'Z';
    CHECK(mt_go(task_connect_client_after_sleep, &byte) > 0);
    CHECK(mt_runtime_start(WORKERS_2) == MT_OK);
    CHECK(atomic_load(&g_rc) == MT_OK);
    CHECK(atomic_load(&g_value) == 'Z');
    finish_runtime();

    reset_runtime();
    g_listen_fd = mt_net_listen_tcp("127.0.0.1", "0", 64);
    CHECK(g_listen_fd >= 0);
    assert_fd_nonblocking(g_listen_fd);
    CHECK(mt_go(task_accept_timeout, NULL) > 0);
    CHECK(mt_runtime_start(WORKERS_1) == MT_OK);
    CHECK(atomic_load(&g_rc) == MT_ERR_TIMEOUT);
    finish_runtime();

    reset_runtime();
    g_listen_fd = mt_net_listen_tcp("127.0.0.1", "0", 64);
    CHECK(g_listen_fd >= 0);
    assert_fd_nonblocking(g_listen_fd);
    get_listener_port(g_listen_fd);
    CHECK(mt_go(task_accept_many, NULL) > 0);
    CHECK(mt_go(task_connect_many_clients, NULL) > 0);
    CHECK(mt_runtime_start(WORKERS_4) == MT_OK);
    CHECK(atomic_load(&g_counter) == TCP_CLIENTS);
    CHECK(atomic_load(&g_value) == (TCP_CLIENTS * (TCP_CLIENTS + 1)) / 2);
    finish_runtime();

    reset_runtime();
    g_listen_fd = mt_net_listen_tcp("127.0.0.1", "0", 64);
    CHECK(g_listen_fd >= 0);
    g_handle = mt_go_handle(task_accept_cancellable, NULL);
    CHECK(g_handle != NULL);
    CHECK(mt_go(task_cancel_waiter, g_handle) > 0);
    CHECK(mt_runtime_start(WORKERS_2) == MT_OK);
    CHECK(atomic_load(&g_rc) == MT_ERR_CANCELLED);
    finish_runtime();

    reset_runtime();
    g_listen_fd = mt_net_listen_tcp("127.0.0.1", "0", 64);
    CHECK(g_listen_fd >= 0);
    CHECK(mt_go(task_accept_cancellable, NULL) > 0);
    CHECK(mt_go(task_close_listener_after_sleep, NULL) > 0);
    CHECK(mt_runtime_start(WORKERS_2) == MT_OK);
    CHECK(atomic_load(&g_rc) == MT_ERR_CLOSED);
    finish_runtime();

    reset_runtime();
    g_listen_fd = mt_net_listen_tcp("127.0.0.1", "0", 64);
    CHECK(g_listen_fd >= 0);
    int open_before_conflict = count_open_fds();
    get_listener_port(g_listen_fd);
    char port_buf[32];
    snprintf(port_buf, sizeof(port_buf), "%d", atomic_load(&g_listener_port));
    int conflict_fd = mt_net_listen_tcp("127.0.0.1", port_buf, 64);
    CHECK(conflict_fd == MT_ERR_IO);
    CHECK(count_open_fds() == open_before_conflict);
    assert_fd_nonblocking(g_listen_fd);
    finish_runtime();

    reset_runtime();
    g_listen_fd = mt_net_listen_tcp("127.0.0.1", "0", 64);
    CHECK(g_listen_fd >= 0);
    get_listener_port(g_listen_fd);
    CHECK(mt_go(task_echo_server, NULL) > 0);
    CHECK(mt_go(task_echo_clients, NULL) > 0);
    CHECK(mt_runtime_start(WORKERS_4) == MT_OK);
    CHECK(atomic_load(&g_counter) == ECHO_CLIENTS);
    CHECK(atomic_load(&g_counter2) == ECHO_CLIENTS);
    finish_runtime();

    reset_runtime();
    make_socketpair();
    CHECK(mt_fd_close(g_fd1) == MT_OK);
    g_fd1 = -1;
    CHECK(mt_go(task_net_write_wrapper, "x") > 0);
    CHECK(mt_runtime_start(WORKERS_1) == MT_OK);
    CHECK(atomic_load(&g_rc) == MT_ERR_IO);
    finish_runtime();

    assert_core_counters_balanced();
    assert_io_counters_balanced();
}

static void test_fd_reuse_and_readiness_races(void) {
    reset_runtime();
    make_socketpair();
    g_old_fd = load_fd0();
    CHECK(mt_go(task_wait_read, NULL) > 0);
    size_t workers = WORKERS_2;
    pthread_t runtime_thread;
    CHECK(pthread_create(&runtime_thread, NULL, runtime_thread_main, &workers) == 0);
    while (!atomic_load(&g_started) || mt_runtime_workers() == 0 ||
           mt_debug_fd_waiting_task_count() == 0) {
        sched_yield();
    }
    CHECK(mt_fd_close(load_fd0()) == MT_OK);
    store_fd0(-1);
    int reused[2] = { -1, -1 };
    for (int i = 0; i < 64 && reused[0] != g_old_fd && reused[1] != g_old_fd; ++i) {
        close_if_open(&reused[0]);
        close_if_open(&reused[1]);
        CHECK(socketpair(AF_UNIX, SOCK_STREAM, 0, reused) == 0);
    }
    CHECK(reused[0] == g_old_fd || reused[1] == g_old_fd);
    CHECK(mt_fd_set_nonblocking(reused[0]) == MT_OK);
    CHECK(mt_fd_set_nonblocking(reused[1]) == MT_OK);
    char c = 'n';
    if (reused[0] == g_old_fd) {
        CHECK(write(reused[1], &c, 1) == 1);
    } else {
        CHECK(write(reused[0], &c, 1) == 1);
    }
    CHECK(pthread_join(runtime_thread, NULL) == 0);
    CHECK(atomic_load(&g_rc) == MT_ERR_CLOSED);
    CHECK(atomic_load(&g_rc2) == MT_OK);
    close_if_open(&reused[0]);
    close_if_open(&reused[1]);
    finish_runtime();

    reset_runtime();
    make_socketpair();
    CHECK(mt_go(task_wait_read_timeout, NULL) > 0);
    CHECK(mt_go(task_write_peer_after_longer_sleep, "t") > 0);
    CHECK(mt_runtime_start(WORKERS_2) == MT_OK);
    CHECK(atomic_load(&g_rc) == MT_ERR_TIMEOUT);
    CHECK(atomic_load(&g_ready) == 0);
    CHECK(atomic_load(&g_counter) == 1);
    finish_runtime();

    reset_runtime();
    make_socketpair();
    g_handle = mt_go_handle(task_wait_read_cancellable, NULL);
    CHECK(g_handle != NULL);
    CHECK(mt_go(task_cancel_then_write_peer, g_handle) > 0);
    CHECK(mt_runtime_start(WORKERS_2) == MT_OK);
    CHECK(atomic_load(&g_rc) == MT_ERR_CANCELLED);
    CHECK(atomic_load(&g_counter) == 1);
    finish_runtime();

    assert_core_counters_balanced();
    assert_io_counters_balanced();
}

static void test_close_reuse_shutdown_faults_and_stress(void) {
    reset_runtime();
    make_socketpair();
    CHECK(mt_fd_close(load_fd0()) == MT_OK);
    store_fd0(-1);
    CHECK(mt_fd_close(load_fd0()) == MT_ERR_INVALID);
    finish_runtime();

    reset_runtime();
    make_socketpair();
    CHECK(mt_go(task_wait_read, NULL) > 0);
    CHECK(mt_go(task_close_waited_fd_after_sleep, NULL) > 0);
    CHECK(mt_runtime_start(WORKERS_2) == MT_OK);
    CHECK(atomic_load(&g_rc) == MT_ERR_CLOSED);
    int fresh[2] = { -1, -1 };
    CHECK(socketpair(AF_UNIX, SOCK_STREAM, 0, fresh) == 0);
    CHECK(mt_fd_set_nonblocking(fresh[0]) == MT_OK);
    CHECK(mt_fd_set_nonblocking(fresh[1]) == MT_OK);
    close(fresh[0]);
    close(fresh[1]);
    finish_runtime();

    reset_runtime();
    make_socketpair();
    CHECK(mt_go(task_wait_read, NULL) > 0);
    size_t workers = WORKERS_2;
    pthread_t runtime_thread;
    CHECK(pthread_create(&runtime_thread, NULL, runtime_thread_main, &workers) == 0);
    while (!atomic_load(&g_started) || mt_runtime_workers() == 0 ||
           mt_debug_fd_waiting_task_count() == 0) {
        sched_yield();
    }
    mt_shutdown();
    CHECK(pthread_join(runtime_thread, NULL) == 0);
    if (atomic_load(&g_rc) != MT_ERR_CANCELLED || atomic_load(&g_rc2) != MT_ERR_CANCELLED) {
        fprintf(stderr, "shutdown waiter rc=%d runrc=%d\n", atomic_load(&g_rc), atomic_load(&g_rc2));
    }
    CHECK(atomic_load(&g_rc) == MT_ERR_CANCELLED);
    CHECK(atomic_load(&g_rc2) == MT_ERR_CANCELLED);
    finish_runtime();

    reset_runtime();
    make_socketpair();
    mt_test_fail_next_fd_waiter_alloc();
    CHECK(mt_go(task_wait_read, NULL) > 0);
    CHECK(mt_runtime_start(WORKERS_1) == MT_OK);
    CHECK(atomic_load(&g_rc) == MT_ERR_NOMEM);
    finish_runtime();

    reset_runtime();
    make_socketpair();
    mt_test_fail_next_io_backend_register();
    CHECK(mt_go(task_wait_read, NULL) > 0);
    CHECK(mt_runtime_start(WORKERS_1) == MT_OK);
    CHECK(atomic_load(&g_rc) == MT_ERR_BACKEND);
    finish_runtime();

    reset_runtime();
    make_socketpair();
    mt_test_fail_next_timer_alloc();
    CHECK(mt_go(task_wait_read, NULL) > 0);
    CHECK(mt_runtime_start(WORKERS_1) == MT_OK);
    CHECK(atomic_load(&g_rc) == MT_ERR_NOMEM);
    finish_runtime();

    reset_runtime();
    make_socketpair();
    mt_test_fail_next_io_backend_unregister();
    CHECK(mt_go(task_wait_read, NULL) > 0);
    CHECK(mt_go(task_close_waited_fd_after_sleep, NULL) > 0);
    CHECK(mt_runtime_start(WORKERS_2) == MT_OK);
    CHECK(atomic_load(&g_rc) == MT_ERR_CLOSED);
    finish_runtime();

    for (int cycle = 0; cycle < 8; ++cycle) {
        reset_runtime();
        make_socketpair();
        CHECK(mt_go(task_wait_read, NULL) > 0);
        CHECK(mt_go(task_write_peer_after_sleep, "r") > 0);
        CHECK(mt_runtime_start((cycle % 2) ? WORKERS_2 : WORKERS_1) == MT_OK);
        CHECK(atomic_load(&g_rc) == MT_OK);
        finish_runtime();
    }

    reset_runtime();
    int fds[STRESS_PAIRS][2];
    for (int i = 0; i < STRESS_PAIRS; ++i) {
        CHECK(socketpair(AF_UNIX, SOCK_STREAM, 0, fds[i]) == 0);
        CHECK(mt_fd_set_nonblocking(fds[i][0]) == MT_OK);
        CHECK(mt_fd_set_nonblocking(fds[i][1]) == MT_OK);
        CHECK(mt_go(task_stress_reader, fds[i]) > 0);
        CHECK(mt_go(task_stress_writer, fds[i]) > 0);
    }
    CHECK(mt_runtime_start(WORKERS_4) == MT_OK);
    CHECK(atomic_load(&g_counter) == STRESS_PAIRS);
    for (int i = 0; i < STRESS_PAIRS; ++i) {
        close_if_open(&fds[i][0]);
        close_if_open(&fds[i][1]);
    }
    finish_runtime();

    reset_runtime();
    make_socketpair();
    CHECK(mt_go(task_runtime_anchor, NULL) > 0);
    CHECK(mt_go(task_wait_read_timeout, NULL) > 0);
    pthread_t rt;
    CHECK(pthread_create(&rt, NULL, runtime_thread_main, &workers) == 0);
    while (!atomic_load(&g_started) || mt_runtime_workers() == 0) {
        sched_yield();
    }
    pthread_t submitter;
    CHECK(pthread_create(&submitter, NULL, external_submitter_main, NULL) == 0);
    CHECK(pthread_join(submitter, NULL) == 0);
    while (atomic_load(&g_counter) != 1) {
        sched_yield();
    }
    atomic_store(&g_stop, 1);
    CHECK(pthread_join(rt, NULL) == 0);
    CHECK(atomic_load(&g_rc2) == MT_OK);
    finish_runtime();

    assert_core_counters_balanced();
    assert_io_counters_balanced();
}

static void test_documentation_contracts(void) {
    require_file_contains("README.md", "fd/socket readiness");
    require_file_contains("README.md", "blocking OS I/O");
    require_file_contains("README.md", "mt_fd_close");
    require_file_contains("README.md", "MT_FORCE_POLL_BACKEND");
    require_file_contains("README.md", "mt_task_status_name");
    require_file_contains("README.md", "mt_last_os_error");
    require_file_contains("README.md", "microthread_debug.h");
    require_file_contains("README.md", "restores the descriptor flags captured");
    require_file_contains("README.md", "microthread_testing.h");
    require_file_contains("README.md", "not an HTTP framework");
    require_file_contains("examples/echo_server.c", "mt_net_accept");
    require_file_contains("examples/echo_server.c", "mt_net_read");
    require_file_contains("examples/echo_server.c", "mt_net_write");
}

int main(void) {
    const char *backend_at_start;
    CHECK(mt_init() == MT_OK);
    backend_at_start = mt_io_backend_name();
    mt_shutdown();
    test_backend_lifecycle_and_reporting();
    test_nonblocking_validation_and_immediate_ready();
    test_combined_fd_events_and_hangup();
    test_read_wait_wakeup_productivity_close_and_timeout();
    test_write_wait_wakeup_and_timeout();
    test_fd_read_write_wrappers_and_large_transfer();
    test_cancellation_duplicate_waiter_join_and_integration();
    test_tcp_accept_and_net_wrappers();
    test_fd_reuse_and_readiness_races();
    test_close_reuse_shutdown_faults_and_stress();
    test_documentation_contracts();
    printf("v0.7 fd/socket io tests passed (%s backend)\n", backend_at_start);
    return 0;
}

#else
int main(void) {
    CHECK(strcmp(mt_io_backend_name(), "unsupported") == 0 ||
          strcmp(mt_io_backend_name(), "none") == 0);
    CHECK(mt_fd_set_nonblocking(-1) == MT_ERR_INVALID);
    int ready = 0;
    CHECK(mt_fd_wait(-1, MT_FD_READ, 0, &ready) == MT_ERR_INVALID);
    CHECK(mt_fd_wait(0, MT_FD_READ, 0, &ready) == MT_ERR_STATE);
    CHECK(mt_fd_read(-1, NULL, 0, 0) == MT_ERR_INVALID);
    CHECK(mt_fd_write(-1, NULL, 0, 0) == MT_ERR_INVALID);
    CHECK(mt_fd_close(-1) == MT_ERR_INVALID);
    printf("v0.7 fd/socket io unsupported-platform tests passed\n");
    return 0;
}
#endif

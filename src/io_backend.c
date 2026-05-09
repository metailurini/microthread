/* Internal I/O backend and fd-waiter implementation.
 * This file is included by microthread.c so it can share the private runtime
 * structs while keeping backend code out of the main scheduler file.
 */

#if !defined(_WIN32)
#define MT_IO_WAKE_SENTINEL (-1)

static mt_fd_waiter_t *mt_fd_find_waiter(int fd, int ready_events);
static void mt_io_drain_wake_pipe(void);

#include "io_backend_poll.c"
#include "io_backend_epoll.c"
#include "io_backend_kqueue.c"

static uint64_t mt_fd_generation_current(int fd) {
    for (mt_fd_generation_t *g = g_rt.fd_generations; g; g = g->next) {
        if (g->fd == fd) {
            return g->generation;
        }
    }
    mt_fd_generation_t *g = (mt_fd_generation_t *)calloc(1, sizeof(*g));
    if (!g) {
        return 0;
    }
    g->fd = fd;
    g->generation = 1;
    g->next = g_rt.fd_generations;
    g_rt.fd_generations = g;
    return g->generation;
}

static mt_fd_generation_t *mt_fd_generation_find(int fd) {
    for (mt_fd_generation_t *g = g_rt.fd_generations; g; g = g->next) {
        if (g->fd == fd) {
            return g;
        }
    }
    return NULL;
}

static int mt_fd_is_closing(int fd) {
    mt_fd_generation_t *g = mt_fd_generation_find(fd);
    return g && g->closing;
}

static int mt_fd_set_closing(int fd, int closing) {
    mt_fd_generation_t *g = mt_fd_generation_find(fd);
    if (!g) {
        g = (mt_fd_generation_t *)calloc(1, sizeof(*g));
        if (!g) {
            return MT_ERR_NOMEM;
        }
        g->fd = fd;
        g->generation = 1;
        g->next = g_rt.fd_generations;
        g_rt.fd_generations = g;
    }
    g->closing = closing;
    return MT_OK;
}

static void mt_fd_generation_bump(int fd) {
    for (mt_fd_generation_t *g = g_rt.fd_generations; g; g = g->next) {
        if (g->fd == fd) {
            g->generation++;
            if (g->generation == 0) {
                g->generation = 1;
            }
            return;
        }
    }
    mt_fd_generation_t *g = (mt_fd_generation_t *)calloc(1, sizeof(*g));
    if (!g) {
        return;
    }
    g->fd = fd;
    g->generation = 2;
    g->next = g_rt.fd_generations;
    g_rt.fd_generations = g;
}

static int mt_fd_generation_remove(int fd) {
    mt_fd_generation_t **link = &g_rt.fd_generations;
    while (*link) {
        if ((*link)->fd == fd) {
            mt_fd_generation_t *old = *link;
            *link = old->next;
            free(old);
            return 1;
        }
        link = &(*link)->next;
    }
    return 0;
}

static void mt_io_drain_wake_pipe(void) {
    if (g_rt.io_wake_read_fd < 0) {
        return;
    }
    char buf[128];
    for (;;) {
        ssize_t n = read(g_rt.io_wake_read_fd, buf, sizeof(buf));
        if (n > 0) {
            continue;
        }
        if (n < 0 && errno == EINTR) {
            continue;
        }
        if (n < 0) {
            mt_set_last_os_error(errno);
        }
        break;
    }
}

static void mt_io_backend_wake(void) {
    if (g_rt.io_wake_write_fd < 0) {
        return;
    }
    const char b = 1;
    ssize_t n;
    do {
        n = write(g_rt.io_wake_write_fd, &b, 1);
    } while (n < 0 && errno == EINTR);
    if (n < 0) {
        mt_set_last_os_error(errno);
    }
}

static int mt_io_make_wake_pipe(void) {
    int fds[2] = {-1, -1};
    if (pipe(fds) != 0) {
        mt_set_last_os_error(errno);
        return MT_ERR_BACKEND;
    }
    if (mt_fd_set_nonblocking(fds[0]) != MT_OK || mt_fd_set_nonblocking(fds[1]) != MT_OK) {
        int saved = errno;
        close(fds[0]);
        close(fds[1]);
        mt_set_last_os_error(saved);
        return MT_ERR_BACKEND;
    }
    g_rt.io_wake_read_fd = fds[0];
    g_rt.io_wake_write_fd = fds[1];
    return MT_OK;
}

static int mt_io_backend_init(void) {
#ifdef MT_TESTING
    if (g_fail_next_io_backend_init) {
        g_fail_next_io_backend_init = 0;
        return MT_ERR_BACKEND;
    }
    MT_TEST_COUNTER_INC(g_io_backend_inits);
#endif
    g_rt.io_backend_fd = -1;
    g_rt.io_wake_read_fd = -1;
    g_rt.io_wake_write_fd = -1;
    g_rt.io_backend_kind = MT_IO_BACKEND_POLL;
    g_rt.io_polling = 0;

    int rc = mt_io_make_wake_pipe();
    if (rc != MT_OK) {
        return rc;
    }

#if defined(MT_HAVE_EPOLL)
    rc = mt_epoll_backend_init();
    if (rc != MT_OK) {
        return rc;
    }
#elif defined(MT_HAVE_KQUEUE)
    rc = mt_kqueue_backend_init();
    if (rc != MT_OK) {
        return rc;
    }
#endif
    return MT_OK;
}

static void mt_io_backend_shutdown(void) {
#ifdef MT_TESTING
    if (g_rt.io_backend_kind != MT_IO_BACKEND_NONE || g_rt.io_backend_fd >= 0 ||
        g_rt.io_wake_read_fd >= 0 || g_rt.io_wake_write_fd >= 0) {
        MT_TEST_COUNTER_INC(g_io_backend_shutdowns);
    }
#endif
    if (g_rt.io_backend_fd >= 0) {
        close(g_rt.io_backend_fd);
        g_rt.io_backend_fd = -1;
    }
    if (g_rt.io_wake_read_fd >= 0) {
        close(g_rt.io_wake_read_fd);
        g_rt.io_wake_read_fd = -1;
    }
    if (g_rt.io_wake_write_fd >= 0) {
        close(g_rt.io_wake_write_fd);
        g_rt.io_wake_write_fd = -1;
    }
    mt_fd_generation_t *gen = g_rt.fd_generations;
    while (gen) {
        mt_fd_generation_t *next = gen->next;
        free(gen);
        gen = next;
    }
    g_rt.fd_generations = NULL;
    g_rt.io_backend_kind = MT_IO_BACKEND_NONE;
    g_rt.io_polling = 0;
}

static const char *mt_io_backend_name_locked(void) {
    switch (g_rt.io_backend_kind) {
        case MT_IO_BACKEND_EPOLL:
            return "epoll";
        case MT_IO_BACKEND_KQUEUE:
            return "kqueue";
        case MT_IO_BACKEND_POLL:
            return "poll";
        case MT_IO_BACKEND_NONE:
        default:
            return "none";
    }
}

static int mt_io_backend_add(mt_fd_waiter_t *waiter) {
    if (!waiter) {
        return MT_ERR_INVALID;
    }
#ifdef MT_TESTING
    if (g_fail_next_io_backend_register) {
        g_fail_next_io_backend_register = 0;
        return MT_ERR_BACKEND;
    }
#endif
    int rc = MT_OK;
    if (g_rt.io_backend_kind == MT_IO_BACKEND_POLL) {
        rc = MT_OK;
#if defined(MT_HAVE_EPOLL)
    } else if (g_rt.io_backend_kind == MT_IO_BACKEND_EPOLL) {
        rc = mt_epoll_backend_add(waiter);
#endif
#if defined(MT_HAVE_KQUEUE)
    } else if (g_rt.io_backend_kind == MT_IO_BACKEND_KQUEUE) {
        rc = mt_kqueue_backend_add(waiter);
#endif
    }
#ifdef MT_TESTING
    if (rc == MT_OK) {
        MT_TEST_COUNTER_INC(g_io_backend_registers);
    }
#endif
    return rc;
}

static void mt_io_backend_remove(mt_fd_waiter_t *waiter) {
    if (!waiter) {
        return;
    }
#ifdef MT_TESTING
    MT_TEST_COUNTER_INC(g_io_backend_unregisters);
    if (g_fail_next_io_backend_unregister) {
        g_fail_next_io_backend_unregister = 0;
        return;
    }
#endif
    if (g_rt.io_backend_kind == MT_IO_BACKEND_POLL || g_rt.io_backend_fd < 0) {
        return;
    }
#if defined(MT_HAVE_EPOLL)
    if (g_rt.io_backend_kind == MT_IO_BACKEND_EPOLL) {
        mt_epoll_backend_remove(waiter);
        return;
    }
#endif
#if defined(MT_HAVE_KQUEUE)
    if (g_rt.io_backend_kind == MT_IO_BACKEND_KQUEUE) {
        mt_kqueue_backend_remove(waiter);
    }
#endif
}

static mt_fd_waiter_t *mt_fd_find_waiter(int fd, int ready_events) {
    for (mt_fd_waiter_t *w = g_rt.fd_waiters; w; w = w->next) {
        if (w->active && w->fd == fd && (w->events & ready_events) != 0) {
            if (w->generation == mt_fd_generation_current(fd)) {
                return w;
            }
            mt_fd_ready_waiter(w, MT_ERR_CLOSED, 0);
            return NULL;
        }
    }
    return NULL;
}

static int mt_fd_waiter_conflicts(int fd, int events) {
    (void)events;
    for (mt_fd_waiter_t *w = g_rt.fd_waiters; w; w = w->next) {
        if (w->active && w->fd == fd) {
            return 1;
        }
    }
    return 0;
}

static int mt_fd_waiter_add(mt_fd_waiter_t *waiter) {
    int rc = mt_io_backend_add(waiter);
    if (rc != MT_OK) {
        return rc;
    }
    waiter->active = 1;
    waiter->next = g_rt.fd_waiters;
    g_rt.fd_waiters = waiter;
    g_rt.fd_waiting_count++;
    mt_notify_all();
    return MT_OK;
}

static int mt_fd_waiter_remove(mt_fd_waiter_t *waiter) {
    if (!waiter || !waiter->active) {
        return 0;
    }
    mt_fd_waiter_t **link = &g_rt.fd_waiters;
    while (*link) {
        if (*link == waiter) {
            *link = waiter->next;
            waiter->next = NULL;
            waiter->active = 0;
            mt_io_backend_remove(waiter);
            if (g_rt.fd_waiting_count > 0) {
                g_rt.fd_waiting_count--;
            }
            mt_notify_all();
            return 1;
        }
        link = &(*link)->next;
    }
    waiter->active = 0;
    return 0;
}

static void mt_fd_free_waiter(mt_fd_waiter_t *waiter) {
    mt_free_fd_waiter(waiter);
}

static void mt_fd_ready_waiter(mt_fd_waiter_t *waiter, int result, int ready_events) {
    if (!waiter || !waiter->task) {
        return;
    }
    mt_task_t *task = waiter->task;
    mt_fd_waiter_remove(waiter);
    if (task->fd_in_timer) {
        mt_timer_remove(task);
        task->fd_in_timer = 0;
    }
    task->fd_result = result;
    task->fd_ready_events = ready_events;
    if (task->state == MT_TASK_WAITING_FD) {
        task->state = MT_TASK_READY;
        mt_runq_push(task);
    }
}

static void mt_fd_timeout_ready(mt_task_t *task) {
    if (!task) {
        return;
    }
    task->fd_in_timer = 0;
    mt_fd_waiter_t *waiter = task->fd_waiter;
    if (waiter) {
        mt_fd_waiter_remove(waiter);
    }
    task->fd_result = MT_ERR_TIMEOUT;
    task->fd_ready_events = 0;
    task->state = MT_TASK_READY;
    mt_runq_push(task);
}

static void mt_backend_fd_waiters_locked(int timeout_ms) {
    if (g_rt.io_backend_kind == MT_IO_BACKEND_POLL) {
        mt_poll_backend_wait_locked(timeout_ms);
        return;
    }
#if defined(MT_HAVE_EPOLL)
    if (g_rt.io_backend_kind == MT_IO_BACKEND_EPOLL) {
        mt_epoll_backend_wait_locked(timeout_ms);
        return;
    }
#endif
#if defined(MT_HAVE_KQUEUE)
    if (g_rt.io_backend_kind == MT_IO_BACKEND_KQUEUE) {
        mt_kqueue_backend_wait_locked(timeout_ms);
        return;
    }
#endif
}

static void mt_poll_fd_waiters_with_timeout(int timeout_ms) {
    if (g_rt.fd_waiting_count == 0 && g_rt.io_wake_read_fd < 0) {
        return;
    }
    if (g_rt.io_polling) {
#if MT_HAS_OS_THREADS
        if (timeout_ms > 0) {
            mt_cond_timedwait_ns((uint64_t)timeout_ms * MT_NS_PER_MS);
        } else {
            pthread_cond_wait(&g_rt.cond, &g_rt.lock);
        }
#endif
        return;
    }
    g_rt.io_polling = 1;
    mt_backend_fd_waiters_locked(timeout_ms);
    g_rt.io_polling = 0;
    mt_notify_all();
}

static void mt_poll_fd_waiters_once(uint64_t now_ns) {
    (void)now_ns;
    if (g_rt.fd_waiting_count > 0) {
        mt_poll_fd_waiters_with_timeout(0);
    }
}

static void mt_fd_wake_all(int result) {
    mt_fd_waiter_t *w = g_rt.fd_waiters;
    while (w) {
        mt_fd_waiter_t *next = w->next;
        if (w->active) {
            mt_fd_ready_waiter(w, result, 0);
        }
        w = next;
    }
}

static void mt_fd_wake_for_close(int fd) {
    mt_fd_generation_bump(fd);
    mt_fd_waiter_t *w = g_rt.fd_waiters;
    while (w) {
        mt_fd_waiter_t *next = w->next;
        if (w->active && w->fd == fd) {
            mt_fd_ready_waiter(w, MT_ERR_CLOSED, 0);
        }
        w = next;
    }
}
#else
static void mt_io_backend_wake(void) {
}

static int mt_io_backend_init(void) {
    return MT_OK;
}

static void mt_io_backend_shutdown(void) {
}

static const char *mt_io_backend_name_locked(void) {
    return "unsupported";
}

static void mt_fd_timeout_ready(mt_task_t *task) {
    if (task) {
        task->state = MT_TASK_READY;
        mt_runq_push(task);
    }
}

static void mt_poll_fd_waiters_once(uint64_t now_ns) {
    (void)now_ns;
}

static void mt_poll_fd_waiters_with_timeout(int timeout_ms) {
    (void)timeout_ms;
}

static void mt_fd_wake_all(int result) {
    mt_fd_waiter_t *w = g_rt.fd_waiters;
    while (w) {
        mt_fd_waiter_t *next = w->next;
        if (w->active) {
            mt_fd_ready_waiter(w, result, 0);
        }
        w = next;
    }
}

static void mt_fd_wake_for_close(int fd) {
    (void)fd;
}
#endif

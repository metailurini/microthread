#include "microthread.h"
#include "io_backend.h"

#ifdef MICROTHREAD_EMBEDDED_IMPL

#if defined(MT_HAVE_EPOLL)
static int mt_epoll_events_to_fd_events(int events) {
    int out = 0;
    if (events & (EPOLLIN | EPOLLHUP | EPOLLERR)) {
        out |= MT_FD_READ;
    }
    if (events & (EPOLLOUT | EPOLLHUP | EPOLLERR)) {
        out |= MT_FD_WRITE;
    }
    return out;
}

static int mt_epoll_backend_init(void) {
    g_rt.io_backend_fd = epoll_create1(EPOLL_CLOEXEC);
    if (g_rt.io_backend_fd < 0) {
        mt_set_last_os_error(errno);
        return MT_ERR_BACKEND;
    }
    g_rt.io_backend_kind = MT_IO_BACKEND_EPOLL;
    struct epoll_event ev;
    memset(&ev, 0, sizeof(ev));
    ev.events = EPOLLIN;
    ev.data.fd = MT_IO_WAKE_SENTINEL;
    if (epoll_ctl(g_rt.io_backend_fd, EPOLL_CTL_ADD, g_rt.io_wake_read_fd, &ev) != 0) {
        mt_set_last_os_error(errno);
        return MT_ERR_BACKEND;
    }
    return MT_OK;
}

static int mt_epoll_backend_add(mt_fd_waiter_t *waiter) {
    struct epoll_event ev;
    memset(&ev, 0, sizeof(ev));
    ev.events = EPOLLERR | EPOLLHUP;
    if (waiter->events & MT_FD_READ) {
        ev.events |= EPOLLIN;
    }
    if (waiter->events & MT_FD_WRITE) {
        ev.events |= EPOLLOUT;
    }
    ev.data.fd = waiter->fd;
    if (epoll_ctl(g_rt.io_backend_fd, EPOLL_CTL_ADD, waiter->fd, &ev) != 0) {
        mt_set_last_os_error(errno);
        return errno == EBADF ? MT_ERR_INVALID : MT_ERR_BACKEND;
    }
    return MT_OK;
}

static void mt_epoll_backend_remove(mt_fd_waiter_t *waiter) {
    if (epoll_ctl(g_rt.io_backend_fd, EPOLL_CTL_DEL, waiter->fd, NULL) != 0) {
        mt_set_last_os_error(errno);
    }
}

static void mt_epoll_backend_wait_locked(int timeout_ms) {
    struct epoll_event events[64];
    mt_unlock();
    int nready;
    do {
        nready = epoll_wait(g_rt.io_backend_fd, events, 64, timeout_ms);
    } while (nready < 0 && errno == EINTR);
    if (nready < 0) {
        mt_set_last_os_error(errno);
    }
    mt_lock();
    if (nready > 0) {
        for (int i = 0; i < nready; ++i) {
            if (events[i].data.fd == MT_IO_WAKE_SENTINEL) {
                mt_io_drain_wake_pipe();
                continue;
            }
            int ready_events = mt_epoll_events_to_fd_events((int)events[i].events);
            mt_fd_waiter_t *w = mt_fd_find_waiter(events[i].data.fd, ready_events);
            if (!w) {
                continue;
            }
            ready_events &= w->events;
            if (ready_events == 0 && (events[i].events & (EPOLLERR | EPOLLHUP))) {
                ready_events = w->events;
            }
            mt_fd_ready_waiter(w, MT_OK, ready_events);
        }
    }
}
#endif

#endif /* MICROTHREAD_EMBEDDED_IMPL */

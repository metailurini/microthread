#include "runtime_internal.h"
#include "status_internal.h"
#include "io_backend.h"


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

int mt_epoll_backend_init(void) {
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

static uint32_t mt_epoll_events_from_fd_events(int events) {
    uint32_t out = EPOLLERR | EPOLLHUP;
    if (events & MT_FD_READ) {
        out |= EPOLLIN;
    }
    if (events & MT_FD_WRITE) {
        out |= EPOLLOUT;
    }
    return out;
}

int mt_epoll_backend_add(mt_fd_waiter_t *waiter) {
    int previous = mt_fd_active_events_for_fd(waiter->fd, NULL, 0);
    int combined = previous | waiter->events;
    struct epoll_event ev;
    memset(&ev, 0, sizeof(ev));
    ev.events = mt_epoll_events_from_fd_events(combined);
    ev.data.fd = waiter->fd;
    int op = previous != 0 ? EPOLL_CTL_MOD : EPOLL_CTL_ADD;
    if (epoll_ctl(g_rt.io_backend_fd, op, waiter->fd, &ev) != 0) {
        mt_set_last_os_error(errno);
        return errno == EBADF ? MT_ERR_INVALID : MT_ERR_BACKEND;
    }
    return MT_OK;
}

void mt_epoll_backend_remove(mt_fd_waiter_t *waiter) {
    int remaining = mt_fd_active_events_for_fd(waiter->fd, NULL, 0);
    if (remaining != 0) {
        struct epoll_event ev;
        memset(&ev, 0, sizeof(ev));
        ev.events = mt_epoll_events_from_fd_events(remaining);
        ev.data.fd = waiter->fd;
        if (epoll_ctl(g_rt.io_backend_fd, EPOLL_CTL_MOD, waiter->fd, &ev) != 0) {
            mt_set_last_os_error(errno);
        }
        return;
    }
    if (epoll_ctl(g_rt.io_backend_fd, EPOLL_CTL_DEL, waiter->fd, NULL) != 0) {
        mt_set_last_os_error(errno);
    }
}

void mt_epoll_backend_wait_locked(int timeout_ms) {
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
            int remaining = ready_events;
            while (remaining != 0) {
                mt_fd_waiter_t *w = mt_fd_find_waiter(events[i].data.fd, remaining);
                if (!w) {
                    break;
                }
                int wready = remaining & w->events;
                if (wready == 0 && (events[i].events & (EPOLLERR | EPOLLHUP))) {
                    wready = w->events;
                }
                remaining &= ~wready;
                mt_fd_ready_waiter(w, MT_OK, wready);
            }
        }
    }
}
#endif


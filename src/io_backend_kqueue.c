#include "runtime_internal.h"
#include "io_backend.h"


#if defined(MT_HAVE_KQUEUE)
int mt_kqueue_backend_init(void) {
    g_rt.io_backend_fd = kqueue();
    if (g_rt.io_backend_fd < 0) {
        mt_set_last_os_error(errno);
        return MT_ERR_BACKEND;
    }
    g_rt.io_backend_kind = MT_IO_BACKEND_KQUEUE;
    struct kevent ev;
    EV_SET(&ev, (uintptr_t)g_rt.io_wake_read_fd, EVFILT_READ, EV_ADD | EV_CLEAR, 0, 0, NULL);
    if (kevent(g_rt.io_backend_fd, &ev, 1, NULL, 0, NULL) != 0) {
        mt_set_last_os_error(errno);
        return MT_ERR_BACKEND;
    }
    return MT_OK;
}

int mt_kqueue_backend_add(mt_fd_waiter_t *waiter) {
    struct kevent evs[2];
    int n = 0;
    if (waiter->events & MT_FD_READ) {
        EV_SET(&evs[n++], (uintptr_t)waiter->fd, EVFILT_READ, EV_ADD | EV_ENABLE | EV_CLEAR, 0, 0, NULL);
    }
    if (waiter->events & MT_FD_WRITE) {
        EV_SET(&evs[n++], (uintptr_t)waiter->fd, EVFILT_WRITE, EV_ADD | EV_ENABLE | EV_CLEAR, 0, 0, NULL);
    }
    if (kevent(g_rt.io_backend_fd, evs, n, NULL, 0, NULL) != 0) {
        mt_set_last_os_error(errno);
        return errno == EBADF ? MT_ERR_INVALID : MT_ERR_BACKEND;
    }
    return MT_OK;
}

void mt_kqueue_backend_remove(mt_fd_waiter_t *waiter) {
    struct kevent evs[2];
    int n = 0;
    if (waiter->events & MT_FD_READ) {
        EV_SET(&evs[n++], (uintptr_t)waiter->fd, EVFILT_READ, EV_DELETE, 0, 0, NULL);
    }
    if (waiter->events & MT_FD_WRITE) {
        EV_SET(&evs[n++], (uintptr_t)waiter->fd, EVFILT_WRITE, EV_DELETE, 0, 0, NULL);
    }
    if (n > 0 && kevent(g_rt.io_backend_fd, evs, n, NULL, 0, NULL) != 0) {
        mt_set_last_os_error(errno);
    }
}

void mt_kqueue_backend_wait_locked(int timeout_ms) {
    struct kevent events[64];
    struct timespec ts;
    struct timespec *tsp = NULL;
    if (timeout_ms >= 0) {
        ts.tv_sec = timeout_ms / 1000;
        ts.tv_nsec = (long)(timeout_ms % 1000) * 1000000L;
        tsp = &ts;
    }
    mt_unlock();
    int nready;
    do {
        nready = kevent(g_rt.io_backend_fd, NULL, 0, events, 64, tsp);
    } while (nready < 0 && errno == EINTR);
    if (nready < 0) {
        mt_set_last_os_error(errno);
    }
    mt_lock();
    if (nready > 0) {
        for (int i = 0; i < nready; ++i) {
            int fd = (int)events[i].ident;
            if (fd == g_rt.io_wake_read_fd) {
                mt_io_drain_wake_pipe();
                continue;
            }
            int ready_events = 0;
            if (events[i].filter == EVFILT_READ) {
                ready_events |= MT_FD_READ;
            } else if (events[i].filter == EVFILT_WRITE) {
                ready_events |= MT_FD_WRITE;
            }
            if (events[i].flags & (EV_EOF | EV_ERROR)) {
                ready_events = MT_FD_READ | MT_FD_WRITE;
            }
            mt_fd_waiter_t *w = mt_fd_find_waiter(fd, ready_events);
            if (!w) {
                continue;
            }
            ready_events &= w->events;
            if (ready_events == 0) {
                ready_events = w->events;
            }
            mt_fd_ready_waiter(w, MT_OK, ready_events);
        }
    }
}
#endif


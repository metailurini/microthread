#include "runtime_internal.h"
#include "status_internal.h"
#include "io_backend.h"


short mt_fd_events_to_poll(int events) {
    short pevents = 0;
    if (events & MT_FD_READ) {
        pevents |= POLLIN;
    }
    if (events & MT_FD_WRITE) {
        pevents |= POLLOUT;
    }
    return pevents;
}


int mt_poll_revents_to_fd_events(short revents) {
    int events = 0;
    if (revents & (POLLIN | POLLHUP | POLLERR)) {
        events |= MT_FD_READ;
    }
    if (revents & (POLLOUT | POLLHUP | POLLERR)) {
        events |= MT_FD_WRITE;
    }
    return events;
}

void mt_poll_backend_wait_locked(int timeout_ms) {
    size_t count = 1;
    for (mt_fd_waiter_t *w = g_rt.fd_waiters; w; w = w->next) {
        if (w->state == MT_FD_WAITER_ACTIVE) {
            count++;
        }
    }
    struct pollfd *pfds = (struct pollfd *)calloc(count, sizeof(*pfds));
    if (!pfds) {
        return;
    }
    size_t i = 0;
    pfds[i].fd = g_rt.io_wake_read_fd;
    pfds[i].events = POLLIN;
    pfds[i].revents = 0;
    i++;
    for (mt_fd_waiter_t *w = g_rt.fd_waiters; w; w = w->next) {
        if (w->state != MT_FD_WAITER_ACTIVE) {
            continue;
        }
        pfds[i].fd = w->fd;
        pfds[i].events = mt_fd_events_to_poll(w->events) | POLLERR | POLLHUP;
        pfds[i].revents = 0;
        i++;
    }
    count = i;

    mt_unlock();
    int nready;
    do {
        nready = poll(pfds, count, timeout_ms);
    } while (nready < 0 && errno == EINTR);
    if (nready < 0) {
        mt_set_last_os_error(errno);
    }
    mt_lock();

    if (nready > 0) {
        for (i = 0; i < count; ++i) {
            if (pfds[i].revents == 0) {
                continue;
            }
            if (pfds[i].fd == g_rt.io_wake_read_fd) {
                mt_io_drain_wake_pipe();
                continue;
            }
            if (pfds[i].revents & POLLNVAL) {
                mt_fd_waiter_t *w;
                while ((w = mt_fd_find_waiter(pfds[i].fd, MT_FD_READ | MT_FD_WRITE)) != NULL) {
                    mt_fd_ready_waiter(w, MT_ERR_INVALID, 0);
                }
                continue;
            }
            int ready_events = mt_poll_revents_to_fd_events(pfds[i].revents);
            mt_fd_waiter_t *w = mt_fd_find_waiter(pfds[i].fd, ready_events);
            if (!w) {
                continue;
            }
            ready_events &= w->events;
            if (ready_events == 0 && (pfds[i].revents & (POLLERR | POLLHUP))) {
                ready_events = w->events;
            }
            mt_fd_ready_waiter(w, MT_OK, ready_events);
        }
    }
    free(pfds);
}


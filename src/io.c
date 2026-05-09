/* Internal fd/socket public API implementation. */

#include "runtime_internal.h"
#include "status_internal.h"
#include "io_backend.h"

#if !defined(_WIN32)
static int mt_fd_validate_events(int events) {
    return events != 0 && (events & ~(MT_FD_READ | MT_FD_WRITE)) == 0;
}

static uint64_t mt_deadline_from_timeout(uint64_t timeout_ms) {
    uint64_t now_ns = mt_now_ns();
    uint64_t timeout_ns = timeout_ms > (UINT64_MAX / MT_NS_PER_MS)
        ? UINT64_MAX
        : timeout_ms * MT_NS_PER_MS;
    return UINT64_MAX - now_ns < timeout_ns ? UINT64_MAX : now_ns + timeout_ns;
}

static uint64_t mt_timeout_left_ms(uint64_t deadline_ns) {
    uint64_t now_ns = mt_now_ns();
    if (deadline_ns <= now_ns) {
        return 0;
    }
    uint64_t left_ns = deadline_ns - now_ns;
    return (left_ns + MT_NS_PER_MS - 1u) / MT_NS_PER_MS;
}

static int mt_io_error_from_errno(int invalid_if_ebadf) {
    int err = errno;
    mt_set_last_os_error(err);
    if (invalid_if_ebadf && err == EBADF) {
        return MT_ERR_INVALID;
    }
    return MT_ERR_IO;
}

int mt_fd_set_nonblocking(int fd) {
    if (fd < 0) {
        return MT_ERR_INVALID;
    }
#if defined(_WIN32)
    (void)fd;
    return MT_ERR_STATE;
#else
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0) {
        return mt_io_error_from_errno(1);
    }
    if ((flags & O_NONBLOCK) != 0) {
        return MT_OK;
    }
    if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) != 0) {
        return mt_io_error_from_errno(1);
    }
    return MT_OK;
#endif

}

int mt_fd_adopt(int fd) {
    if (fd < 0) {
        return MT_ERR_INVALID;
    }
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0) {
        return mt_io_error_from_errno(1);
    }
    if ((flags & O_NONBLOCK) == 0 && fcntl(fd, F_SETFL, flags | O_NONBLOCK) != 0) {
        return mt_io_error_from_errno(1);
    }
    mt_lock();
    if (mt_fd_is_closing(fd)) {
        mt_unlock();
        return MT_ERR_CLOSED;
    }
    uint64_t gen = mt_fd_generation_current(fd);
    if (gen == 0) {
        mt_unlock();
        return MT_ERR_NOMEM;
    }
    mt_fd_generation_t *entry = mt_fd_generation_find(fd);
    if (entry && !entry->adopted) {
        entry->adopted = 1;
        entry->original_flags = flags;
    }
    mt_unlock();
    return MT_OK;
}

int mt_fd_release(int fd) {
    if (fd < 0) {
        return MT_ERR_INVALID;
    }
    int restore_flags = -1;
    mt_lock();
    if (mt_fd_is_closing(fd) || mt_fd_waiter_conflicts(fd, MT_FD_READ | MT_FD_WRITE)) {
        mt_unlock();
        return MT_ERR_STATE;
    }
    mt_fd_generation_t *entry = mt_fd_generation_find(fd);
    if (entry && entry->adopted) {
        restore_flags = entry->original_flags;
    }
    (void)mt_fd_generation_remove(fd);
    mt_unlock();
    if (restore_flags >= 0 && fcntl(fd, F_SETFL, restore_flags) != 0) {
        return mt_io_error_from_errno(1);
    }
    return MT_OK;
}

const char *mt_io_backend_name(void) {
    if (!g_rt.initialized) {
        return "none";
    }
    mt_lock();
    const char *name = mt_io_backend_name_locked();
    mt_unlock();
    return name;
}

int mt_fd_wait(int fd, int events, uint64_t timeout_ms, int *ready_events) {
    if (ready_events) {
        *ready_events = 0;
    }
    if (fd < 0 || !mt_fd_validate_events(events) || !ready_events) {
        return MT_ERR_INVALID;
    }
    mt_task_t *task = mt_current_task();
    if (!task) {
        return MT_ERR_STATE;
    }

    mt_lock();
    if (mt_fd_is_closing(fd)) {
        mt_unlock();
        return MT_ERR_CLOSED;
    }
    if (mt_fd_waiter_conflicts(fd, events)) {
        mt_unlock();
        return MT_ERR_STATE;
    }

    struct pollfd pfd;
    pfd.fd = fd;
    pfd.events = mt_fd_events_to_poll(events) | POLLERR | POLLHUP;
    pfd.revents = 0;
    int nready;
    do {
        nready = poll(&pfd, 1, 0);
    } while (nready < 0 && errno == EINTR);
    if (nready < 0) {
        mt_unlock();
        return mt_io_error_from_errno(1);
    }
    if (nready > 0) {
        *ready_events = mt_poll_revents_to_fd_events(pfd.revents) & events;
        if (pfd.revents & POLLNVAL) {
            mt_unlock();
            return MT_ERR_INVALID;
        }
        if (*ready_events == 0 && (pfd.revents & (POLLERR | POLLHUP))) {
            *ready_events = events;
        }
        mt_unlock();
        return MT_OK;
    }
    if (timeout_ms == 0) {
        mt_unlock();
        return MT_ERR_TIMEOUT;
    }

    mt_fd_waiter_t *waiter = mt_alloc_fd_waiter();
    if (!waiter) {
        mt_unlock();
        return MT_ERR_NOMEM;
    }
    waiter->task = task;
    waiter->fd = fd;
    waiter->events = events;
    waiter->generation = mt_fd_generation_current(fd);
    task->fd_waiter = waiter;
    task->fd_result = MT_OK;
    task->fd_ready_events = 0;
    task->fd_in_timer = 0;
    int add_rc = mt_fd_waiter_add(waiter);
    if (add_rc != MT_OK) {
        task->fd_waiter = NULL;
        mt_fd_free_waiter(waiter);
        mt_unlock();
        return add_rc;
    }

    uint64_t deadline_ns = mt_deadline_from_timeout(timeout_ms);
    if (mt_timer_push_state(task, deadline_ns, MT_TASK_WAITING_FD) != MT_OK) {
        mt_fd_waiter_remove(waiter);
        task->fd_waiter = NULL;
        mt_fd_free_waiter(waiter);
        mt_unlock();
        return MT_ERR_NOMEM;
    }
    task->fd_in_timer = 1;
    mt_ctx_switch(&task->ctx, mt_current_scheduler_ctx());

    int rc = task->fd_result;
    if (rc == MT_OK) {
        *ready_events = task->fd_ready_events;
    }
    if (task->fd_waiter == waiter) {
        task->fd_waiter = NULL;
    }
    mt_fd_free_waiter(waiter);
    return rc;
}

int mt_fd_wait_read(int fd, uint64_t timeout_ms) {
    int ready = 0;
    return mt_fd_wait(fd, MT_FD_READ, timeout_ms, &ready);
}

int mt_fd_wait_write(int fd, uint64_t timeout_ms) {
    int ready = 0;
    return mt_fd_wait(fd, MT_FD_WRITE, timeout_ms, &ready);
}

ssize_t mt_fd_read(int fd, void *buf, size_t len, uint64_t timeout_ms) {
    if (fd < 0 || (!buf && len > 0)) {
        return MT_ERR_INVALID;
    }
    if (len == 0) {
        return 0;
    }
    int nb_rc = mt_fd_adopt(fd);
    if (nb_rc != MT_OK) {
        return nb_rc;
    }
    uint64_t deadline_ns = mt_deadline_from_timeout(timeout_ms);
    for (;;) {
        ssize_t n = read(fd, buf, len);
        if (n >= 0) {
            return n;
        }
        if (errno == EINTR) {
            continue;
        }
        if (errno != EAGAIN && errno != EWOULDBLOCK) {
            return mt_io_error_from_errno(1);
        }
        uint64_t left_ms = mt_timeout_left_ms(deadline_ns);
        int rc = mt_fd_wait_read(fd, left_ms);
        if (rc != MT_OK) {
            return rc;
        }
    }
}

ssize_t mt_fd_write(int fd, const void *buf, size_t len, uint64_t timeout_ms) {
    if (fd < 0 || (!buf && len > 0)) {
        return MT_ERR_INVALID;
    }
    if (len == 0) {
        return 0;
    }
    int nb_rc = mt_fd_adopt(fd);
    if (nb_rc != MT_OK) {
        return nb_rc;
    }
    uint64_t deadline_ns = mt_deadline_from_timeout(timeout_ms);
    const unsigned char *p = (const unsigned char *)buf;
    size_t total = 0;
    while (total < len) {
        ssize_t n = write(fd, p + total, len - total);
        if (n > 0) {
            total += (size_t)n;
            continue;
        }
        if (n == 0) {
            continue;
        }
        if (errno == EINTR) {
            continue;
        }
        if (errno != EAGAIN && errno != EWOULDBLOCK) {
            int rc = mt_io_error_from_errno(1);
            return total > 0 ? (ssize_t)total : rc;
        }
        uint64_t left_ms = mt_timeout_left_ms(deadline_ns);
        int rc = mt_fd_wait_write(fd, left_ms);
        if (rc != MT_OK) {
            return total > 0 ? (ssize_t)total : rc;
        }
    }
    return (ssize_t)total;
}

int mt_fd_close(int fd) {
    if (fd < 0) {
        return MT_ERR_INVALID;
    }
    mt_lock();
    if (mt_fd_is_closing(fd)) {
        mt_unlock();
        return MT_ERR_STATE;
    }
    if (mt_fd_set_closing(fd, 1) != MT_OK) {
        mt_unlock();
        return MT_ERR_NOMEM;
    }
    mt_fd_wake_for_close(fd);
    mt_unlock();
    int rc = close(fd) == 0 ? MT_OK : mt_io_error_from_errno(1);
    mt_lock();
    (void)mt_fd_set_closing(fd, 0);
    mt_unlock();
    return rc;
}

int mt_net_listen_tcp(const char *host, const char *port, int backlog) {
    if (!port || backlog < 0) {
        return MT_ERR_INVALID;
    }
    struct addrinfo hints;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;

    struct addrinfo *res = NULL;
    int gai = getaddrinfo(host, port, &hints, &res);
    if (gai != 0) {
        mt_set_last_os_error(gai);
        return MT_ERR_ADDRINFO;
    }

    int listen_fd = -1;
    for (struct addrinfo *ai = res; ai; ai = ai->ai_next) {
        int fd = socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
        if (fd < 0) {
            mt_set_last_os_error(errno);
            continue;
        }
        int yes = 1;
        (void)setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
        if (bind(fd, ai->ai_addr, ai->ai_addrlen) == 0 && listen(fd, backlog) == 0) {
            if (mt_fd_adopt(fd) == MT_OK) {
                listen_fd = fd;
                break;
            }
        }
        if (listen_fd < 0) {
            mt_set_last_os_error(errno);
        }
        close(fd);
    }
    freeaddrinfo(res);
    return listen_fd >= 0 ? listen_fd : MT_ERR_IO;
}

int mt_net_accept(int listen_fd, struct sockaddr *addr, socklen_t *addrlen,
                  uint64_t timeout_ms) {
    if (listen_fd < 0) {
        return MT_ERR_INVALID;
    }
    int nb_rc = mt_fd_adopt(listen_fd);
    if (nb_rc != MT_OK) {
        return nb_rc;
    }
    uint64_t deadline_ns = mt_deadline_from_timeout(timeout_ms);
    for (;;) {
        int fd = accept(listen_fd, addr, addrlen);
        if (fd >= 0) {
            if (mt_fd_adopt(fd) != MT_OK) {
                close(fd);
                return MT_ERR_IO;
            }
            return fd;
        }
        if (errno == EINTR) {
            continue;
        }
        if (errno != EAGAIN && errno != EWOULDBLOCK) {
            return mt_io_error_from_errno(1);
        }
        uint64_t left_ms = mt_timeout_left_ms(deadline_ns);
        int rc = mt_fd_wait_read(listen_fd, left_ms);
        if (rc != MT_OK) {
            return rc;
        }
    }
}

ssize_t mt_net_read(int fd, void *buf, size_t len, uint64_t timeout_ms) {
    return mt_fd_read(fd, buf, len, timeout_ms);
}

ssize_t mt_net_write(int fd, const void *buf, size_t len, uint64_t timeout_ms) {
    if (fd < 0 || (!buf && len > 0)) {
        return MT_ERR_INVALID;
    }
    if (len == 0) {
        return 0;
    }
    int nb_rc = mt_fd_adopt(fd);
    if (nb_rc != MT_OK) {
        return nb_rc;
    }
#if defined(SO_NOSIGPIPE)
    int one = 1;
    (void)setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof(one));
#endif
    uint64_t deadline_ns = mt_deadline_from_timeout(timeout_ms);
    const unsigned char *p = (const unsigned char *)buf;
    size_t total = 0;
    while (total < len) {
#if defined(MSG_NOSIGNAL)
        ssize_t n = send(fd, p + total, len - total, MSG_NOSIGNAL);
#else
        ssize_t n = send(fd, p + total, len - total, 0);
#endif
        if (n > 0) {
            total += (size_t)n;
            continue;
        }
        if (n == 0) {
            continue;
        }
        if (errno == EINTR) {
            continue;
        }
        if (errno != EAGAIN && errno != EWOULDBLOCK) {
            int rc = mt_io_error_from_errno(1);
            return total > 0 ? (ssize_t)total : rc;
        }
        uint64_t left_ms = mt_timeout_left_ms(deadline_ns);
        int rc = mt_fd_wait_write(fd, left_ms);
        if (rc != MT_OK) {
            return total > 0 ? (ssize_t)total : rc;
        }
    }
    return (ssize_t)total;
}

int mt_net_close(int fd) {
    return mt_fd_close(fd);
}
#else
int mt_fd_set_nonblocking(int fd) {
    return fd < 0 ? MT_ERR_INVALID : MT_ERR_UNSUPPORTED;
}

int mt_fd_adopt(int fd) {
    return fd < 0 ? MT_ERR_INVALID : MT_ERR_UNSUPPORTED;
}

int mt_fd_release(int fd) {
    return fd < 0 ? MT_ERR_INVALID : MT_ERR_UNSUPPORTED;
}

const char *mt_io_backend_name(void) {
    return mt_io_backend_name_locked();
}

int mt_fd_wait(int fd, int events, uint64_t timeout_ms, int *ready_events) {
    (void)events;
    (void)timeout_ms;
    if (ready_events) {
        *ready_events = 0;
    }
    return fd < 0 || !ready_events ? MT_ERR_INVALID : MT_ERR_UNSUPPORTED;
}

int mt_fd_wait_read(int fd, uint64_t timeout_ms) {
    int ready = 0;
    return mt_fd_wait(fd, MT_FD_READ, timeout_ms, &ready);
}

int mt_fd_wait_write(int fd, uint64_t timeout_ms) {
    int ready = 0;
    return mt_fd_wait(fd, MT_FD_WRITE, timeout_ms, &ready);
}

ssize_t mt_fd_read(int fd, void *buf, size_t len, uint64_t timeout_ms) {
    (void)buf;
    (void)len;
    (void)timeout_ms;
    return fd < 0 ? MT_ERR_INVALID : MT_ERR_UNSUPPORTED;
}

ssize_t mt_fd_write(int fd, const void *buf, size_t len, uint64_t timeout_ms) {
    (void)buf;
    (void)len;
    (void)timeout_ms;
    return fd < 0 ? MT_ERR_INVALID : MT_ERR_UNSUPPORTED;
}

int mt_fd_close(int fd) {
    return fd < 0 ? MT_ERR_INVALID : MT_ERR_UNSUPPORTED;
}

int mt_net_listen_tcp(const char *host, const char *port, int backlog) {
    (void)host;
    (void)backlog;
    return port ? MT_ERR_UNSUPPORTED : MT_ERR_INVALID;
}

int mt_net_accept(int listen_fd, struct sockaddr *addr, socklen_t *addrlen,
                  uint64_t timeout_ms) {
    (void)addr;
    (void)addrlen;
    (void)timeout_ms;
    return listen_fd < 0 ? MT_ERR_INVALID : MT_ERR_UNSUPPORTED;
}

ssize_t mt_net_read(int fd, void *buf, size_t len, uint64_t timeout_ms) {
    return mt_fd_read(fd, buf, len, timeout_ms);
}

ssize_t mt_net_write(int fd, const void *buf, size_t len, uint64_t timeout_ms) {
    return mt_fd_write(fd, buf, len, timeout_ms);
}

int mt_net_close(int fd) {
    return mt_fd_close(fd);
}
#endif


#ifndef MICROTHREAD_IO_BACKEND_INTERNAL_H
#define MICROTHREAD_IO_BACKEND_INTERNAL_H

#include "runtime_internal.h"

#define MT_IO_WAKE_SENTINEL (-1)

void mt_io_backend_wake(void);
int mt_io_backend_init(void);
void mt_io_backend_shutdown(void);
const char *mt_io_backend_name_locked(void);
int mt_io_backend_add(mt_fd_waiter_t *waiter);
void mt_io_backend_remove(mt_fd_waiter_t *waiter);
void mt_poll_fd_waiters_with_timeout(int timeout_ms);
void mt_poll_fd_waiters_once(uint64_t now_ns);
void mt_fd_wake_all(int result);
void mt_fd_wake_for_close(int fd);

void mt_poll_backend_wait_locked(int timeout_ms);
short mt_fd_events_to_poll(int events);
int mt_poll_revents_to_fd_events(short revents);

#if defined(MT_HAVE_EPOLL)
int mt_epoll_backend_init(void);
int mt_epoll_backend_add(mt_fd_waiter_t *waiter);
void mt_epoll_backend_remove(mt_fd_waiter_t *waiter);
void mt_epoll_backend_wait_locked(int timeout_ms);
#endif

#if defined(MT_HAVE_KQUEUE)
int mt_kqueue_backend_init(void);
int mt_kqueue_backend_add(mt_fd_waiter_t *waiter);
void mt_kqueue_backend_remove(mt_fd_waiter_t *waiter);
void mt_kqueue_backend_wait_locked(int timeout_ms);
#endif

#endif /* MICROTHREAD_IO_BACKEND_INTERNAL_H */

#ifndef MICROTHREAD_IO_BACKEND_INTERNAL_H
#define MICROTHREAD_IO_BACKEND_INTERNAL_H

#include <stdint.h>

typedef struct mt_fd_waiter mt_fd_waiter_t;

/*
 * Internal fd-readiness backend glue. These helpers are private to the runtime
 * translation unit; backend implementation files are included by
 * src/io_backend.c so they can share mt_runtime_t and mt_fd_waiter_t without
 * exposing those internals in the public API.
 */
#ifdef MICROTHREAD_EMBEDDED_IMPL

static void mt_io_backend_wake(void);
static int mt_io_backend_init(void);
static void mt_io_backend_shutdown(void);
static const char *mt_io_backend_name_locked(void);
static int mt_io_backend_add(mt_fd_waiter_t *waiter);
static void mt_io_backend_remove(mt_fd_waiter_t *waiter);
static void mt_poll_fd_waiters_with_timeout(int timeout_ms);
static void mt_poll_fd_waiters_once(uint64_t now_ns);
static void mt_fd_wake_all(int result);
static void mt_fd_wake_for_close(int fd);

#endif /* MICROTHREAD_EMBEDDED_IMPL */

#endif /* MICROTHREAD_IO_BACKEND_INTERNAL_H */

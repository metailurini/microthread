#ifndef MICROTHREAD_IO_H
#define MICROTHREAD_IO_H

#ifndef MICROTHREAD_H
#include "microthread.h"
#endif

#ifdef __cplusplus
extern "C" {
#endif

int     mt_fd_set_nonblocking(int fd);
int     mt_fd_adopt(int fd);
int     mt_fd_release(int fd);
int     mt_fd_wait_read(int fd, uint64_t timeout_ms);
int     mt_fd_wait_write(int fd, uint64_t timeout_ms);
int     mt_fd_wait(int fd, int events, uint64_t timeout_ms, int *ready_events);
ssize_t mt_fd_read(int fd, void *buf, size_t len, uint64_t timeout_ms);
ssize_t mt_fd_write(int fd, const void *buf, size_t len, uint64_t timeout_ms);
int     mt_fd_close(int fd);

int     mt_net_listen_tcp(const char *host, const char *port, int backlog);
int     mt_net_accept(int listen_fd, struct sockaddr *addr, socklen_t *addrlen,
                      uint64_t timeout_ms);
ssize_t mt_net_read(int fd, void *buf, size_t len, uint64_t timeout_ms);
ssize_t mt_net_write(int fd, const void *buf, size_t len, uint64_t timeout_ms);
int     mt_net_close(int fd);
const char *mt_io_backend_name(void);

#ifdef __cplusplus
}
#endif

#endif /* MICROTHREAD_IO_H */

#ifndef MT_H
#define MT_H

#include <stddef.h>
#include <stdint.h>

#if defined(_WIN32)
#ifndef _SSIZE_T_DEFINED
typedef long ssize_t;
#endif
struct sockaddr;
typedef int socklen_t;
#else
#include <sys/socket.h>
#include <sys/types.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*mt_fn)(void *arg);

typedef struct mt_chan mt_chan_t;
typedef struct mt_task_handle mt_task_handle_t;

typedef enum mt_select_op {
    MT_SELECT_RECV = 0,
    MT_SELECT_SEND,
    MT_SELECT_DEFAULT,
    MT_SELECT_TIMEOUT
} mt_select_op_t;

typedef struct mt_select_case {
    mt_select_op_t op;
    mt_chan_t *ch;
    void *value;
    uint64_t timeout_ms;
} mt_select_case_t;

typedef enum mt_task_status {
    MT_TASK_STATUS_READY = 0,
    MT_TASK_STATUS_RUNNING,
    MT_TASK_STATUS_SLEEPING,
    MT_TASK_STATUS_WAITING_CHAN,
    MT_TASK_STATUS_WAITING_JOIN,
    MT_TASK_STATUS_DONE,
    MT_TASK_STATUS_CANCELLED
} mt_task_status_t;

#ifndef MT_DEFAULT_STACK_SIZE
#define MT_DEFAULT_STACK_SIZE (64u * 1024u)
#endif

#ifndef MT_MIN_STACK_SIZE
#define MT_MIN_STACK_SIZE (16u * 1024u)
#endif

typedef struct mt_options {
    size_t stack_size;
} mt_options_t;

enum {
    MT_OK = 0,
    MT_ERR = -1,
    MT_ERR_INVALID = -2,
    MT_ERR_NOMEM = -3,
    MT_ERR_STATE = -4,
    MT_ERR_CLOSED = -5,
    MT_ERR_CANCELLED = -6,
    MT_ERR_WOULD_BLOCK = -7,
    MT_ERR_TIMEOUT = -8
};

enum {
    MT_FD_READ = 0x01,
    MT_FD_WRITE = 0x02
};

int  mt_init(void);
int  mt_go(mt_fn fn, void *arg);
int  mt_go_with_stack(mt_fn fn, void *arg, size_t stack_size);
mt_task_handle_t *mt_go_handle(mt_fn fn, void *arg);
mt_task_handle_t *mt_go_handle_with_stack(mt_fn fn, void *arg, size_t stack_size);
int  mt_run(void);
int  mt_runtime_start(size_t worker_count);
int  mt_runtime_workers(void);
int  mt_run_workers(size_t worker_count);
void mt_yield(void);
void mt_sleep_ms(uint64_t ms);
void mt_shutdown(void);

int  mt_join(mt_task_handle_t *task);
int  mt_task_cancel(mt_task_handle_t *task);
int  mt_task_cancelled(void);
int  mt_task_status(mt_task_handle_t *task, mt_task_status_t *out_status);
void mt_task_handle_release(mt_task_handle_t *task);

mt_chan_t *mt_chan_create(size_t elem_size, size_t capacity);
int        mt_chan_send(mt_chan_t *ch, const void *value);
int        mt_chan_recv(mt_chan_t *ch, void *out);
int        mt_chan_try_send(mt_chan_t *ch, const void *value);
int        mt_chan_try_recv(mt_chan_t *ch, void *out);
int        mt_chan_close(mt_chan_t *ch);
int        mt_chan_destroy(mt_chan_t *ch);
size_t     mt_chan_len(const mt_chan_t *ch);
size_t     mt_chan_capacity(const mt_chan_t *ch);
int        mt_chan_is_closed(const mt_chan_t *ch);

int        mt_select(mt_select_case_t *cases, size_t count, size_t *selected_index);

int     mt_fd_set_nonblocking(int fd);
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

size_t mt_debug_runnable_count(void);
size_t mt_debug_live_task_count(void);
size_t mt_debug_completed_task_count(void);
size_t mt_debug_sleeping_task_count(void);
size_t mt_debug_channel_waiting_task_count(void);
size_t mt_debug_join_waiting_task_count(void);
int    mt_debug_current_task_id(void);

#ifdef MT_TESTING
void   mt_test_fail_next_task_alloc(void);
void   mt_test_fail_next_stack_alloc(void);
void   mt_test_fail_next_context_make(void);
void   mt_test_fail_next_timer_alloc(void);
void   mt_test_fail_next_clock_read(void);
void   mt_test_fail_next_channel_alloc(void);
void   mt_test_fail_next_channel_buffer_alloc(void);
void   mt_test_fail_next_handle_alloc(void);
void   mt_test_fail_next_select_alloc(void);
void   mt_test_reset_faults(void);
int    mt_test_run_until_blocked(void);
void  *mt_test_current_stack_base(void);
size_t mt_test_current_stack_size(void);
size_t mt_test_current_stack_guard_size(void);
void   mt_test_memory_counters(size_t *task_allocs,
                               size_t *task_frees,
                               size_t *stack_allocs,
                               size_t *stack_frees,
                               size_t *timer_allocs,
                               size_t *timer_frees);
void   mt_test_channel_memory_counters(size_t *channel_allocs,
                                       size_t *channel_frees,
                                       size_t *buffer_allocs,
                                       size_t *buffer_frees);
void   mt_test_handle_memory_counters(size_t *handle_allocs,
                                      size_t *handle_frees);
void   mt_test_select_memory_counters(size_t *select_allocs,
                                      size_t *select_frees);
#endif

#ifdef __cplusplus
}
#endif

#endif /* MT_H */
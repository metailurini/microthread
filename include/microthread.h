#ifndef MICROTHREAD_H
#define MICROTHREAD_H

#include <stddef.h>
#include <stdint.h>

#if defined(_WIN32)
#ifndef _SSIZE_T_DEFINED
#include <stdint.h>
typedef intptr_t ssize_t;
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
    MT_TASK_STATUS_CANCELLED,
    MT_TASK_STATUS_WAITING_SELECT,
    MT_TASK_STATUS_WAITING_FD
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
    MT_ERR_TIMEOUT = -8,
    MT_ERR_IO = -9,
    MT_ERR_BACKEND = -10,
    MT_ERR_ADDRINFO = -11,
    MT_ERR_UNSUPPORTED = -12
};

enum {
    MT_FD_READ = 0x01,
    MT_FD_WRITE = 0x02
};

int  mt_init(void);
int  mt_init_with_options(const mt_options_t *options);
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
const char *mt_strerror(int rc);
const char *mt_task_status_name(mt_task_status_t status);
int  mt_last_os_error(void);

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

#include "microthread_io.h"

/* Diagnostic counters are declared by <microthread_debug.h>. */

/* Test-only fault hooks are declared by <microthread_testing.h>. */

#ifdef __cplusplus
}
#endif

#endif /* MICROTHREAD_H */

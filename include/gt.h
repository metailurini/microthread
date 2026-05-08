#ifndef GT_H
#define GT_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*gt_fn)(void *arg);

typedef struct gt_chan gt_chan_t;
typedef struct gt_task_handle gt_task_handle_t;

typedef enum gt_select_op {
    GT_SELECT_RECV = 0,
    GT_SELECT_SEND,
    GT_SELECT_DEFAULT,
    GT_SELECT_TIMEOUT
} gt_select_op_t;

typedef struct gt_select_case {
    gt_select_op_t op;
    gt_chan_t *ch;
    void *value;
    uint64_t timeout_ms;
} gt_select_case_t;

typedef enum gt_task_status {
    GT_TASK_STATUS_READY = 0,
    GT_TASK_STATUS_RUNNING,
    GT_TASK_STATUS_SLEEPING,
    GT_TASK_STATUS_WAITING_CHAN,
    GT_TASK_STATUS_WAITING_JOIN,
    GT_TASK_STATUS_DONE,
    GT_TASK_STATUS_CANCELLED
} gt_task_status_t;

#ifndef GT_DEFAULT_STACK_SIZE
#define GT_DEFAULT_STACK_SIZE (64u * 1024u)
#endif

#ifndef GT_MIN_STACK_SIZE
#define GT_MIN_STACK_SIZE (16u * 1024u)
#endif

typedef struct gt_options {
    size_t stack_size;
} gt_options_t;

enum {
    GT_OK = 0,
    GT_ERR = -1,
    GT_ERR_INVALID = -2,
    GT_ERR_NOMEM = -3,
    GT_ERR_STATE = -4,
    GT_ERR_CLOSED = -5,
    GT_ERR_CANCELLED = -6,
    GT_ERR_WOULD_BLOCK = -7
};

int  gt_init(void);
int  gt_go(gt_fn fn, void *arg);
int  gt_go_with_stack(gt_fn fn, void *arg, size_t stack_size);
gt_task_handle_t *gt_go_handle(gt_fn fn, void *arg);
gt_task_handle_t *gt_go_handle_with_stack(gt_fn fn, void *arg, size_t stack_size);
int  gt_run(void);
int  gt_runtime_start(size_t worker_count);
int  gt_runtime_workers(void);
int  gt_run_workers(size_t worker_count);
void gt_yield(void);
void gt_sleep_ms(uint64_t ms);
void gt_shutdown(void);

int  gt_join(gt_task_handle_t *task);
int  gt_task_cancel(gt_task_handle_t *task);
int  gt_task_cancelled(void);
int  gt_task_status(gt_task_handle_t *task, gt_task_status_t *out_status);
void gt_task_handle_release(gt_task_handle_t *task);

gt_chan_t *gt_chan_create(size_t elem_size, size_t capacity);
int        gt_chan_send(gt_chan_t *ch, const void *value);
int        gt_chan_recv(gt_chan_t *ch, void *out);
int        gt_chan_try_send(gt_chan_t *ch, const void *value);
int        gt_chan_try_recv(gt_chan_t *ch, void *out);
int        gt_chan_close(gt_chan_t *ch);
int        gt_chan_destroy(gt_chan_t *ch);
size_t     gt_chan_len(const gt_chan_t *ch);
size_t     gt_chan_capacity(const gt_chan_t *ch);
int        gt_chan_is_closed(const gt_chan_t *ch);

int        gt_select(gt_select_case_t *cases, size_t count, size_t *selected_index);

size_t gt_debug_runnable_count(void);
size_t gt_debug_live_task_count(void);
size_t gt_debug_completed_task_count(void);
size_t gt_debug_sleeping_task_count(void);
size_t gt_debug_channel_waiting_task_count(void);
size_t gt_debug_join_waiting_task_count(void);
int    gt_debug_current_task_id(void);

#ifdef GT_TESTING
void   gt_test_fail_next_task_alloc(void);
void   gt_test_fail_next_stack_alloc(void);
void   gt_test_fail_next_context_make(void);
void   gt_test_fail_next_timer_alloc(void);
void   gt_test_fail_next_clock_read(void);
void   gt_test_fail_next_channel_alloc(void);
void   gt_test_fail_next_channel_buffer_alloc(void);
void   gt_test_fail_next_handle_alloc(void);
void   gt_test_fail_next_select_alloc(void);
void   gt_test_reset_faults(void);
int    gt_test_run_until_blocked(void);
void  *gt_test_current_stack_base(void);
size_t gt_test_current_stack_size(void);
size_t gt_test_current_stack_guard_size(void);
void   gt_test_memory_counters(size_t *task_allocs,
                               size_t *task_frees,
                               size_t *stack_allocs,
                               size_t *stack_frees,
                               size_t *timer_allocs,
                               size_t *timer_frees);
void   gt_test_channel_memory_counters(size_t *channel_allocs,
                                       size_t *channel_frees,
                                       size_t *buffer_allocs,
                                       size_t *buffer_frees);
void   gt_test_handle_memory_counters(size_t *handle_allocs,
                                      size_t *handle_frees);
void   gt_test_select_memory_counters(size_t *select_allocs,
                                      size_t *select_frees);
#endif

#ifdef __cplusplus
}
#endif

#endif /* GT_H */
#ifndef MICROTHREAD_TESTING_H
#define MICROTHREAD_TESTING_H

#ifndef MT_TESTING
#define MT_TESTING
#endif

#include "microthread.h"
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

void   mt_test_fail_next_task_alloc(void);
void   mt_test_fail_next_stack_alloc(void);
void   mt_test_fail_next_context_make(void);
void   mt_test_fail_next_timer_alloc(void);
void   mt_test_fail_next_clock_read(void);
void   mt_test_fail_next_channel_alloc(void);
void   mt_test_fail_next_channel_buffer_alloc(void);
void   mt_test_fail_next_handle_alloc(void);
void   mt_test_fail_next_select_alloc(void);
void   mt_test_fail_next_fd_waiter_alloc(void);
void   mt_test_fail_next_io_backend_init(void);
void   mt_test_fail_next_io_backend_register(void);
void   mt_test_fail_next_io_backend_unregister(void);
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
void   mt_test_io_memory_counters(size_t *fd_waiter_allocs,
                                  size_t *fd_waiter_frees,
                                  size_t *backend_inits,
                                  size_t *backend_shutdowns,
                                  size_t *backend_registers,
                                  size_t *backend_unregisters);

#ifdef __cplusplus
}
#endif

#endif /* MICROTHREAD_TESTING_H */

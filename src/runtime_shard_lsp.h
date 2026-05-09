#ifndef MICROTHREAD_RUNTIME_SHARD_LSP_H
#define MICROTHREAD_RUNTIME_SHARD_LSP_H

/*
 * Support standalone parsing of implementation shards by clangd.
 *
 * The runtime intentionally builds these files by including them from
 * microthread.c so file-local helpers can remain private.  When clangd opens a
 * shard directly, though, it parses that shard as an independent translation
 * unit and otherwise reports many false undeclared-identifier diagnostics.  The
 * real build defines MT_RUNTIME_SHARD_BUILD before including shards, so this
 * header is only for editor/LSP analysis.
 */

#include "runtime_internal.h"

static void mt_task_destroy(mt_task_t *task);
static void mt_task_entry(void *arg);

static void mt_task_register(mt_task_t *task);
static void mt_task_unregister(mt_task_t *task);
static void mt_chan_register(mt_chan_t *ch);
static void mt_chan_unregister(mt_chan_t *ch);
static void mt_handle_register(mt_task_handle_t *handle);
static void mt_handle_unregister(mt_task_handle_t *handle);

static void *mt_alloc_task_memory(size_t size);
static void mt_free_task_memory(void *ptr);
static int mt_make_context(mt_context_t *ctx,
                           void *stack,
                           size_t stack_size,
                           void (*entry)(void *),
                           void *arg);
static void *mt_alloc_timer_memory(size_t size);
static void *mt_realloc_timer_memory(void *ptr, size_t size);
static void mt_free_timer_memory(void *ptr);
static mt_chan_t *mt_alloc_channel_memory(void);
static void mt_free_channel_memory(mt_chan_t *ch);
static unsigned char *mt_alloc_channel_buffer(size_t size);
static void mt_free_channel_buffer(unsigned char *buffer);
static mt_task_handle_t *mt_alloc_handle_memory(void);
static void mt_free_handle_memory(mt_task_handle_t *handle);
static mt_select_waiter_t *mt_alloc_select_waiter(void);
static void mt_free_select_waiter(mt_select_waiter_t *waiter);

static size_t mt_page_size(void);
static size_t mt_round_up(size_t value, size_t align);
static int mt_stack_alloc(mt_stack_t *stack, size_t requested_size);
static void *mt_stack_context_base(mt_stack_t *stack);
static size_t mt_stack_context_size(mt_stack_t *stack);
static void mt_stack_free(mt_stack_t *stack);

static uint64_t mt_now_ns_raw(int *ok);
static void mt_sleep_os_ns(uint64_t ns);
static int mt_timer_less(const mt_task_t *a, const mt_task_t *b);
static int mt_timer_reserve(size_t needed);
static void mt_timer_swap(size_t a, size_t b);
static int mt_timer_push(mt_task_t *task, uint64_t deadline_ns);
static mt_task_t *mt_timer_pop(void);
static mt_task_t *mt_timer_peek(void);
static void mt_wake_expired_timers(uint64_t now_ns);

static mt_task_t *mt_runq_pop(void);
static int mt_runq_remove(mt_task_t *task);

static void mt_chan_waitq_push(mt_task_t **head,
                               mt_task_t **tail,
                               size_t *count,
                               mt_task_t *task);
static mt_task_t *mt_chan_waitq_pop(mt_task_t **head,
                                    mt_task_t **tail,
                                    size_t *count);
static int mt_waitq_remove(mt_task_t **head,
                           mt_task_t **tail,
                           size_t *count,
                           mt_task_t *task);
static void mt_select_waitq_push(mt_select_waiter_t **head,
                                 mt_select_waiter_t **tail,
                                 size_t *count,
                                 mt_select_waiter_t *waiter);
static mt_select_waiter_t *mt_select_waitq_pop(mt_select_waiter_t **head,
                                               mt_select_waiter_t **tail,
                                               size_t *count);
static int mt_select_waitq_remove(mt_select_waiter_t **head,
                                  mt_select_waiter_t **tail,
                                  size_t *count,
                                  mt_select_waiter_t *waiter);

static int mt_select_remove_waiter_from_channel(mt_select_waiter_t *waiter);
static void mt_select_free_task_waiters(mt_task_t *task);
static void mt_select_unpark_task(mt_task_t *task, int result, size_t index);
static void mt_select_complete_waiter(mt_select_waiter_t *waiter, int result);

static mt_task_status_t mt_status_from_task(const mt_task_t *task);
static void mt_handle_free_if_possible(mt_task_handle_t *handle);
static void mt_join_waiter_ready(mt_task_t *task, int result);
static void mt_handle_complete(mt_task_handle_t *handle, int cancelled);
static int mt_handle_remove_joiner(mt_task_handle_t *handle, mt_task_t *task);

static void mt_chan_ready_waiter(mt_task_t *task, int result);
static int mt_chan_remove_waiter(mt_task_t *task);
static void mt_task_after_switch(mt_task_t *task);
static void *mt_worker_thread_main(void *arg);
static void mt_worker_loop(mt_context_t *scheduler_ctx,
                           int worker_index,
                           int sleep_for_timers);
static int mt_run_internal(size_t worker_count, int sleep_for_timers);

static void mt_chan_buffer_push(mt_chan_t *ch, const void *value);
static void mt_chan_buffer_pop(mt_chan_t *ch, void *out);
static void mt_chan_fill_buffer_from_waiting_sender(mt_chan_t *ch);
static void mt_chan_wake_closed_waiters(mt_chan_t *ch);
static int mt_chan_can_send_locked(mt_chan_t *ch);
static int mt_chan_can_recv_locked(mt_chan_t *ch);
static int mt_select_try_case(mt_select_case_t *scase);
static int mt_select_has_timeout(const mt_select_case_t *cases,
                                 size_t count,
                                 uint64_t *timeout_ms);
static void mt_select_cleanup_allocated(mt_select_waiter_t *head);

#endif /* MICROTHREAD_RUNTIME_SHARD_LSP_H */

/* Internal implementation shard included by microthread.c. */

#ifndef MT_RUNTIME_SHARD_BUILD
#include "runtime_shard_lsp.h"
#endif
int mt_init_with_options(const mt_options_t *options) {
    if (g_rt.initialized) {
        return MT_OK;
    }

    size_t default_stack_size = MT_DEFAULT_STACK_SIZE;
    if (options && options->stack_size != 0) {
        if (options->stack_size < MT_MIN_STACK_SIZE) {
            return MT_ERR_INVALID;
        }
        default_stack_size = options->stack_size;
    }

    memset(&g_rt, 0, sizeof(g_rt));
    g_rt.default_stack_size = default_stack_size;
    g_rt.io_backend_kind = MT_IO_BACKEND_NONE;
    g_rt.io_backend_fd = -1;
    g_rt.io_wake_read_fd = -1;
    g_rt.io_wake_write_fd = -1;
#if MT_HAS_OS_THREADS
    if (pthread_mutex_init(&g_rt.lock, NULL) != 0) {
        return MT_ERR;
    }
    if (pthread_cond_init(&g_rt.cond, NULL) != 0) {
        pthread_mutex_destroy(&g_rt.lock);
        return MT_ERR;
    }
#endif
    if (mt_ctx_init_scheduler(&g_rt.scheduler_ctx) != 0) {
#if MT_HAS_OS_THREADS
        pthread_cond_destroy(&g_rt.cond);
        pthread_mutex_destroy(&g_rt.lock);
#endif
        return MT_ERR;
    }
#if !defined(_WIN32)
    int io_rc = mt_io_backend_init();
    if (io_rc != MT_OK) {
        mt_ctx_destroy(&g_rt.scheduler_ctx);
#if MT_HAS_OS_THREADS
        pthread_cond_destroy(&g_rt.cond);
        pthread_mutex_destroy(&g_rt.lock);
#endif
        mt_io_backend_shutdown();
        return io_rc;
    }
#else
    g_rt.io_backend_kind = MT_IO_BACKEND_NONE;
    g_rt.io_backend_fd = -1;
    g_rt.io_wake_read_fd = -1;
    g_rt.io_wake_write_fd = -1;
#endif

    g_rt.initialized = 1;
    g_rt.next_id = 1;
    g_rt.worker_count = 1;
    g_rt.run_result = MT_OK;
    return MT_OK;
}

int mt_init(void) {
    return mt_init_with_options(NULL);
}
void mt_shutdown(void) {
    if (mt_current_task()) {
        /*
         * Shutting down from inside a running microthread would require
         * freeing the currently executing stack/context.  v0.1 treats this as
         * misuse and makes the call a safe no-op instead of corrupting the
         * scheduler.  The caller can return to the scheduler and shut down
         * from the owning OS thread.
         */
        return;
    }

    if (!g_rt.initialized) {
        return;
    }

    mt_lock();
    if (g_rt.running) {
        g_rt.stopping = 1;
        g_rt.run_result = MT_ERR_CANCELLED;
        mt_fd_wake_all(MT_ERR_CANCELLED);
        mt_notify_all();
        mt_unlock();
        return;
    }

    for (mt_chan_t *ch = g_rt.channels; ch; ch = ch->registry_next) {
        ch->closed = 1;
        ch->send_head = NULL;
        ch->send_tail = NULL;
        ch->recv_head = NULL;
        ch->recv_tail = NULL;
        ch->send_waiters = 0;
        ch->recv_waiters = 0;
        ch->select_send_head = NULL;
        ch->select_send_tail = NULL;
        ch->select_recv_head = NULL;
        ch->select_recv_tail = NULL;
        ch->select_send_waiters = 0;
        ch->select_recv_waiters = 0;
    }

    for (mt_task_handle_t *handle = g_rt.handles; handle; handle = handle->registry_next) {
        handle->cancel_requested = 1;
        handle->completed = 1;
        handle->status = MT_TASK_STATUS_CANCELLED;
        handle->join_result = MT_ERR_CANCELLED;
        handle->task = NULL;
        handle->join_head = NULL;
        handle->join_tail = NULL;
        handle->join_waiters = 0;
    }

    while (g_rt.all_tasks) {
        mt_task_t *task = g_rt.all_tasks;
        mt_select_free_task_waiters(task);
        if (task->fd_waiter) {
            mt_fd_waiter_remove(task->fd_waiter);
            mt_fd_free_waiter(task->fd_waiter);
            task->fd_waiter = NULL;
        }
        if (g_rt.live_count > 0) {
            g_rt.live_count--;
        }
        mt_task_destroy(task);
    }

    g_rt.runq_head = NULL;
    g_rt.runq_tail = NULL;
    g_rt.runnable_count = 0;
    g_rt.timers.len = 0;
    g_rt.channel_waiting_count = 0;
    g_rt.join_waiting_count = 0;
    g_rt.fd_waiting_count = 0;

    mt_free_timer_memory(g_rt.timers.items);
    g_rt.timers.items = NULL;
    g_rt.timers.len = 0;
    g_rt.timers.cap = 0;

    mt_io_backend_shutdown();

    mt_ctx_destroy(&g_rt.scheduler_ctx);

    mt_task_handle_t *handle = g_rt.handles;
    while (handle) {
        mt_task_handle_t *next = handle->registry_next;
        if (handle->released) {
            mt_free_handle_memory(handle);
        }
        handle = next;
    }

#if MT_HAS_OS_THREADS
    pthread_mutex_unlock(&g_rt.lock);
    pthread_cond_destroy(&g_rt.cond);
    pthread_mutex_destroy(&g_rt.lock);
#else
    mt_unlock();
#endif
    memset(&g_rt, 0, sizeof(g_rt));
}

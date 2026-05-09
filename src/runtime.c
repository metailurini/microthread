/* Internal implementation shard included by microthread.c. */

#include "runtime_internal.h"

mt_runtime_t g_rt;

#if MT_HAS_OS_THREADS
__thread mt_task_t *g_tls_current;
__thread mt_context_t *g_tls_scheduler_ctx;
__thread int g_tls_worker_index;
#else
mt_task_t *g_tls_current;
mt_context_t *g_tls_scheduler_ctx;
int g_tls_worker_index;
#endif

void mt_lock(void) {
#if MT_HAS_OS_THREADS
    if (g_rt.initialized) {
        pthread_mutex_lock(&g_rt.lock);
    }
#endif
}

void mt_unlock(void) {
#if MT_HAS_OS_THREADS
    if (g_rt.initialized) {
        pthread_mutex_unlock(&g_rt.lock);
    }
#endif
}

void mt_notify_one(void) {
#if MT_HAS_OS_THREADS
    if (g_rt.initialized) {
        pthread_cond_signal(&g_rt.cond);
        mt_io_backend_wake();
    }
#endif
}

void mt_notify_all(void) {
#if MT_HAS_OS_THREADS
    if (g_rt.initialized) {
        pthread_cond_broadcast(&g_rt.cond);
        mt_io_backend_wake();
    }
#endif
}

mt_task_t *mt_current_task(void) {
    return g_tls_current;
}

mt_context_t *mt_current_scheduler_ctx(void) {
    return g_tls_scheduler_ctx ? g_tls_scheduler_ctx : &g_rt.scheduler_ctx;
}

void mt_runq_push(mt_task_t *task);
void mt_select_timeout_ready(mt_task_t *task);
void mt_fd_ready_waiter(mt_fd_waiter_t *waiter, int result, int ready_events);
void mt_fd_timeout_ready(mt_task_t *task);
void mt_poll_fd_waiters_once(uint64_t now_ns);
#if MT_HAS_OS_THREADS
void mt_cond_timedwait_ns(uint64_t delay_ns);
#endif

static void mt_task_register(mt_task_t *task) {
    task->all_next = g_rt.all_tasks;
    g_rt.all_tasks = task;
}

static void mt_task_unregister(mt_task_t *task) {
    mt_task_t **link = &g_rt.all_tasks;
    while (*link) {
        if (*link == task) {
            *link = task->all_next;
            task->all_next = NULL;
            return;
        }
        link = &(*link)->all_next;
    }
}

static void mt_chan_register(mt_chan_t *ch) {
    ch->registry_next = g_rt.channels;
    g_rt.channels = ch;
}

static void mt_chan_unregister(mt_chan_t *ch) {
    mt_chan_t **link = &g_rt.channels;
    while (*link) {
        if (*link == ch) {
            *link = ch->registry_next;
            ch->registry_next = NULL;
            return;
        }
        link = &(*link)->registry_next;
    }
}

static void mt_handle_register(mt_task_handle_t *handle) {
    handle->registry_next = g_rt.handles;
    g_rt.handles = handle;
}

static void mt_handle_unregister(mt_task_handle_t *handle) {
    mt_task_handle_t **link = &g_rt.handles;
    while (*link) {
        if (*link == handle) {
            *link = handle->registry_next;
            handle->registry_next = NULL;
            return;
        }
        link = &(*link)->registry_next;
    }
}


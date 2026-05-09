/* Internal implementation shard included by microthread.c. */

#ifndef MT_RUNTIME_SHARD_BUILD
#include "runtime_shard_lsp.h"
#endif
static mt_task_status_t mt_status_from_task(const mt_task_t *task) {
    switch (task->state) {
        case MT_TASK_READY: return MT_TASK_STATUS_READY;
        case MT_TASK_RUNNING: return MT_TASK_STATUS_RUNNING;
        case MT_TASK_SLEEPING: return MT_TASK_STATUS_SLEEPING;
        case MT_TASK_WAITING_CHAN: return MT_TASK_STATUS_WAITING_CHAN;
        case MT_TASK_WAITING_SELECT: return MT_TASK_STATUS_WAITING_SELECT;
        case MT_TASK_WAITING_FD: return MT_TASK_STATUS_WAITING_FD;
        case MT_TASK_WAITING_JOIN: return MT_TASK_STATUS_WAITING_JOIN;
        case MT_TASK_DEAD: return MT_TASK_STATUS_DONE;
    }
    return MT_TASK_STATUS_DONE;
}

static void mt_handle_free_if_possible(mt_task_handle_t *handle) {
    if (!handle || !handle->released || handle->task || handle->join_waiters != 0) {
        return;
    }
    mt_handle_unregister(handle);
    mt_free_handle_memory(handle);
}

static void mt_join_waiter_ready(mt_task_t *task, int result) {
    mt_task_complete_join_wait(task, result);
}

static void mt_handle_complete(mt_task_handle_t *handle, int cancelled) {
    if (!handle || handle->completed) {
        return;
    }
    handle->completed = 1;
    handle->task = NULL;
    handle->status = cancelled ? MT_TASK_STATUS_CANCELLED : MT_TASK_STATUS_DONE;
    handle->join_result = cancelled ? MT_ERR_CANCELLED : MT_OK;

    mt_task_t *waiter = NULL;
    while ((waiter = mt_chan_waitq_pop(&handle->join_head, &handle->join_tail, &handle->join_waiters)) != NULL) {
        mt_join_waiter_ready(waiter, handle->join_result);
    }
    mt_handle_free_if_possible(handle);
}

static int mt_handle_remove_joiner(mt_task_handle_t *handle, mt_task_t *task) {
    if (!handle) {
        return 0;
    }
    if (mt_waitq_remove(&handle->join_head, &handle->join_tail, &handle->join_waiters, task)) {
        if (g_rt.join_waiting_count > 0) {
            g_rt.join_waiting_count--;
        }
        task->join_waiting_on = NULL;
        return 1;
    }
    return 0;
}

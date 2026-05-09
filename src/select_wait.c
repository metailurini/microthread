/* Internal implementation shard included by microthread.c. */

#ifndef MT_RUNTIME_SHARD_BUILD
#include "runtime_shard_lsp.h"
#endif
static int mt_select_remove_waiter_from_channel(mt_select_waiter_t *waiter) {
    if (!waiter || !waiter->active || !waiter->ch) {
        return 0;
    }
    if (waiter->op == MT_SELECT_SEND) {
        return mt_select_waitq_remove(&waiter->ch->select_send_head,
                                      &waiter->ch->select_send_tail,
                                      &waiter->ch->select_send_waiters,
                                      waiter);
    }
    if (waiter->op == MT_SELECT_RECV) {
        return mt_select_waitq_remove(&waiter->ch->select_recv_head,
                                      &waiter->ch->select_recv_tail,
                                      &waiter->ch->select_recv_waiters,
                                      waiter);
    }
    return 0;
}

static void mt_select_free_task_waiters(mt_task_t *task) {
    mt_select_waiter_t *waiter = task ? task->select_waiters : NULL;
    while (waiter) {
        mt_select_waiter_t *next = waiter->task_next;
        if (waiter->active) {
            mt_select_remove_waiter_from_channel(waiter);
        }
        mt_free_select_waiter(waiter);
        waiter = next;
    }
    if (task) {
        task->select_waiters = NULL;
        task->select_waiter_count = 0;
    }
}

static void mt_select_unpark_task(mt_task_t *task, int result, size_t index) {
    if (!task) {
        return;
    }
    if (task->select_in_timer) {
        mt_timer_remove(task);
        task->select_in_timer = 0;
    }
    mt_select_free_task_waiters(task);
    mt_task_complete_select_wait(task);
    task->select_result = result;
    task->select_index = index;
    mt_task_mark_ready(task);
}

static void mt_select_complete_waiter(mt_select_waiter_t *waiter, int result) {
    if (!waiter || !waiter->task) {
        return;
    }
    waiter->active = 0;
    mt_select_unpark_task(waiter->task, result, waiter->index);
}

void mt_select_timeout_ready(mt_task_t *task) {
    if (!task) {
        return;
    }
    task->select_in_timer = 0;
    mt_select_free_task_waiters(task);
    mt_task_complete_select_wait(task);
    task->select_result = MT_OK;
    mt_task_mark_ready(task);
}

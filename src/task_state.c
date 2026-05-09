/* Internal implementation shard included by microthread.c. */

#ifndef MT_RUNTIME_SHARD_BUILD
#include "runtime_shard_lsp.h"
#endif
void mt_task_mark_ready(mt_task_t *task) {
    if (!task) {
        return;
    }
    task->state = MT_TASK_READY;
    mt_runq_push(task);
}

void mt_task_mark_running(mt_task_t *task) {
    if (!task) {
        return;
    }
    task->state = MT_TASK_RUNNING;
}

void mt_task_mark_dead(mt_task_t *task) {
    if (!task) {
        return;
    }
    task->state = MT_TASK_DEAD;
}

void mt_task_block_on_channel(mt_task_t *task,
                              mt_chan_t *ch,
                              mt_chan_wait_kind_t kind,
                              void *value) {
    assert(task != NULL);
    assert(ch != NULL);
    task->chan_wait_kind = kind;
    task->chan_wait_ch = ch;
    task->chan_value = value;
    task->chan_result = MT_OK;
    task->state = MT_TASK_WAITING_CHAN;
    g_rt.channel_waiting_count++;
}

void mt_task_complete_channel_wait(mt_task_t *task, int result) {
    assert(task != NULL);
    task->chan_result = result;
    task->chan_wait_kind = MT_CHAN_WAIT_NONE;
    task->chan_wait_ch = NULL;
    task->chan_value = NULL;
    if (g_rt.channel_waiting_count > 0) {
        g_rt.channel_waiting_count--;
    }
    mt_task_mark_ready(task);
}

void mt_task_clear_channel_wait(mt_task_t *task) {
    assert(task != NULL);
    task->chan_wait_kind = MT_CHAN_WAIT_NONE;
    task->chan_wait_ch = NULL;
    task->chan_value = NULL;
}

void mt_task_block_on_select(mt_task_t *task) {
    assert(task != NULL);
    task->state = MT_TASK_WAITING_SELECT;
    if (!task->select_counted_waiting) {
        g_rt.channel_waiting_count++;
        task->select_counted_waiting = 1;
    }
}

void mt_task_complete_select_wait(mt_task_t *task) {
    assert(task != NULL);
    if (task->select_counted_waiting) {
        if (g_rt.channel_waiting_count > 0) {
            g_rt.channel_waiting_count--;
        }
        task->select_counted_waiting = 0;
    }
}

void mt_task_block_on_join(mt_task_t *task, mt_task_handle_t *handle) {
    assert(task != NULL);
    assert(handle != NULL);
    task->join_waiting_on = handle;
    task->join_result = MT_OK;
    task->state = MT_TASK_WAITING_JOIN;
    g_rt.join_waiting_count++;
}

void mt_task_complete_join_wait(mt_task_t *task, int result) {
    assert(task != NULL);
    task->join_result = result;
    task->join_waiting_on = NULL;
    if (g_rt.join_waiting_count > 0) {
        g_rt.join_waiting_count--;
    }
    mt_task_mark_ready(task);
}

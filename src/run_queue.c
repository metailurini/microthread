/* Internal implementation shard included by microthread.c. */

#ifndef MT_RUNTIME_SHARD_BUILD
#include "runtime_shard_lsp.h"
#endif
void mt_runq_push(mt_task_t *task) {
    task->next = NULL;
    if (!g_rt.runq_tail) {
        g_rt.runq_head = task;
        g_rt.runq_tail = task;
    } else {
        g_rt.runq_tail->next = task;
        g_rt.runq_tail = task;
    }
    g_rt.runnable_count++;
    mt_notify_one();
}

static mt_task_t *mt_runq_pop(void) {
    mt_task_t *task = g_rt.runq_head;
    if (!task) {
        return NULL;
    }

    g_rt.runq_head = task->next;
    if (!g_rt.runq_head) {
        g_rt.runq_tail = NULL;
    }
    task->next = NULL;
    g_rt.runnable_count--;
    return task;
}

static int mt_runq_remove(mt_task_t *task) {
    mt_task_t **link = &g_rt.runq_head;
    mt_task_t *prev = NULL;
    while (*link) {
        if (*link == task) {
            *link = task->next;
            if (g_rt.runq_tail == task) {
                g_rt.runq_tail = prev;
            }
            task->next = NULL;
            if (g_rt.runnable_count > 0) {
                g_rt.runnable_count--;
            }
            return 1;
        }
        prev = *link;
        link = &(*link)->next;
    }
    return 0;
}

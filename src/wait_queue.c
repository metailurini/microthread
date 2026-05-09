/* Internal implementation shard included by microthread.c. */

#ifndef MT_RUNTIME_SHARD_BUILD
#include "runtime_shard_lsp.h"
#endif
static void mt_chan_waitq_push(mt_task_t **head, mt_task_t **tail, size_t *count, mt_task_t *task) {
    task->wait_next = NULL;
    if (!*tail) {
        *head = task;
        *tail = task;
    } else {
        (*tail)->wait_next = task;
        *tail = task;
    }
    if (count) {
        (*count)++;
    }
}

static mt_task_t *mt_chan_waitq_pop(mt_task_t **head, mt_task_t **tail, size_t *count) {
    mt_task_t *task = *head;
    if (!task) {
        return NULL;
    }
    *head = task->wait_next;
    if (!*head) {
        *tail = NULL;
    }
    task->wait_next = NULL;
    if (count && *count > 0) {
        (*count)--;
    }
    return task;
}

static int mt_waitq_remove(mt_task_t **head, mt_task_t **tail, size_t *count, mt_task_t *task) {
    mt_task_t **link = head;
    mt_task_t *prev = NULL;
    while (*link) {
        if (*link == task) {
            *link = task->wait_next;
            if (*tail == task) {
                *tail = prev;
            }
            task->wait_next = NULL;
            if (count && *count > 0) {
                (*count)--;
            }
            return 1;
        }
        prev = *link;
        link = &(*link)->wait_next;
    }
    return 0;
}

static void mt_select_waitq_push(mt_select_waiter_t **head,
                                 mt_select_waiter_t **tail,
                                 size_t *count,
                                 mt_select_waiter_t *waiter) {
    waiter->chan_next = NULL;
    waiter->active = 1;
    if (!*tail) {
        *head = waiter;
        *tail = waiter;
    } else {
        (*tail)->chan_next = waiter;
        *tail = waiter;
    }
    if (count) {
        (*count)++;
    }
}

static mt_select_waiter_t *mt_select_waitq_pop(mt_select_waiter_t **head,
                                               mt_select_waiter_t **tail,
                                               size_t *count) {
    mt_select_waiter_t *waiter = *head;
    if (!waiter) {
        return NULL;
    }
    *head = waiter->chan_next;
    if (!*head) {
        *tail = NULL;
    }
    waiter->chan_next = NULL;
    waiter->active = 0;
    if (count && *count > 0) {
        (*count)--;
    }
    return waiter;
}

static int mt_select_waitq_remove(mt_select_waiter_t **head,
                                  mt_select_waiter_t **tail,
                                  size_t *count,
                                  mt_select_waiter_t *waiter) {
    mt_select_waiter_t **link = head;
    mt_select_waiter_t *prev = NULL;
    while (*link) {
        if (*link == waiter) {
            *link = waiter->chan_next;
            if (*tail == waiter) {
                *tail = prev;
            }
            waiter->chan_next = NULL;
            waiter->active = 0;
            if (count && *count > 0) {
                (*count)--;
            }
            return 1;
        }
        prev = *link;
        link = &(*link)->chan_next;
    }
    return 0;
}

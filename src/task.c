/* Internal implementation shard included by microthread.c. */

static void mt_task_destroy(mt_task_t *task) {
    if (!task) {
        return;
    }
    mt_select_free_task_waiters(task);
    if (task->fd_waiter) {
        mt_fd_waiter_remove(task->fd_waiter);
        mt_fd_free_waiter(task->fd_waiter);
        task->fd_waiter = NULL;
        task->fd_in_timer = 0;
    }
    mt_task_unregister(task);
    mt_ctx_destroy(&task->ctx);
    mt_stack_free(&task->stack);
    mt_free_task_memory(task);
}

static int mt_create_task_internal(mt_fn fn,
                                   void *arg,
                                   size_t stack_size,
                                   int want_handle,
                                   mt_task_handle_t **out_handle) {
    if (!fn) {
        return MT_ERR_INVALID;
    }

    mt_task_handle_t *handle = NULL;
    if (want_handle) {
        handle = mt_alloc_handle_memory();
        if (!handle) {
            return MT_ERR_NOMEM;
        }
        handle->status = MT_TASK_STATUS_READY;
        handle->join_result = MT_OK;
        mt_handle_register(handle);
    }

    mt_task_t *task = (mt_task_t *)mt_alloc_task_memory(sizeof(*task));
    if (!task) {
        if (handle) {
            handle->released = 1;
            mt_handle_free_if_possible(handle);
        }
        return MT_ERR_NOMEM;
    }

    size_t effective_stack_size = stack_size != 0 ? stack_size : g_rt.default_stack_size;
    int rc = mt_stack_alloc(&task->stack, effective_stack_size);
    if (rc != MT_OK) {
        mt_free_task_memory(task);
        if (handle) {
            handle->released = 1;
            mt_handle_free_if_possible(handle);
        }
        return rc;
    }

    task->id = g_rt.next_id++;
    task->fn = fn;
    task->arg = arg;
    task->state = MT_TASK_READY;
    task->handle = handle;
    if (handle) {
        handle->task = task;
    }

    if (mt_make_context(&task->ctx,
                        mt_stack_context_base(&task->stack),
                        mt_stack_context_size(&task->stack),
                        mt_task_entry,
                        task) != 0) {
        mt_stack_free(&task->stack);
        mt_free_task_memory(task);
        if (handle) {
            handle->task = NULL;
            handle->released = 1;
            mt_handle_free_if_possible(handle);
        }
        return MT_ERR;
    }

    mt_runq_push(task);
    mt_task_register(task);
    g_rt.live_count++;
    if (out_handle) {
        *out_handle = handle;
    }
    return task->id;
}

int mt_go(mt_fn fn, void *arg) {
    if (!g_rt.initialized && mt_init() != MT_OK) {
        return MT_ERR;
    }
    mt_lock();
    int rc = mt_create_task_internal(fn, arg, 0, 0, NULL);
    mt_unlock();
    return rc;
}

int mt_go_with_stack(mt_fn fn, void *arg, size_t stack_size) {
    if (!g_rt.initialized && mt_init() != MT_OK) {
        return MT_ERR;
    }
    mt_lock();
    int rc = mt_create_task_internal(fn, arg, stack_size, 0, NULL);
    mt_unlock();
    return rc;
}

mt_task_handle_t *mt_go_handle(mt_fn fn, void *arg) {
    if (!g_rt.initialized && mt_init() != MT_OK) {
        return NULL;
    }
    mt_task_handle_t *handle = NULL;
    mt_lock();
    if (mt_create_task_internal(fn, arg, 0, 1, &handle) < 0) {
        mt_unlock();
        return NULL;
    }
    mt_unlock();
    return handle;
}

mt_task_handle_t *mt_go_handle_with_stack(mt_fn fn, void *arg, size_t stack_size) {
    if (!g_rt.initialized && mt_init() != MT_OK) {
        return NULL;
    }
    mt_task_handle_t *handle = NULL;
    mt_lock();
    if (mt_create_task_internal(fn, arg, stack_size, 1, &handle) < 0) {
        mt_unlock();
        return NULL;
    }
    mt_unlock();
    return handle;
}

int mt_join(mt_task_handle_t *handle) {
    if (!handle) {
        return MT_ERR_INVALID;
    }
    mt_lock();
    if (handle->completed) {
        int rc = handle->join_result;
        mt_unlock();
        return rc;
    }

    mt_task_t *task = mt_current_task();
    if (!task) {
        mt_unlock();
        return MT_ERR_STATE;
    }
    if (handle->task == task) {
        mt_unlock();
        return MT_ERR_STATE;
    }

    mt_task_block_on_join(task, handle);
    mt_chan_waitq_push(&handle->join_head, &handle->join_tail, &handle->join_waiters, task);
    mt_ctx_switch(&task->ctx, mt_current_scheduler_ctx());
    return task->join_result;
}

int mt_task_cancelled(void) {
    mt_task_t *task = mt_current_task();
    return task && task->handle && task->handle->cancel_requested;
}

static void mt_task_wake_for_cancel(mt_task_t *task) {
    if (!task) {
        return;
    }
    switch (task->state) {
        case MT_TASK_READY:
            if (mt_runq_remove(task)) {
                mt_task_mark_dead(task);
                if (g_rt.live_count > 0) {
                    g_rt.live_count--;
                }
                g_rt.completed_count++;
                if (task->handle) {
                    mt_handle_complete(task->handle, 1);
                }
                mt_task_destroy(task);
            }
            break;
        case MT_TASK_SLEEPING:
            if (mt_timer_remove(task)) {
                mt_task_mark_ready(task);
            }
            break;
        case MT_TASK_WAITING_CHAN:
            if (mt_chan_remove_waiter(task)) {
                mt_task_complete_channel_wait(task, MT_ERR_CANCELLED);
            }
            break;
        case MT_TASK_WAITING_SELECT:
            mt_select_unpark_task(task, MT_ERR_CANCELLED, task->select_index);
            break;
        case MT_TASK_WAITING_FD:
            if (task->fd_waiter) {
                mt_fd_ready_waiter(task->fd_waiter, MT_ERR_CANCELLED, 0);
            }
            break;
        case MT_TASK_WAITING_JOIN:
            if (mt_handle_remove_joiner(task->join_waiting_on, task)) {
                mt_task_complete_join_wait(task, MT_ERR_CANCELLED);
            }
            break;
        case MT_TASK_RUNNING:
        case MT_TASK_DEAD:
            break;
    }
}

int mt_task_cancel(mt_task_handle_t *handle) {
    if (!handle) {
        return MT_ERR_INVALID;
    }
    mt_lock();
    handle->cancel_requested = 1;
    if (handle->completed) {
        mt_unlock();
        return MT_OK;
    }
    handle->status = MT_TASK_STATUS_CANCELLED;
    mt_task_wake_for_cancel(handle->task);
    mt_unlock();
    return MT_OK;
}

int mt_task_status(mt_task_handle_t *handle, mt_task_status_t *out_status) {
    if (!handle || !out_status) {
        return MT_ERR_INVALID;
    }
    mt_lock();
    if (handle->completed) {
        *out_status = handle->status;
        mt_unlock();
        return MT_OK;
    }
    if (handle->cancel_requested) {
        *out_status = MT_TASK_STATUS_CANCELLED;
        mt_unlock();
        return MT_OK;
    }
    if (handle->task) {
        *out_status = mt_status_from_task(handle->task);
        mt_unlock();
        return MT_OK;
    }
    *out_status = handle->status;
    mt_unlock();
    return MT_OK;
}

void mt_task_handle_release(mt_task_handle_t *handle) {
    if (!handle) {
        return;
    }
    mt_lock();
    handle->released = 1;
    mt_handle_free_if_possible(handle);
    mt_unlock();
}
static void mt_task_entry(void *arg) {
    mt_task_t *task = (mt_task_t *)arg;
    task->fn(task->arg);
    mt_lock();
    mt_task_mark_dead(task);
    mt_ctx_switch(&task->ctx, mt_current_scheduler_ctx());
    abort();
}
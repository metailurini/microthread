/* Internal implementation shard included by microthread.c. */

static void mt_chan_buffer_push(mt_chan_t *ch, const void *value) {
    memcpy(ch->buffer + (ch->tail * ch->elem_size), value, ch->elem_size);
    ch->tail = (ch->tail + 1u) % ch->capacity;
    ch->len++;
}

static void mt_chan_buffer_pop(mt_chan_t *ch, void *out) {
    memcpy(out, ch->buffer + (ch->head * ch->elem_size), ch->elem_size);
    ch->head = (ch->head + 1u) % ch->capacity;
    ch->len--;
}

static void mt_chan_fill_buffer_from_waiting_sender(mt_chan_t *ch) {
    if (!ch || ch->closed || ch->capacity == 0 || ch->len >= ch->capacity) {
        return;
    }

    mt_task_t *sender = mt_chan_waitq_pop(&ch->send_head, &ch->send_tail, &ch->send_waiters);
    if (sender) {
        mt_chan_buffer_push(ch, sender->chan_value);
        mt_chan_ready_waiter(sender, MT_OK);
        return;
    }

    mt_select_waiter_t *select_sender = mt_select_waitq_pop(&ch->select_send_head,
                                                            &ch->select_send_tail,
                                                            &ch->select_send_waiters);
    if (select_sender) {
        mt_chan_buffer_push(ch, select_sender->value);
        mt_select_complete_waiter(select_sender, MT_OK);
    }
}

static void mt_chan_wake_closed_waiters(mt_chan_t *ch) {
    mt_task_t *task = NULL;
    while ((task = mt_chan_waitq_pop(&ch->send_head, &ch->send_tail, &ch->send_waiters)) != NULL) {
        mt_chan_ready_waiter(task, MT_ERR_CLOSED);
    }
    while ((task = mt_chan_waitq_pop(&ch->recv_head, &ch->recv_tail, &ch->recv_waiters)) != NULL) {
        mt_chan_ready_waiter(task, MT_ERR_CLOSED);
    }

    mt_select_waiter_t *waiter = NULL;
    while ((waiter = mt_select_waitq_pop(&ch->select_send_head,
                                         &ch->select_send_tail,
                                         &ch->select_send_waiters)) != NULL) {
        mt_select_complete_waiter(waiter, MT_ERR_CLOSED);
    }
    while ((waiter = mt_select_waitq_pop(&ch->select_recv_head,
                                         &ch->select_recv_tail,
                                         &ch->select_recv_waiters)) != NULL) {
        mt_select_complete_waiter(waiter, MT_ERR_CLOSED);
    }
}

mt_chan_t *mt_chan_create(size_t elem_size, size_t capacity) {
    if (elem_size == 0) {
        return NULL;
    }
    if (!g_rt.initialized && mt_init() != MT_OK) {
        return NULL;
    }
    mt_lock();
    if (capacity > 0 && elem_size > (SIZE_MAX / capacity)) {
        mt_unlock();
        return NULL;
    }

    mt_chan_t *ch = mt_alloc_channel_memory();
    if (!ch) {
        mt_unlock();
        return NULL;
    }

    ch->elem_size = elem_size;
    ch->capacity = capacity;

    if (capacity > 0) {
        ch->buffer = mt_alloc_channel_buffer(elem_size * capacity);
        if (!ch->buffer) {
            mt_free_channel_memory(ch);
            mt_unlock();
            return NULL;
        }
    }

    mt_chan_register(ch);
    mt_unlock();
    return ch;
}

int mt_chan_send(mt_chan_t *ch, const void *value) {
    if (!ch || !value) {
        return MT_ERR_INVALID;
    }
    mt_lock();
    if (ch->closed) {
        mt_unlock();
        return MT_ERR_CLOSED;
    }

    mt_task_t *receiver = mt_chan_waitq_pop(&ch->recv_head, &ch->recv_tail, &ch->recv_waiters);
    if (receiver) {
        memcpy(receiver->chan_value, value, ch->elem_size);
        mt_chan_ready_waiter(receiver, MT_OK);
        mt_unlock();
        return MT_OK;
    }

    mt_select_waiter_t *select_receiver = mt_select_waitq_pop(&ch->select_recv_head,
                                                              &ch->select_recv_tail,
                                                              &ch->select_recv_waiters);
    if (select_receiver) {
        memcpy(select_receiver->value, value, ch->elem_size);
        mt_select_complete_waiter(select_receiver, MT_OK);
        mt_unlock();
        return MT_OK;
    }

    if (ch->capacity > 0 && ch->len < ch->capacity) {
        mt_chan_buffer_push(ch, value);
        mt_unlock();
        return MT_OK;
    }

    mt_task_t *task = mt_current_task();
    if (!task) {
        mt_unlock();
        return MT_ERR_STATE;
    }

    mt_task_block_on_channel(task, ch, MT_CHAN_WAIT_SEND, (void *)value);
    mt_chan_waitq_push(&ch->send_head, &ch->send_tail, &ch->send_waiters, task);
    mt_ctx_switch(&task->ctx, mt_current_scheduler_ctx());
    return task->chan_result;
}

int mt_chan_recv(mt_chan_t *ch, void *out) {
    if (!ch || !out) {
        return MT_ERR_INVALID;
    }
    mt_lock();

    if (ch->len > 0) {
        mt_chan_buffer_pop(ch, out);
        mt_chan_fill_buffer_from_waiting_sender(ch);
        mt_unlock();
        return MT_OK;
    }

    mt_task_t *sender = mt_chan_waitq_pop(&ch->send_head, &ch->send_tail, &ch->send_waiters);
    if (sender) {
        if (ch->closed) {
            mt_chan_ready_waiter(sender, MT_ERR_CLOSED);
            mt_unlock();
            return MT_ERR_CLOSED;
        }
        memcpy(out, sender->chan_value, ch->elem_size);
        mt_chan_ready_waiter(sender, MT_OK);
        mt_unlock();
        return MT_OK;
    }

    mt_select_waiter_t *select_sender = mt_select_waitq_pop(&ch->select_send_head,
                                                            &ch->select_send_tail,
                                                            &ch->select_send_waiters);
    if (select_sender) {
        if (ch->closed) {
            mt_select_complete_waiter(select_sender, MT_ERR_CLOSED);
            mt_unlock();
            return MT_ERR_CLOSED;
        }
        memcpy(out, select_sender->value, ch->elem_size);
        mt_select_complete_waiter(select_sender, MT_OK);
        mt_unlock();
        return MT_OK;
    }

    if (ch->closed) {
        mt_unlock();
        return MT_ERR_CLOSED;
    }

    mt_task_t *task = mt_current_task();
    if (!task) {
        mt_unlock();
        return MT_ERR_STATE;
    }

    mt_task_block_on_channel(task, ch, MT_CHAN_WAIT_RECV, out);
    mt_chan_waitq_push(&ch->recv_head, &ch->recv_tail, &ch->recv_waiters, task);
    mt_ctx_switch(&task->ctx, mt_current_scheduler_ctx());
    return task->chan_result;
}

int mt_chan_try_send(mt_chan_t *ch, const void *value) {
    if (!ch || !value) {
        return MT_ERR_INVALID;
    }
    mt_lock();
    if (ch->closed) {
        mt_unlock();
        return MT_ERR_CLOSED;
    }

    mt_task_t *receiver = mt_chan_waitq_pop(&ch->recv_head, &ch->recv_tail, &ch->recv_waiters);
    if (receiver) {
        memcpy(receiver->chan_value, value, ch->elem_size);
        mt_chan_ready_waiter(receiver, MT_OK);
        mt_unlock();
        return MT_OK;
    }

    mt_select_waiter_t *select_receiver = mt_select_waitq_pop(&ch->select_recv_head,
                                                              &ch->select_recv_tail,
                                                              &ch->select_recv_waiters);
    if (select_receiver) {
        memcpy(select_receiver->value, value, ch->elem_size);
        mt_select_complete_waiter(select_receiver, MT_OK);
        mt_unlock();
        return MT_OK;
    }

    if (ch->capacity > 0 && ch->len < ch->capacity) {
        mt_chan_buffer_push(ch, value);
        mt_unlock();
        return MT_OK;
    }

    mt_unlock();
    return MT_ERR_WOULD_BLOCK;
}

int mt_chan_try_recv(mt_chan_t *ch, void *out) {
    if (!ch || !out) {
        return MT_ERR_INVALID;
    }
    mt_lock();

    if (ch->len > 0) {
        mt_chan_buffer_pop(ch, out);
        mt_chan_fill_buffer_from_waiting_sender(ch);
        mt_unlock();
        return MT_OK;
    }

    mt_task_t *sender = mt_chan_waitq_pop(&ch->send_head, &ch->send_tail, &ch->send_waiters);
    if (sender) {
        if (ch->closed) {
            mt_chan_ready_waiter(sender, MT_ERR_CLOSED);
            mt_unlock();
            return MT_ERR_CLOSED;
        }
        memcpy(out, sender->chan_value, ch->elem_size);
        mt_chan_ready_waiter(sender, MT_OK);
        mt_unlock();
        return MT_OK;
    }

    mt_select_waiter_t *select_sender = mt_select_waitq_pop(&ch->select_send_head,
                                                            &ch->select_send_tail,
                                                            &ch->select_send_waiters);
    if (select_sender) {
        if (ch->closed) {
            mt_select_complete_waiter(select_sender, MT_ERR_CLOSED);
            mt_unlock();
            return MT_ERR_CLOSED;
        }
        memcpy(out, select_sender->value, ch->elem_size);
        mt_select_complete_waiter(select_sender, MT_OK);
        mt_unlock();
        return MT_OK;
    }

    int rc = ch->closed ? MT_ERR_CLOSED : MT_ERR_WOULD_BLOCK;
    mt_unlock();
    return rc;
}

static int mt_select_try_case(mt_select_case_t *scase) {
    if (!scase) {
        return MT_ERR_INVALID;
    }
    switch (scase->op) {
        case MT_SELECT_RECV:
            return mt_chan_try_recv(scase->ch, scase->value);
        case MT_SELECT_SEND:
            return mt_chan_try_send(scase->ch, scase->value);
        default:
            return MT_ERR_INVALID;
    }
}

int mt_select(mt_select_case_t *cases, size_t count, size_t *selected_index) {
    if (!cases || count == 0 || !selected_index) {
        return MT_ERR_INVALID;
    }

    size_t default_index = SIZE_MAX;
    size_t timeout_index = SIZE_MAX;
    uint64_t timeout_ms = 0;
    size_t channel_cases = 0;

    for (size_t i = 0; i < count; ++i) {
        switch (cases[i].op) {
            case MT_SELECT_RECV:
                if (!cases[i].ch || !cases[i].value) {
                    return MT_ERR_INVALID;
                }
                channel_cases++;
                break;
            case MT_SELECT_SEND:
                if (!cases[i].ch || !cases[i].value) {
                    return MT_ERR_INVALID;
                }
                channel_cases++;
                break;
            case MT_SELECT_DEFAULT:
                if (default_index != SIZE_MAX) {
                    return MT_ERR_INVALID;
                }
                default_index = i;
                break;
            case MT_SELECT_TIMEOUT:
                if (timeout_index != SIZE_MAX) {
                    return MT_ERR_INVALID;
                }
                timeout_index = i;
                timeout_ms = cases[i].timeout_ms;
                break;
            default:
                return MT_ERR_INVALID;
        }
    }

    for (size_t i = 0; i < count; ++i) {
        if (cases[i].op != MT_SELECT_RECV && cases[i].op != MT_SELECT_SEND) {
            continue;
        }
        int rc = mt_select_try_case(&cases[i]);
        if (rc != MT_ERR_WOULD_BLOCK) {
            *selected_index = i;
            return rc;
        }
    }

    if (default_index != SIZE_MAX) {
        *selected_index = default_index;
        return MT_OK;
    }

    if (timeout_index != SIZE_MAX && timeout_ms == 0) {
        *selected_index = timeout_index;
        return MT_OK;
    }

    mt_lock();
    mt_task_t *task = mt_current_task();
    if (!task) {
        mt_unlock();
        return MT_ERR_STATE;
    }
    if (channel_cases == 0 && timeout_index == SIZE_MAX) {
        mt_unlock();
        return MT_ERR_INVALID;
    }

    task->select_waiters = NULL;
    task->select_waiter_count = 0;
    task->select_result = MT_OK;
    task->select_index = timeout_index != SIZE_MAX ? timeout_index : 0;
    task->select_in_timer = 0;
    task->select_counted_waiting = 0;

    for (size_t i = 0; i < count; ++i) {
        if (cases[i].op != MT_SELECT_RECV && cases[i].op != MT_SELECT_SEND) {
            continue;
        }
        mt_select_waiter_t *waiter = mt_alloc_select_waiter();
        if (!waiter) {
            mt_select_free_task_waiters(task);
            mt_unlock();
            return MT_ERR_NOMEM;
        }
        waiter->task = task;
        waiter->ch = cases[i].ch;
        waiter->op = cases[i].op;
        waiter->value = cases[i].value;
        waiter->index = i;
        waiter->task_next = task->select_waiters;
        task->select_waiters = waiter;
        task->select_waiter_count++;

        if (waiter->op == MT_SELECT_SEND) {
            mt_select_waitq_push(&waiter->ch->select_send_head,
                                 &waiter->ch->select_send_tail,
                                 &waiter->ch->select_send_waiters,
                                 waiter);
        } else {
            mt_select_waitq_push(&waiter->ch->select_recv_head,
                                 &waiter->ch->select_recv_tail,
                                 &waiter->ch->select_recv_waiters,
                                 waiter);
        }
    }

    if (timeout_index != SIZE_MAX) {
        uint64_t now_ns = mt_now_ns();
        uint64_t timeout_ns = timeout_ms > (UINT64_MAX / MT_NS_PER_MS)
            ? UINT64_MAX
            : timeout_ms * MT_NS_PER_MS;
        uint64_t deadline_ns = UINT64_MAX - now_ns < timeout_ns
            ? UINT64_MAX
            : now_ns + timeout_ns;
        task->select_index = timeout_index;
        if (mt_timer_push_state(task, deadline_ns, MT_TASK_WAITING_SELECT) != MT_OK) {
            mt_select_free_task_waiters(task);
            mt_unlock();
            return MT_ERR_NOMEM;
        }
        task->select_in_timer = 1;
    } else {
        mt_task_block_on_select(task);
    }

    if (!task->select_counted_waiting) {
        mt_task_block_on_select(task);
    }
    mt_ctx_switch(&task->ctx, mt_current_scheduler_ctx());

    *selected_index = task->select_index;
    return task->select_result;
}

int mt_chan_close(mt_chan_t *ch) {
    if (!ch) {
        return MT_ERR_INVALID;
    }
    mt_lock();
    if (ch->closed) {
        mt_unlock();
        return MT_ERR_CLOSED;
    }
    ch->closed = 1;
    mt_chan_wake_closed_waiters(ch);
    mt_unlock();
    return MT_OK;
}

int mt_chan_destroy(mt_chan_t *ch) {
    if (!ch) {
        return MT_ERR_INVALID;
    }
    mt_lock();

    if (ch->send_waiters || ch->recv_waiters) {
        mt_unlock();
        return MT_ERR_STATE;
    }

    /*
     * v0.5 select waiters must not be left blocked when a channel is
     * destroyed.  Preserve the v0.3 blocking send/recv contract above while
     * making select destruction behave like a terminal close.
     */
    if (ch->select_send_waiters || ch->select_recv_waiters) {
        ch->closed = 1;
        mt_chan_wake_closed_waiters(ch);
    }

    mt_chan_unregister(ch);
    mt_free_channel_buffer(ch->buffer);
    memset(ch, 0, sizeof(*ch));
    mt_free_channel_memory(ch);
    mt_unlock();
    return MT_OK;
}

size_t mt_chan_len(const mt_chan_t *ch) {
    if (!ch) {
        return 0;
    }
    mt_lock();
    size_t len = ch->len;
    mt_unlock();
    return len;
}

size_t mt_chan_capacity(const mt_chan_t *ch) {
    if (!ch) {
        return 0;
    }
    mt_lock();
    size_t cap = ch->capacity;
    mt_unlock();
    return cap;
}

int mt_chan_is_closed(const mt_chan_t *ch) {
    if (!ch) {
        return 0;
    }
    mt_lock();
    int closed = ch->closed;
    mt_unlock();
    return closed;
}

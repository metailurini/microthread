/* Internal implementation shard included by microthread.c. */

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
    if (task->select_counted_waiting) {
        if (g_rt.channel_waiting_count > 0) {
            g_rt.channel_waiting_count--;
        }
        task->select_counted_waiting = 0;
    }
    task->select_result = result;
    task->select_index = index;
    task->state = MT_TASK_READY;
    mt_runq_push(task);
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
    if (task->select_counted_waiting) {
        if (g_rt.channel_waiting_count > 0) {
            g_rt.channel_waiting_count--;
        }
        task->select_counted_waiting = 0;
    }
    task->select_result = MT_OK;
    task->state = MT_TASK_READY;
    mt_runq_push(task);
}

static void mt_chan_ready_waiter(mt_task_t *task, int result) {
    task->chan_result = result;
    task->chan_wait_kind = MT_CHAN_WAIT_NONE;
    task->chan_wait_ch = NULL;
    task->chan_value = NULL;
    if (g_rt.channel_waiting_count > 0) {
        g_rt.channel_waiting_count--;
    }
    task->state = MT_TASK_READY;
    mt_runq_push(task);
}


static int mt_chan_remove_waiter(mt_task_t *task) {
    mt_chan_t *ch = task->chan_wait_ch;
    int removed = 0;
    if (!ch) {
        return 0;
    }
    if (task->chan_wait_kind == MT_CHAN_WAIT_SEND) {
        removed = mt_waitq_remove(&ch->send_head, &ch->send_tail, &ch->send_waiters, task);
    } else if (task->chan_wait_kind == MT_CHAN_WAIT_RECV) {
        removed = mt_waitq_remove(&ch->recv_head, &ch->recv_tail, &ch->recv_waiters, task);
    }
    if (removed && g_rt.channel_waiting_count > 0) {
        g_rt.channel_waiting_count--;
    }
    task->chan_wait_kind = MT_CHAN_WAIT_NONE;
    task->chan_wait_ch = NULL;
    task->chan_value = NULL;
    return removed;
}

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
    task->join_result = result;
    task->join_waiting_on = NULL;
    if (g_rt.join_waiting_count > 0) {
        g_rt.join_waiting_count--;
    }
    task->state = MT_TASK_READY;
    mt_runq_push(task);
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

static void mt_task_after_switch(mt_task_t *task) {
    g_tls_current = NULL;
    g_rt.current = NULL;
    if (g_rt.running_tasks > 0) {
        g_rt.running_tasks--;
    }

    if (task->state == MT_TASK_READY) {
        if (task->handle && !task->handle->completed) {
            task->handle->status = task->handle->cancel_requested
                ? MT_TASK_STATUS_CANCELLED
                : MT_TASK_STATUS_READY;
        }
        mt_runq_push(task);
    } else if (task->state == MT_TASK_SLEEPING) {
        if (task->handle && !task->handle->completed) {
            task->handle->status = task->handle->cancel_requested
                ? MT_TASK_STATUS_CANCELLED
                : MT_TASK_STATUS_SLEEPING;
        }
    } else if (task->state == MT_TASK_WAITING_CHAN) {
        if (task->handle && !task->handle->completed) {
            task->handle->status = task->handle->cancel_requested
                ? MT_TASK_STATUS_CANCELLED
                : MT_TASK_STATUS_WAITING_CHAN;
        }
    } else if (task->state == MT_TASK_WAITING_SELECT) {
        if (task->handle && !task->handle->completed) {
            task->handle->status = task->handle->cancel_requested
                ? MT_TASK_STATUS_CANCELLED
                : MT_TASK_STATUS_WAITING_SELECT;
        }
    } else if (task->state == MT_TASK_WAITING_FD) {
        if (task->handle && !task->handle->completed) {
            task->handle->status = task->handle->cancel_requested
                ? MT_TASK_STATUS_CANCELLED
                : MT_TASK_STATUS_WAITING_FD;
        }
    } else if (task->state == MT_TASK_WAITING_JOIN) {
        if (task->handle && !task->handle->completed) {
            task->handle->status = task->handle->cancel_requested
                ? MT_TASK_STATUS_CANCELLED
                : MT_TASK_STATUS_WAITING_JOIN;
        }
    } else if (task->state == MT_TASK_DEAD) {
        if (g_rt.live_count > 0) {
            g_rt.live_count--;
        }
        g_rt.completed_count++;
        if (task->handle) {
            mt_handle_complete(task->handle, task->handle->cancel_requested);
        }
        mt_task_destroy(task);
    } else {
        g_rt.run_result = MT_ERR_STATE;
        g_rt.stopping = 1;
        mt_notify_all();
    }
}

#if MT_HAS_OS_THREADS
void mt_cond_timedwait_ns(uint64_t delay_ns) {
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    uint64_t nsec = (uint64_t)ts.tv_nsec + (delay_ns % UINT64_C(1000000000));
    ts.tv_sec += (time_t)(delay_ns / UINT64_C(1000000000));
    if (nsec >= UINT64_C(1000000000)) {
        ts.tv_sec++;
        nsec -= UINT64_C(1000000000);
    }
    ts.tv_nsec = (long)nsec;
    pthread_cond_timedwait(&g_rt.cond, &g_rt.lock, &ts);
}
#endif

static void mt_worker_loop(mt_context_t *scheduler_ctx, int worker_index, int sleep_for_timers) {
    g_tls_scheduler_ctx = scheduler_ctx;
    g_tls_worker_index = worker_index;

    mt_lock();
    g_rt.active_workers++;
    for (;;) {
        if (g_rt.stopping &&
            (g_rt.run_result != MT_ERR_CANCELLED || g_rt.runq_head == NULL)) {
            break;
        }

        uint64_t now_ns = mt_now_ns();
        if (!g_rt.stopping) {
            mt_wake_expired_timers(now_ns);
            mt_poll_fd_waiters_once(now_ns);
        }

        mt_task_t *task = mt_runq_pop();
        if (!task) {
            if (g_rt.stopping) {
                break;
            }
            if (g_rt.live_count == 0) {
                g_rt.stopping = 1;
                mt_notify_all();
                break;
            }

            mt_task_t *next_timer = mt_timer_peek();
            if (g_rt.fd_waiting_count > 0) {
                int poll_timeout_ms = 10;
                if (next_timer) {
                    uint64_t now_for_poll = mt_now_ns();
                    if (next_timer->wake_ns <= now_for_poll) {
                        poll_timeout_ms = 0;
                    } else {
                        uint64_t delay_ns = next_timer->wake_ns - now_for_poll;
                        uint64_t delay_ms = (delay_ns + MT_NS_PER_MS - 1u) / MT_NS_PER_MS;
                        if (delay_ms < (uint64_t)poll_timeout_ms) {
                            poll_timeout_ms = (int)delay_ms;
                        }
                    }
                }
                mt_poll_fd_waiters_with_timeout(poll_timeout_ms);
                continue;
            }
            if (!next_timer) {
                if (g_rt.running_tasks == 0 &&
                    (g_rt.channel_waiting_count > 0 ||
                     g_rt.join_waiting_count > 0 ||
                     g_rt.fd_waiting_count > 0)) {
                    g_rt.run_result = MT_ERR_STATE;
                    g_rt.stopping = 1;
                    mt_notify_all();
                    break;
                }
#if MT_HAS_OS_THREADS
                pthread_cond_wait(&g_rt.cond, &g_rt.lock);
#else
                break;
#endif
                continue;
            }

            uint64_t now_ns = mt_now_ns();
            if (next_timer->wake_ns > now_ns) {
                if (!sleep_for_timers) {
                    g_rt.run_result = MT_ERR_STATE;
                    g_rt.stopping = 1;
                    mt_notify_all();
                    break;
                }
#if MT_HAS_OS_THREADS
                mt_cond_timedwait_ns(next_timer->wake_ns - now_ns);
#else
                mt_unlock();
                mt_sleep_os_ns(next_timer->wake_ns - now_ns);
                mt_lock();
#endif
            }
            continue;
        }

        g_tls_current = task;
        g_rt.current = task;
        g_rt.running_tasks++;
        task->state = MT_TASK_RUNNING;
        if (task->handle && !task->handle->completed) {
            task->handle->status = task->handle->cancel_requested
                ? MT_TASK_STATUS_CANCELLED
                : MT_TASK_STATUS_RUNNING;
        }
        mt_unlock();
        mt_ctx_switch(scheduler_ctx, &task->ctx);
        /* Tasks switch back to their worker scheduler with g_rt.lock held. */
        mt_task_after_switch(task);
    }
    if (g_rt.active_workers > 0) {
        g_rt.active_workers--;
    }
    mt_unlock();
    g_tls_current = NULL;
    g_tls_scheduler_ctx = NULL;
    g_tls_worker_index = 0;
}

#if MT_HAS_OS_THREADS
static void *mt_worker_thread_main(void *arg) {
    int worker_index = (int)(intptr_t)arg;
    mt_context_t scheduler_ctx;
    if (mt_ctx_init_scheduler(&scheduler_ctx) != 0) {
        mt_lock();
        g_rt.run_result = MT_ERR;
        g_rt.stopping = 1;
        mt_notify_all();
        mt_unlock();
        return NULL;
    }
    mt_worker_loop(&scheduler_ctx, worker_index, 1);
    mt_ctx_destroy(&scheduler_ctx);
    return NULL;
}
#endif

static int mt_run_internal(size_t worker_count, int sleep_for_timers) {
    if (!g_rt.initialized && mt_init() != MT_OK) {
        return MT_ERR;
    }
    if (mt_current_task()) {
        return MT_ERR_STATE;
    }

    mt_lock();
    if (g_rt.running) {
        mt_unlock();
        return MT_ERR_STATE;
    }
    g_rt.running = 1;
    g_rt.stopping = 0;
    g_rt.run_result = MT_OK;
    g_rt.worker_count = worker_count;
    mt_unlock();

#if MT_HAS_OS_THREADS
    if (worker_count > 1 && sleep_for_timers) {
        g_rt.workers = (pthread_t *)calloc(worker_count - 1u, sizeof(*g_rt.workers));
        if (!g_rt.workers) {
            mt_lock();
            g_rt.running = 0;
            mt_unlock();
            return MT_ERR_NOMEM;
        }
        g_rt.worker_threads = worker_count - 1u;
        for (size_t i = 0; i < g_rt.worker_threads; ++i) {
            if (pthread_create(&g_rt.workers[i], NULL, mt_worker_thread_main, (void *)(intptr_t)(i + 1u)) != 0) {
                mt_lock();
                g_rt.run_result = MT_ERR;
                g_rt.stopping = 1;
                mt_notify_all();
                mt_unlock();
                for (size_t j = 0; j < i; ++j) {
                    pthread_join(g_rt.workers[j], NULL);
                }
                free(g_rt.workers);
                g_rt.workers = NULL;
                g_rt.worker_threads = 0;
                mt_lock();
                g_rt.running = 0;
                mt_unlock();
                return MT_ERR;
            }
        }
    }
#endif

    mt_worker_loop(&g_rt.scheduler_ctx, 0, sleep_for_timers);

#if MT_HAS_OS_THREADS
    for (size_t i = 0; i < g_rt.worker_threads; ++i) {
        pthread_join(g_rt.workers[i], NULL);
    }
    free(g_rt.workers);
    g_rt.workers = NULL;
    g_rt.worker_threads = 0;
#endif

    mt_lock();
    int rc = g_rt.run_result;
    g_rt.running = 0;
    g_rt.stopping = 0;
    mt_unlock();
    return rc;
}

int mt_run(void) {
    if (!g_rt.initialized && mt_init() != MT_OK) {
        return MT_ERR;
    }
    return mt_run_internal(1, 1);
}

int mt_runtime_start(size_t worker_count) {
    if (worker_count == 0) {
        return MT_ERR_INVALID;
    }
    if (!g_rt.initialized && mt_init() != MT_OK) {
        return MT_ERR;
    }
    if (mt_current_task()) {
        return MT_ERR_STATE;
    }
    mt_lock();
    int already_running = g_rt.running;
    mt_unlock();
    if (already_running) {
        return MT_ERR_STATE;
    }
    return mt_run_internal(worker_count, 1);
}

int mt_runtime_workers(void) {
    if (!g_rt.initialized) {
        return 0;
    }
    mt_lock();
    size_t worker_count = g_rt.running ? g_rt.worker_count : 0;
    mt_unlock();
    if (worker_count > (size_t)INT32_MAX) {
        return INT32_MAX;
    }
    return (int)worker_count;
}

int mt_run_workers(size_t worker_count) {
    return mt_runtime_start(worker_count);
}

#ifdef MT_TESTING
int mt_test_run_until_blocked(void) {
    return mt_run_internal(1, 0);
}
#endif

void mt_yield(void) {
    mt_task_t *task = mt_current_task();
    if (!task) {
        return;
    }

    mt_lock();
    task->state = MT_TASK_READY;
    mt_ctx_switch(&task->ctx, mt_current_scheduler_ctx());
}

void mt_sleep_ms(uint64_t ms) {
    mt_task_t *task = mt_current_task();
    if (!task) {
        return;
    }

    if (ms == 0) {
        mt_yield();
        return;
    }

    mt_lock();
    uint64_t now_ns = mt_now_ns();
    uint64_t sleep_ns = ms > (UINT64_MAX / MT_NS_PER_MS)
        ? UINT64_MAX
        : ms * MT_NS_PER_MS;
    uint64_t deadline_ns = UINT64_MAX - now_ns < sleep_ns
        ? UINT64_MAX
        : now_ns + sleep_ns;

    if (mt_timer_push(task, deadline_ns) != MT_OK) {
        /*
         * mt_sleep_ms() has no error return in the v0.2 API.  If the timer
         * heap cannot grow, preserve safety and approximate semantics by
         * blocking the owning OS thread for this sleep instead of corrupting
         * scheduler queues.
         */
        mt_unlock();
        mt_sleep_os_ns(sleep_ns);
        return;
    }

    mt_ctx_switch(&task->ctx, mt_current_scheduler_ctx());
}

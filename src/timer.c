/* Internal implementation shard included by microthread.c. */

static uint64_t mt_now_ns_raw(int *ok) {
#if defined(_WIN32)
    static LARGE_INTEGER frequency;
    LARGE_INTEGER counter;
    if (frequency.QuadPart == 0) {
        if (!QueryPerformanceFrequency(&frequency)) {
            if (ok) {
                *ok = 0;
            }
            return 0;
        }
    }
    if (!QueryPerformanceCounter(&counter)) {
        if (ok) {
            *ok = 0;
        }
        return 0;
    }
    if (ok) {
        *ok = 1;
    }
    return (uint64_t)((counter.QuadPart * UINT64_C(1000000000)) / frequency.QuadPart);
#else
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) {
        if (ok) {
            *ok = 0;
        }
        return 0;
    }
    if (ok) {
        *ok = 1;
    }
    return ((uint64_t)ts.tv_sec * UINT64_C(1000000000)) + (uint64_t)ts.tv_nsec;
#endif
}

uint64_t mt_now_ns(void) {
    static _Thread_local uint64_t last_good_ns;

#ifdef MT_TESTING
    if (g_fail_next_clock_read) {
        g_fail_next_clock_read = 0;
        if (last_good_ns != 0) {
            return last_good_ns;
        }
        int fallback_ok = 0;
        uint64_t fallback = mt_now_ns_raw(&fallback_ok);
        if (fallback_ok) {
            last_good_ns = fallback;
            return fallback;
        }
        return 0;
    }
#endif

    int ok = 0;
    uint64_t now = mt_now_ns_raw(&ok);
    if (ok) {
        last_good_ns = now;
        return now;
    }
    return last_good_ns;
}

static void mt_sleep_os_ns(uint64_t ns) {
    if (ns == 0) {
        return;
    }
#if defined(_WIN32)
    DWORD ms = (DWORD)((ns + MT_NS_PER_MS - 1) / MT_NS_PER_MS);
    Sleep(ms);
#else
    struct timespec req;
    req.tv_sec = (time_t)(ns / UINT64_C(1000000000));
    req.tv_nsec = (long)(ns % UINT64_C(1000000000));
    while (nanosleep(&req, &req) != 0 && errno == EINTR) {
    }
#endif
}

static int mt_timer_less(const mt_task_t *a, const mt_task_t *b) {
    if (a->wake_ns != b->wake_ns) {
        return a->wake_ns < b->wake_ns;
    }
    return a->timer_seq < b->timer_seq;
}

static int mt_timer_reserve(size_t needed) {
    if (g_rt.timers.cap >= needed) {
        return MT_OK;
    }
    size_t new_cap = g_rt.timers.cap ? g_rt.timers.cap * 2u : 16u;
    while (new_cap < needed) {
        new_cap *= 2u;
    }

    size_t bytes = new_cap * sizeof(g_rt.timers.items[0]);
    void *new_items = g_rt.timers.items
        ? mt_realloc_timer_memory(g_rt.timers.items, bytes)
        : mt_alloc_timer_memory(bytes);
    if (!new_items) {
        return MT_ERR_NOMEM;
    }
    g_rt.timers.items = (mt_task_t **)new_items;
    g_rt.timers.cap = new_cap;
    return MT_OK;
}

static void mt_timer_swap(size_t a, size_t b) {
    mt_task_t *tmp = g_rt.timers.items[a];
    g_rt.timers.items[a] = g_rt.timers.items[b];
    g_rt.timers.items[b] = tmp;
}

int mt_timer_push_state(mt_task_t *task, uint64_t deadline_ns, mt_task_state_t state) {
    if (mt_timer_reserve(g_rt.timers.len + 1u) != MT_OK) {
        return MT_ERR_NOMEM;
    }

    task->wake_ns = deadline_ns;
    task->timer_seq = g_rt.timers.next_seq++;
    task->state = state;

    size_t i = g_rt.timers.len++;
    g_rt.timers.items[i] = task;
    while (i > 0) {
        size_t parent = (i - 1u) / 2u;
        if (!mt_timer_less(g_rt.timers.items[i], g_rt.timers.items[parent])) {
            break;
        }
        mt_timer_swap(i, parent);
        i = parent;
    }
    mt_notify_all();
    return MT_OK;
}

static int mt_timer_push(mt_task_t *task, uint64_t deadline_ns) {
    return mt_timer_push_state(task, deadline_ns, MT_TASK_SLEEPING);
}

static mt_task_t *mt_timer_pop(void) {
    if (g_rt.timers.len == 0) {
        return NULL;
    }
    mt_task_t *top = g_rt.timers.items[0];
    g_rt.timers.len--;
    if (g_rt.timers.len > 0) {
        g_rt.timers.items[0] = g_rt.timers.items[g_rt.timers.len];
        size_t i = 0;
        for (;;) {
            size_t left = i * 2u + 1u;
            size_t right = left + 1u;
            size_t smallest = i;
            if (left < g_rt.timers.len && mt_timer_less(g_rt.timers.items[left], g_rt.timers.items[smallest])) {
                smallest = left;
            }
            if (right < g_rt.timers.len && mt_timer_less(g_rt.timers.items[right], g_rt.timers.items[smallest])) {
                smallest = right;
            }
            if (smallest == i) {
                break;
            }
            mt_timer_swap(i, smallest);
            i = smallest;
        }
    }
    top->wake_ns = 0;
    return top;
}

int mt_timer_remove(mt_task_t *task) {
    for (size_t i = 0; i < g_rt.timers.len; ++i) {
        if (g_rt.timers.items[i] == task) {
            g_rt.timers.len--;
            if (i != g_rt.timers.len) {
                g_rt.timers.items[i] = g_rt.timers.items[g_rt.timers.len];
                while (i > 0) {
                    size_t parent = (i - 1u) / 2u;
                    if (!mt_timer_less(g_rt.timers.items[i], g_rt.timers.items[parent])) {
                        break;
                    }
                    mt_timer_swap(i, parent);
                    i = parent;
                }
                for (;;) {
                    size_t left = i * 2u + 1u;
                    size_t right = left + 1u;
                    size_t smallest = i;
                    if (left < g_rt.timers.len && mt_timer_less(g_rt.timers.items[left], g_rt.timers.items[smallest])) {
                        smallest = left;
                    }
                    if (right < g_rt.timers.len && mt_timer_less(g_rt.timers.items[right], g_rt.timers.items[smallest])) {
                        smallest = right;
                    }
                    if (smallest == i) {
                        break;
                    }
                    mt_timer_swap(i, smallest);
                    i = smallest;
                }
            }
            task->wake_ns = 0;
            return 1;
        }
    }
    return 0;
}

static mt_task_t *mt_timer_peek(void) {
    return g_rt.timers.len ? g_rt.timers.items[0] : NULL;
}

static void mt_wake_expired_timers(uint64_t now_ns) {
    for (;;) {
        mt_task_t *task = mt_timer_peek();
        if (!task || task->wake_ns > now_ns) {
            break;
        }
        task = mt_timer_pop();
        if (task->state == MT_TASK_WAITING_SELECT) {
            mt_select_timeout_ready(task);
        } else if (task->state == MT_TASK_WAITING_FD) {
            mt_fd_timeout_ready(task);
        } else {
            task->state = MT_TASK_READY;
            mt_runq_push(task);
        }
    }
}

#if !defined(_WIN32) && !defined(_POSIX_C_SOURCE)
#define _POSIX_C_SOURCE 200809L
#endif

#include "gt.h"

#include "context.h"

#include <errno.h>
#include <limits.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#define GT_HAS_OS_THREADS 0
#else
#include <pthread.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>
#define GT_HAS_OS_THREADS 1
#if !defined(MAP_ANONYMOUS) && defined(MAP_ANON)
#define MAP_ANONYMOUS MAP_ANON
#endif
#endif

#define GT_NS_PER_MS UINT64_C(1000000)

typedef enum gt_task_state {
    GT_TASK_READY = 0,
    GT_TASK_RUNNING,
    GT_TASK_SLEEPING,
    GT_TASK_WAITING_CHAN,
    GT_TASK_WAITING_SELECT,
    GT_TASK_WAITING_JOIN,
    GT_TASK_DEAD
} gt_task_state_t;

typedef enum gt_chan_wait_kind {
    GT_CHAN_WAIT_NONE = 0,
    GT_CHAN_WAIT_SEND,
    GT_CHAN_WAIT_RECV
} gt_chan_wait_kind_t;

typedef struct gt_stack {
    void *mapping;
    size_t mapping_size;
    void *usable;
    size_t usable_size;
    size_t guard_size;
    int alloc_kind;
} gt_stack_t;

typedef struct gt_select_waiter gt_select_waiter_t;

enum {
    GT_STACK_ALLOC_NONE = 0,
    GT_STACK_ALLOC_MMAP = 1,
    GT_STACK_ALLOC_MALLOC = 2
};

typedef struct gt_task {
    int id;
    gt_fn fn;
    void *arg;
    gt_stack_t stack;
    gt_context_t ctx;
    gt_task_state_t state;
    uint64_t wake_ns;
    uint64_t timer_seq;
    struct gt_task *next;
    struct gt_task *wait_next;
    struct gt_task *all_next;
    gt_task_handle_t *handle;
    gt_task_handle_t *join_waiting_on;
    gt_chan_wait_kind_t chan_wait_kind;
    gt_chan_t *chan_wait_ch;
    void *chan_value;
    int chan_result;
    int join_result;
    gt_select_waiter_t *select_waiters;
    size_t select_waiter_count;
    size_t select_index;
    int select_result;
    int select_in_timer;
    int select_counted_waiting;
} gt_task_t;

struct gt_select_waiter {
    gt_task_t *task;
    gt_chan_t *ch;
    gt_select_op_t op;
    void *value;
    size_t index;
    int active;
    gt_select_waiter_t *task_next;
    gt_select_waiter_t *chan_next;
};

struct gt_task_handle {
    gt_task_t *task;
    gt_task_status_t status;
    int cancel_requested;
    int completed;
    int released;
    int join_result;
    gt_task_t *join_head;
    gt_task_t *join_tail;
    size_t join_waiters;
    struct gt_task_handle *registry_next;
};

struct gt_chan {
    size_t elem_size;
    size_t capacity;
    size_t len;
    size_t head;
    size_t tail;
    unsigned char *buffer;
    int closed;
    gt_task_t *send_head;
    gt_task_t *send_tail;
    gt_task_t *recv_head;
    gt_task_t *recv_tail;
    size_t send_waiters;
    size_t recv_waiters;
    gt_select_waiter_t *select_send_head;
    gt_select_waiter_t *select_send_tail;
    gt_select_waiter_t *select_recv_head;
    gt_select_waiter_t *select_recv_tail;
    size_t select_send_waiters;
    size_t select_recv_waiters;
    struct gt_chan *registry_next;
};

typedef struct gt_timer_heap {
    gt_task_t **items;
    size_t len;
    size_t cap;
    uint64_t next_seq;
} gt_timer_heap_t;

typedef struct gt_runtime {
    int initialized;
    int running;
    int stopping;
    int run_result;
    int next_id;
    size_t worker_count;
    size_t active_workers;
    size_t running_tasks;
#if GT_HAS_OS_THREADS
    pthread_mutex_t lock;
    pthread_cond_t cond;
    pthread_t *workers;
    size_t worker_threads;
#endif
    gt_context_t scheduler_ctx;
    gt_task_t *current;
    gt_task_t *all_tasks;
    gt_task_t *runq_head;
    gt_task_t *runq_tail;
    gt_chan_t *channels;
    gt_task_handle_t *handles;
    gt_timer_heap_t timers;
    size_t runnable_count;
    size_t live_count;
    size_t completed_count;
    size_t channel_waiting_count;
    size_t join_waiting_count;
} gt_runtime_t;

static gt_runtime_t g_rt;

#if GT_HAS_OS_THREADS
static __thread gt_task_t *g_tls_current;
static __thread gt_context_t *g_tls_scheduler_ctx;
static __thread int g_tls_worker_index;
#else
static gt_task_t *g_tls_current;
static gt_context_t *g_tls_scheduler_ctx;
static int g_tls_worker_index;
#endif

static void gt_lock(void) {
#if GT_HAS_OS_THREADS
    if (g_rt.initialized) {
        pthread_mutex_lock(&g_rt.lock);
    }
#endif
}

static void gt_unlock(void) {
#if GT_HAS_OS_THREADS
    if (g_rt.initialized) {
        pthread_mutex_unlock(&g_rt.lock);
    }
#endif
}

static void gt_notify_one(void) {
#if GT_HAS_OS_THREADS
    if (g_rt.initialized) {
        pthread_cond_signal(&g_rt.cond);
    }
#endif
}

static void gt_notify_all(void) {
#if GT_HAS_OS_THREADS
    if (g_rt.initialized) {
        pthread_cond_broadcast(&g_rt.cond);
    }
#endif
}

static gt_task_t *gt_current_task(void) {
    return g_tls_current;
}

static gt_context_t *gt_current_scheduler_ctx(void) {
    return g_tls_scheduler_ctx ? g_tls_scheduler_ctx : &g_rt.scheduler_ctx;
}

static void gt_runq_push(gt_task_t *task);
static void gt_select_timeout_ready(gt_task_t *task);

static void gt_task_register(gt_task_t *task) {
    task->all_next = g_rt.all_tasks;
    g_rt.all_tasks = task;
}

static void gt_task_unregister(gt_task_t *task) {
    gt_task_t **link = &g_rt.all_tasks;
    while (*link) {
        if (*link == task) {
            *link = task->all_next;
            task->all_next = NULL;
            return;
        }
        link = &(*link)->all_next;
    }
}

static void gt_chan_register(gt_chan_t *ch) {
    ch->registry_next = g_rt.channels;
    g_rt.channels = ch;
}

static void gt_chan_unregister(gt_chan_t *ch) {
    gt_chan_t **link = &g_rt.channels;
    while (*link) {
        if (*link == ch) {
            *link = ch->registry_next;
            ch->registry_next = NULL;
            return;
        }
        link = &(*link)->registry_next;
    }
}

static void gt_handle_register(gt_task_handle_t *handle) {
    handle->registry_next = g_rt.handles;
    g_rt.handles = handle;
}

static void gt_handle_unregister(gt_task_handle_t *handle) {
    gt_task_handle_t **link = &g_rt.handles;
    while (*link) {
        if (*link == handle) {
            *link = handle->registry_next;
            handle->registry_next = NULL;
            return;
        }
        link = &(*link)->registry_next;
    }
}

#ifdef GT_TESTING
static int g_fail_next_task_alloc;
static int g_fail_next_stack_alloc;
static int g_fail_next_context_make;
static int g_fail_next_timer_alloc;
static int g_fail_next_clock_read;
static int g_fail_next_channel_alloc;
static int g_fail_next_channel_buffer_alloc;
static int g_fail_next_handle_alloc;
static int g_fail_next_select_alloc;
static size_t g_task_allocs;
static size_t g_task_frees;
static size_t g_stack_allocs;
static size_t g_stack_frees;
static size_t g_timer_allocs;
static size_t g_timer_frees;
static size_t g_channel_allocs;
static size_t g_channel_frees;
static size_t g_channel_buffer_allocs;
static size_t g_channel_buffer_frees;
static size_t g_handle_allocs;
static size_t g_handle_frees;
static size_t g_select_allocs;
static size_t g_select_frees;

static void *gt_alloc_task_memory(size_t size) {
    if (g_fail_next_task_alloc) {
        g_fail_next_task_alloc = 0;
        return NULL;
    }
    void *ptr = calloc(1, size);
    if (ptr) {
        g_task_allocs++;
    }
    return ptr;
}

static void gt_free_task_memory(void *ptr) {
    if (ptr) {
        g_task_frees++;
    }
    free(ptr);
}

static int gt_make_context(gt_context_t *ctx,
                           void *stack,
                           size_t stack_size,
                           void (*entry)(void *),
                           void *arg) {
    if (g_fail_next_context_make) {
        g_fail_next_context_make = 0;
        return -1;
    }
    return gt_ctx_make(ctx, stack, stack_size, entry, arg);
}

static void *gt_alloc_timer_memory(size_t size) {
    if (g_fail_next_timer_alloc) {
        g_fail_next_timer_alloc = 0;
        return NULL;
    }
    void *ptr = realloc(NULL, size);
    if (ptr) {
        g_timer_allocs++;
    }
    return ptr;
}

static void *gt_realloc_timer_memory(void *ptr, size_t size) {
    if (g_fail_next_timer_alloc) {
        g_fail_next_timer_alloc = 0;
        return NULL;
    }
    void *new_ptr = realloc(ptr, size);
    if (new_ptr && !ptr) {
        g_timer_allocs++;
    }
    return new_ptr;
}

static void gt_free_timer_memory(void *ptr) {
    if (ptr) {
        g_timer_frees++;
    }
    free(ptr);
}

static gt_chan_t *gt_alloc_channel_memory(void) {
    if (g_fail_next_channel_alloc) {
        g_fail_next_channel_alloc = 0;
        return NULL;
    }
    gt_chan_t *ch = (gt_chan_t *)calloc(1, sizeof(*ch));
    if (ch) {
        g_channel_allocs++;
    }
    return ch;
}

static void gt_free_channel_memory(gt_chan_t *ch) {
    if (ch) {
        g_channel_frees++;
    }
    free(ch);
}

static unsigned char *gt_alloc_channel_buffer(size_t size) {
    if (g_fail_next_channel_buffer_alloc) {
        g_fail_next_channel_buffer_alloc = 0;
        return NULL;
    }
    unsigned char *buffer = (unsigned char *)malloc(size);
    if (buffer) {
        g_channel_buffer_allocs++;
    }
    return buffer;
}

static void gt_free_channel_buffer(unsigned char *buffer) {
    if (buffer) {
        g_channel_buffer_frees++;
    }
    free(buffer);
}

static gt_task_handle_t *gt_alloc_handle_memory(void) {
    if (g_fail_next_handle_alloc) {
        g_fail_next_handle_alloc = 0;
        return NULL;
    }
    gt_task_handle_t *handle = (gt_task_handle_t *)calloc(1, sizeof(*handle));
    if (handle) {
        g_handle_allocs++;
    }
    return handle;
}

static void gt_free_handle_memory(gt_task_handle_t *handle) {
    if (handle) {
        g_handle_frees++;
    }
    free(handle);
}

static gt_select_waiter_t *gt_alloc_select_waiter(void) {
    if (g_fail_next_select_alloc) {
        g_fail_next_select_alloc = 0;
        return NULL;
    }
    gt_select_waiter_t *waiter = (gt_select_waiter_t *)calloc(1, sizeof(*waiter));
    if (waiter) {
        g_select_allocs++;
    }
    return waiter;
}

static void gt_free_select_waiter(gt_select_waiter_t *waiter) {
    if (waiter) {
        g_select_frees++;
    }
    free(waiter);
}

#else
static void *gt_alloc_task_memory(size_t size) {
    return calloc(1, size);
}

static void gt_free_task_memory(void *ptr) {
    free(ptr);
}

static int gt_make_context(gt_context_t *ctx,
                           void *stack,
                           size_t stack_size,
                           void (*entry)(void *),
                           void *arg) {
    return gt_ctx_make(ctx, stack, stack_size, entry, arg);
}

static void *gt_alloc_timer_memory(size_t size) {
    return realloc(NULL, size);
}

static void *gt_realloc_timer_memory(void *ptr, size_t size) {
    return realloc(ptr, size);
}

static void gt_free_timer_memory(void *ptr) {
    free(ptr);
}

static gt_chan_t *gt_alloc_channel_memory(void) {
    return (gt_chan_t *)calloc(1, sizeof(gt_chan_t));
}

static void gt_free_channel_memory(gt_chan_t *ch) {
    free(ch);
}

static unsigned char *gt_alloc_channel_buffer(size_t size) {
    return (unsigned char *)malloc(size);
}

static void gt_free_channel_buffer(unsigned char *buffer) {
    free(buffer);
}

static gt_task_handle_t *gt_alloc_handle_memory(void) {
    return (gt_task_handle_t *)calloc(1, sizeof(gt_task_handle_t));
}

static void gt_free_handle_memory(gt_task_handle_t *handle) {
    free(handle);
}

static gt_select_waiter_t *gt_alloc_select_waiter(void) {
    return (gt_select_waiter_t *)calloc(1, sizeof(gt_select_waiter_t));
}

static void gt_free_select_waiter(gt_select_waiter_t *waiter) {
    free(waiter);
}
#endif

static void gt_task_entry(void *arg);

static size_t gt_page_size(void) {
#if defined(_WIN32)
    SYSTEM_INFO info;
    GetSystemInfo(&info);
    return (size_t)info.dwPageSize;
#else
    long page = sysconf(_SC_PAGESIZE);
    return page > 0 ? (size_t)page : 4096u;
#endif
}

static size_t gt_round_up(size_t value, size_t align) {
    if (align == 0) {
        return value;
    }
    size_t rem = value % align;
    return rem == 0 ? value : value + (align - rem);
}

static int gt_stack_alloc(gt_stack_t *stack, size_t requested_size) {
    if (!stack) {
        return GT_ERR_INVALID;
    }
    memset(stack, 0, sizeof(*stack));

    if (requested_size == 0) {
        requested_size = GT_DEFAULT_STACK_SIZE;
    }
    if (requested_size < GT_MIN_STACK_SIZE) {
        return GT_ERR_INVALID;
    }

#if defined(_WIN32)
    /*
     * Windows Fibers allocate/manage their own stack inside CreateFiber().
     * The runtime records the requested usable size for debug metadata, but
     * no separate stack mapping is needed here.
     */
#ifdef GT_TESTING
    if (g_fail_next_stack_alloc) {
        g_fail_next_stack_alloc = 0;
        return GT_ERR_NOMEM;
    }
#endif
    stack->usable_size = requested_size;
#if defined(GT_DISABLE_GUARD_PAGES)
    stack->guard_size = 0;
#else
    stack->guard_size = gt_page_size();
#endif
    stack->alloc_kind = GT_STACK_ALLOC_NONE;
#ifdef GT_TESTING
    g_stack_allocs++;
#endif
    return GT_OK;
#else
    const size_t page = gt_page_size();
    const size_t usable = gt_round_up(requested_size, page);
#if defined(GT_DISABLE_GUARD_PAGES)
    const size_t total = usable;
#else
    const size_t guard = page;
    const size_t total = usable + guard;
#endif

#ifdef GT_TESTING
    if (g_fail_next_stack_alloc) {
        g_fail_next_stack_alloc = 0;
        return GT_ERR_NOMEM;
    }
#endif

#if defined(GT_DISABLE_GUARD_PAGES)
    void *mapping = malloc(total);
    if (!mapping) {
        return GT_ERR_NOMEM;
    }

    stack->mapping = mapping;
    stack->mapping_size = total;
    stack->usable = mapping;
    stack->usable_size = usable;
    stack->guard_size = 0;
    stack->alloc_kind = GT_STACK_ALLOC_MALLOC;
#ifdef GT_TESTING
    g_stack_allocs++;
#endif
    return GT_OK;
#else
    void *mapping = mmap(NULL, total, PROT_READ | PROT_WRITE,
                         MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (mapping == MAP_FAILED) {
        return GT_ERR_NOMEM;
    }

    if (mprotect(mapping, guard, PROT_NONE) != 0) {
        munmap(mapping, total);
        return GT_ERR;
    }

    stack->mapping = mapping;
    stack->mapping_size = total;
    stack->usable = (char *)mapping + guard;
    stack->usable_size = usable;
    stack->guard_size = guard;
    stack->alloc_kind = GT_STACK_ALLOC_MMAP;
#ifdef GT_TESTING
    g_stack_allocs++;
#endif
    return GT_OK;
#endif
#endif
}

static void gt_stack_free(gt_stack_t *stack) {
    if (!stack) {
        return;
    }
#if defined(_WIN32)
    if (stack->alloc_kind != GT_STACK_ALLOC_NONE || stack->usable_size != 0) {
#ifdef GT_TESTING
        g_stack_frees++;
#endif
    }
    memset(stack, 0, sizeof(*stack));
#else
    if (stack->alloc_kind == GT_STACK_ALLOC_MMAP && stack->mapping && stack->mapping_size > 0) {
        munmap(stack->mapping, stack->mapping_size);
#ifdef GT_TESTING
        g_stack_frees++;
#endif
    } else if (stack->alloc_kind == GT_STACK_ALLOC_MALLOC && stack->mapping) {
        free(stack->mapping);
#ifdef GT_TESTING
        g_stack_frees++;
#endif
    }
    memset(stack, 0, sizeof(*stack));
#endif
}

static void *gt_stack_context_base(gt_stack_t *stack) {
    return stack->usable;
}

static size_t gt_stack_context_size(gt_stack_t *stack) {
    return stack->usable_size;
}

static uint64_t gt_now_ns_raw(int *ok) {
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

static uint64_t gt_now_ns(void) {
    static uint64_t last_good_ns;

#ifdef GT_TESTING
    if (g_fail_next_clock_read) {
        g_fail_next_clock_read = 0;
        if (last_good_ns != 0) {
            return last_good_ns;
        }
        int fallback_ok = 0;
        uint64_t fallback = gt_now_ns_raw(&fallback_ok);
        if (fallback_ok) {
            last_good_ns = fallback;
            return fallback;
        }
        return 0;
    }
#endif

    int ok = 0;
    uint64_t now = gt_now_ns_raw(&ok);
    if (ok) {
        last_good_ns = now;
        return now;
    }
    return last_good_ns;
}

static void gt_sleep_os_ns(uint64_t ns) {
    if (ns == 0) {
        return;
    }
#if defined(_WIN32)
    DWORD ms = (DWORD)((ns + GT_NS_PER_MS - 1) / GT_NS_PER_MS);
    Sleep(ms);
#else
    struct timespec req;
    req.tv_sec = (time_t)(ns / UINT64_C(1000000000));
    req.tv_nsec = (long)(ns % UINT64_C(1000000000));
    while (nanosleep(&req, &req) != 0 && errno == EINTR) {
    }
#endif
}

static int gt_timer_less(const gt_task_t *a, const gt_task_t *b) {
    if (a->wake_ns != b->wake_ns) {
        return a->wake_ns < b->wake_ns;
    }
    return a->timer_seq < b->timer_seq;
}

static int gt_timer_reserve(size_t needed) {
    if (g_rt.timers.cap >= needed) {
        return GT_OK;
    }
    size_t new_cap = g_rt.timers.cap ? g_rt.timers.cap * 2u : 16u;
    while (new_cap < needed) {
        new_cap *= 2u;
    }

    size_t bytes = new_cap * sizeof(g_rt.timers.items[0]);
    void *new_items = g_rt.timers.items
        ? gt_realloc_timer_memory(g_rt.timers.items, bytes)
        : gt_alloc_timer_memory(bytes);
    if (!new_items) {
        return GT_ERR_NOMEM;
    }
    g_rt.timers.items = (gt_task_t **)new_items;
    g_rt.timers.cap = new_cap;
    return GT_OK;
}

static void gt_timer_swap(size_t a, size_t b) {
    gt_task_t *tmp = g_rt.timers.items[a];
    g_rt.timers.items[a] = g_rt.timers.items[b];
    g_rt.timers.items[b] = tmp;
}

static int gt_timer_push_state(gt_task_t *task, uint64_t deadline_ns, gt_task_state_t state) {
    if (gt_timer_reserve(g_rt.timers.len + 1u) != GT_OK) {
        return GT_ERR_NOMEM;
    }

    task->wake_ns = deadline_ns;
    task->timer_seq = g_rt.timers.next_seq++;
    task->state = state;

    size_t i = g_rt.timers.len++;
    g_rt.timers.items[i] = task;
    while (i > 0) {
        size_t parent = (i - 1u) / 2u;
        if (!gt_timer_less(g_rt.timers.items[i], g_rt.timers.items[parent])) {
            break;
        }
        gt_timer_swap(i, parent);
        i = parent;
    }
    gt_notify_all();
    return GT_OK;
}

static int gt_timer_push(gt_task_t *task, uint64_t deadline_ns) {
    return gt_timer_push_state(task, deadline_ns, GT_TASK_SLEEPING);
}

static gt_task_t *gt_timer_pop(void) {
    if (g_rt.timers.len == 0) {
        return NULL;
    }
    gt_task_t *top = g_rt.timers.items[0];
    g_rt.timers.len--;
    if (g_rt.timers.len > 0) {
        g_rt.timers.items[0] = g_rt.timers.items[g_rt.timers.len];
        size_t i = 0;
        for (;;) {
            size_t left = i * 2u + 1u;
            size_t right = left + 1u;
            size_t smallest = i;
            if (left < g_rt.timers.len && gt_timer_less(g_rt.timers.items[left], g_rt.timers.items[smallest])) {
                smallest = left;
            }
            if (right < g_rt.timers.len && gt_timer_less(g_rt.timers.items[right], g_rt.timers.items[smallest])) {
                smallest = right;
            }
            if (smallest == i) {
                break;
            }
            gt_timer_swap(i, smallest);
            i = smallest;
        }
    }
    top->wake_ns = 0;
    return top;
}

static int gt_timer_remove(gt_task_t *task) {
    for (size_t i = 0; i < g_rt.timers.len; ++i) {
        if (g_rt.timers.items[i] == task) {
            g_rt.timers.len--;
            if (i != g_rt.timers.len) {
                g_rt.timers.items[i] = g_rt.timers.items[g_rt.timers.len];
                while (i > 0) {
                    size_t parent = (i - 1u) / 2u;
                    if (!gt_timer_less(g_rt.timers.items[i], g_rt.timers.items[parent])) {
                        break;
                    }
                    gt_timer_swap(i, parent);
                    i = parent;
                }
                for (;;) {
                    size_t left = i * 2u + 1u;
                    size_t right = left + 1u;
                    size_t smallest = i;
                    if (left < g_rt.timers.len && gt_timer_less(g_rt.timers.items[left], g_rt.timers.items[smallest])) {
                        smallest = left;
                    }
                    if (right < g_rt.timers.len && gt_timer_less(g_rt.timers.items[right], g_rt.timers.items[smallest])) {
                        smallest = right;
                    }
                    if (smallest == i) {
                        break;
                    }
                    gt_timer_swap(i, smallest);
                    i = smallest;
                }
            }
            task->wake_ns = 0;
            return 1;
        }
    }
    return 0;
}

static gt_task_t *gt_timer_peek(void) {
    return g_rt.timers.len ? g_rt.timers.items[0] : NULL;
}

static void gt_wake_expired_timers(uint64_t now_ns) {
    for (;;) {
        gt_task_t *task = gt_timer_peek();
        if (!task || task->wake_ns > now_ns) {
            break;
        }
        task = gt_timer_pop();
        if (task->state == GT_TASK_WAITING_SELECT) {
            gt_select_timeout_ready(task);
        } else {
            task->state = GT_TASK_READY;
            gt_runq_push(task);
        }
    }
}

static void gt_runq_push(gt_task_t *task) {
    task->next = NULL;
    if (!g_rt.runq_tail) {
        g_rt.runq_head = task;
        g_rt.runq_tail = task;
    } else {
        g_rt.runq_tail->next = task;
        g_rt.runq_tail = task;
    }
    g_rt.runnable_count++;
    gt_notify_one();
}

static gt_task_t *gt_runq_pop(void) {
    gt_task_t *task = g_rt.runq_head;
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

static int gt_runq_remove(gt_task_t *task) {
    gt_task_t **link = &g_rt.runq_head;
    gt_task_t *prev = NULL;
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

static void gt_chan_waitq_push(gt_task_t **head, gt_task_t **tail, size_t *count, gt_task_t *task) {
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

static gt_task_t *gt_chan_waitq_pop(gt_task_t **head, gt_task_t **tail, size_t *count) {
    gt_task_t *task = *head;
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

static int gt_waitq_remove(gt_task_t **head, gt_task_t **tail, size_t *count, gt_task_t *task) {
    gt_task_t **link = head;
    gt_task_t *prev = NULL;
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

static void gt_select_waitq_push(gt_select_waiter_t **head,
                                 gt_select_waiter_t **tail,
                                 size_t *count,
                                 gt_select_waiter_t *waiter) {
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

static gt_select_waiter_t *gt_select_waitq_pop(gt_select_waiter_t **head,
                                               gt_select_waiter_t **tail,
                                               size_t *count) {
    gt_select_waiter_t *waiter = *head;
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

static int gt_select_waitq_remove(gt_select_waiter_t **head,
                                  gt_select_waiter_t **tail,
                                  size_t *count,
                                  gt_select_waiter_t *waiter) {
    gt_select_waiter_t **link = head;
    gt_select_waiter_t *prev = NULL;
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

static int gt_select_remove_waiter_from_channel(gt_select_waiter_t *waiter) {
    if (!waiter || !waiter->active || !waiter->ch) {
        return 0;
    }
    if (waiter->op == GT_SELECT_SEND) {
        return gt_select_waitq_remove(&waiter->ch->select_send_head,
                                      &waiter->ch->select_send_tail,
                                      &waiter->ch->select_send_waiters,
                                      waiter);
    }
    if (waiter->op == GT_SELECT_RECV) {
        return gt_select_waitq_remove(&waiter->ch->select_recv_head,
                                      &waiter->ch->select_recv_tail,
                                      &waiter->ch->select_recv_waiters,
                                      waiter);
    }
    return 0;
}

static void gt_select_free_task_waiters(gt_task_t *task) {
    gt_select_waiter_t *waiter = task ? task->select_waiters : NULL;
    while (waiter) {
        gt_select_waiter_t *next = waiter->task_next;
        if (waiter->active) {
            gt_select_remove_waiter_from_channel(waiter);
        }
        gt_free_select_waiter(waiter);
        waiter = next;
    }
    if (task) {
        task->select_waiters = NULL;
        task->select_waiter_count = 0;
    }
}

static void gt_select_unpark_task(gt_task_t *task, int result, size_t index) {
    if (!task) {
        return;
    }
    if (task->select_in_timer) {
        gt_timer_remove(task);
        task->select_in_timer = 0;
    }
    gt_select_free_task_waiters(task);
    if (task->select_counted_waiting) {
        if (g_rt.channel_waiting_count > 0) {
            g_rt.channel_waiting_count--;
        }
        task->select_counted_waiting = 0;
    }
    task->select_result = result;
    task->select_index = index;
    task->state = GT_TASK_READY;
    gt_runq_push(task);
}

static void gt_select_complete_waiter(gt_select_waiter_t *waiter, int result) {
    if (!waiter || !waiter->task) {
        return;
    }
    waiter->active = 0;
    gt_select_unpark_task(waiter->task, result, waiter->index);
}

static void gt_select_timeout_ready(gt_task_t *task) {
    if (!task) {
        return;
    }
    task->select_in_timer = 0;
    gt_select_free_task_waiters(task);
    if (task->select_counted_waiting) {
        if (g_rt.channel_waiting_count > 0) {
            g_rt.channel_waiting_count--;
        }
        task->select_counted_waiting = 0;
    }
    task->select_result = GT_OK;
    task->state = GT_TASK_READY;
    gt_runq_push(task);
}

static void gt_chan_ready_waiter(gt_task_t *task, int result) {
    task->chan_result = result;
    task->chan_wait_kind = GT_CHAN_WAIT_NONE;
    task->chan_wait_ch = NULL;
    task->chan_value = NULL;
    if (g_rt.channel_waiting_count > 0) {
        g_rt.channel_waiting_count--;
    }
    task->state = GT_TASK_READY;
    gt_runq_push(task);
}

static int gt_chan_remove_waiter(gt_task_t *task) {
    gt_chan_t *ch = task->chan_wait_ch;
    int removed = 0;
    if (!ch) {
        return 0;
    }
    if (task->chan_wait_kind == GT_CHAN_WAIT_SEND) {
        removed = gt_waitq_remove(&ch->send_head, &ch->send_tail, &ch->send_waiters, task);
    } else if (task->chan_wait_kind == GT_CHAN_WAIT_RECV) {
        removed = gt_waitq_remove(&ch->recv_head, &ch->recv_tail, &ch->recv_waiters, task);
    }
    if (removed && g_rt.channel_waiting_count > 0) {
        g_rt.channel_waiting_count--;
    }
    task->chan_wait_kind = GT_CHAN_WAIT_NONE;
    task->chan_wait_ch = NULL;
    task->chan_value = NULL;
    return removed;
}

static gt_task_status_t gt_status_from_task(const gt_task_t *task) {
    switch (task->state) {
        case GT_TASK_READY: return GT_TASK_STATUS_READY;
        case GT_TASK_RUNNING: return GT_TASK_STATUS_RUNNING;
        case GT_TASK_SLEEPING: return GT_TASK_STATUS_SLEEPING;
        case GT_TASK_WAITING_CHAN: return GT_TASK_STATUS_WAITING_CHAN;
        case GT_TASK_WAITING_SELECT: return GT_TASK_STATUS_WAITING_CHAN;
        case GT_TASK_WAITING_JOIN: return GT_TASK_STATUS_WAITING_JOIN;
        case GT_TASK_DEAD: return GT_TASK_STATUS_DONE;
    }
    return GT_TASK_STATUS_DONE;
}

static void gt_handle_free_if_possible(gt_task_handle_t *handle) {
    if (!handle || !handle->released || handle->task || handle->join_waiters != 0) {
        return;
    }
    gt_handle_unregister(handle);
    gt_free_handle_memory(handle);
}

static void gt_join_waiter_ready(gt_task_t *task, int result) {
    task->join_result = result;
    task->join_waiting_on = NULL;
    if (g_rt.join_waiting_count > 0) {
        g_rt.join_waiting_count--;
    }
    task->state = GT_TASK_READY;
    gt_runq_push(task);
}

static void gt_handle_complete(gt_task_handle_t *handle, int cancelled) {
    if (!handle || handle->completed) {
        return;
    }
    handle->completed = 1;
    handle->task = NULL;
    handle->status = cancelled ? GT_TASK_STATUS_CANCELLED : GT_TASK_STATUS_DONE;
    handle->join_result = cancelled ? GT_ERR_CANCELLED : GT_OK;

    gt_task_t *waiter = NULL;
    while ((waiter = gt_chan_waitq_pop(&handle->join_head, &handle->join_tail, &handle->join_waiters)) != NULL) {
        gt_join_waiter_ready(waiter, handle->join_result);
    }
    gt_handle_free_if_possible(handle);
}

static int gt_handle_remove_joiner(gt_task_handle_t *handle, gt_task_t *task) {
    if (!handle) {
        return 0;
    }
    if (gt_waitq_remove(&handle->join_head, &handle->join_tail, &handle->join_waiters, task)) {
        if (g_rt.join_waiting_count > 0) {
            g_rt.join_waiting_count--;
        }
        task->join_waiting_on = NULL;
        return 1;
    }
    return 0;
}

static void gt_chan_buffer_push(gt_chan_t *ch, const void *value) {
    memcpy(ch->buffer + (ch->tail * ch->elem_size), value, ch->elem_size);
    ch->tail = (ch->tail + 1u) % ch->capacity;
    ch->len++;
}

static void gt_chan_buffer_pop(gt_chan_t *ch, void *out) {
    memcpy(out, ch->buffer + (ch->head * ch->elem_size), ch->elem_size);
    ch->head = (ch->head + 1u) % ch->capacity;
    ch->len--;
}

static void gt_chan_fill_buffer_from_waiting_sender(gt_chan_t *ch) {
    if (!ch || ch->closed || ch->capacity == 0 || ch->len >= ch->capacity) {
        return;
    }

    gt_task_t *sender = gt_chan_waitq_pop(&ch->send_head, &ch->send_tail, &ch->send_waiters);
    if (sender) {
        gt_chan_buffer_push(ch, sender->chan_value);
        gt_chan_ready_waiter(sender, GT_OK);
        return;
    }

    gt_select_waiter_t *select_sender = gt_select_waitq_pop(&ch->select_send_head,
                                                            &ch->select_send_tail,
                                                            &ch->select_send_waiters);
    if (select_sender) {
        gt_chan_buffer_push(ch, select_sender->value);
        gt_select_complete_waiter(select_sender, GT_OK);
    }
}

static void gt_chan_wake_closed_waiters(gt_chan_t *ch) {
    gt_task_t *task = NULL;
    while ((task = gt_chan_waitq_pop(&ch->send_head, &ch->send_tail, &ch->send_waiters)) != NULL) {
        gt_chan_ready_waiter(task, GT_ERR_CLOSED);
    }
    while ((task = gt_chan_waitq_pop(&ch->recv_head, &ch->recv_tail, &ch->recv_waiters)) != NULL) {
        gt_chan_ready_waiter(task, GT_ERR_CLOSED);
    }

    gt_select_waiter_t *waiter = NULL;
    while ((waiter = gt_select_waitq_pop(&ch->select_send_head,
                                         &ch->select_send_tail,
                                         &ch->select_send_waiters)) != NULL) {
        gt_select_complete_waiter(waiter, GT_ERR_CLOSED);
    }
    while ((waiter = gt_select_waitq_pop(&ch->select_recv_head,
                                         &ch->select_recv_tail,
                                         &ch->select_recv_waiters)) != NULL) {
        gt_select_complete_waiter(waiter, GT_ERR_CLOSED);
    }
}

static void gt_task_destroy(gt_task_t *task) {
    if (!task) {
        return;
    }
    gt_select_free_task_waiters(task);
    gt_task_unregister(task);
    gt_ctx_destroy(&task->ctx);
    gt_stack_free(&task->stack);
    gt_free_task_memory(task);
}

static int gt_create_task_internal(gt_fn fn,
                                   void *arg,
                                   size_t stack_size,
                                   int want_handle,
                                   gt_task_handle_t **out_handle) {
    if (!fn) {
        return GT_ERR_INVALID;
    }

    gt_task_handle_t *handle = NULL;
    if (want_handle) {
        handle = gt_alloc_handle_memory();
        if (!handle) {
            return GT_ERR_NOMEM;
        }
        handle->status = GT_TASK_STATUS_READY;
        handle->join_result = GT_OK;
        gt_handle_register(handle);
    }

    gt_task_t *task = (gt_task_t *)gt_alloc_task_memory(sizeof(*task));
    if (!task) {
        if (handle) {
            handle->released = 1;
            gt_handle_free_if_possible(handle);
        }
        return GT_ERR_NOMEM;
    }

    int rc = gt_stack_alloc(&task->stack, stack_size);
    if (rc != GT_OK) {
        gt_free_task_memory(task);
        if (handle) {
            handle->released = 1;
            gt_handle_free_if_possible(handle);
        }
        return rc;
    }

    task->id = g_rt.next_id++;
    task->fn = fn;
    task->arg = arg;
    task->state = GT_TASK_READY;
    task->handle = handle;
    if (handle) {
        handle->task = task;
    }

    if (gt_make_context(&task->ctx,
                        gt_stack_context_base(&task->stack),
                        gt_stack_context_size(&task->stack),
                        gt_task_entry,
                        task) != 0) {
        gt_stack_free(&task->stack);
        gt_free_task_memory(task);
        if (handle) {
            handle->task = NULL;
            handle->released = 1;
            gt_handle_free_if_possible(handle);
        }
        return GT_ERR;
    }

    gt_runq_push(task);
    gt_task_register(task);
    g_rt.live_count++;
    if (out_handle) {
        *out_handle = handle;
    }
    return task->id;
}

int gt_init(void) {
    if (g_rt.initialized) {
        return GT_OK;
    }

    memset(&g_rt, 0, sizeof(g_rt));
#if GT_HAS_OS_THREADS
    if (pthread_mutex_init(&g_rt.lock, NULL) != 0) {
        return GT_ERR;
    }
    if (pthread_cond_init(&g_rt.cond, NULL) != 0) {
        pthread_mutex_destroy(&g_rt.lock);
        return GT_ERR;
    }
#endif
    if (gt_ctx_init_scheduler(&g_rt.scheduler_ctx) != 0) {
#if GT_HAS_OS_THREADS
        pthread_cond_destroy(&g_rt.cond);
        pthread_mutex_destroy(&g_rt.lock);
#endif
        return GT_ERR;
    }

    g_rt.initialized = 1;
    g_rt.next_id = 1;
    g_rt.worker_count = 1;
    g_rt.run_result = GT_OK;
    return GT_OK;
}

int gt_go(gt_fn fn, void *arg) {
    if (!g_rt.initialized && gt_init() != GT_OK) {
        return GT_ERR;
    }
    gt_lock();
    int rc = gt_create_task_internal(fn, arg, 0, 0, NULL);
    gt_unlock();
    return rc;
}

int gt_go_with_stack(gt_fn fn, void *arg, size_t stack_size) {
    if (!g_rt.initialized && gt_init() != GT_OK) {
        return GT_ERR;
    }
    gt_lock();
    int rc = gt_create_task_internal(fn, arg, stack_size, 0, NULL);
    gt_unlock();
    return rc;
}

gt_task_handle_t *gt_go_handle(gt_fn fn, void *arg) {
    if (!g_rt.initialized && gt_init() != GT_OK) {
        return NULL;
    }
    gt_task_handle_t *handle = NULL;
    gt_lock();
    if (gt_create_task_internal(fn, arg, 0, 1, &handle) < 0) {
        gt_unlock();
        return NULL;
    }
    gt_unlock();
    return handle;
}

gt_task_handle_t *gt_go_handle_with_stack(gt_fn fn, void *arg, size_t stack_size) {
    if (!g_rt.initialized && gt_init() != GT_OK) {
        return NULL;
    }
    gt_task_handle_t *handle = NULL;
    gt_lock();
    if (gt_create_task_internal(fn, arg, stack_size, 1, &handle) < 0) {
        gt_unlock();
        return NULL;
    }
    gt_unlock();
    return handle;
}

static void gt_task_after_switch(gt_task_t *task) {
    g_tls_current = NULL;
    g_rt.current = NULL;
    if (g_rt.running_tasks > 0) {
        g_rt.running_tasks--;
    }

    if (task->state == GT_TASK_READY) {
        if (task->handle && !task->handle->completed) {
            task->handle->status = task->handle->cancel_requested
                ? GT_TASK_STATUS_CANCELLED
                : GT_TASK_STATUS_READY;
        }
        gt_runq_push(task);
    } else if (task->state == GT_TASK_SLEEPING) {
        if (task->handle && !task->handle->completed) {
            task->handle->status = task->handle->cancel_requested
                ? GT_TASK_STATUS_CANCELLED
                : GT_TASK_STATUS_SLEEPING;
        }
    } else if (task->state == GT_TASK_WAITING_CHAN) {
        if (task->handle && !task->handle->completed) {
            task->handle->status = task->handle->cancel_requested
                ? GT_TASK_STATUS_CANCELLED
                : GT_TASK_STATUS_WAITING_CHAN;
        }
    } else if (task->state == GT_TASK_WAITING_SELECT) {
        if (task->handle && !task->handle->completed) {
            task->handle->status = task->handle->cancel_requested
                ? GT_TASK_STATUS_CANCELLED
                : GT_TASK_STATUS_WAITING_CHAN;
        }
    } else if (task->state == GT_TASK_WAITING_JOIN) {
        if (task->handle && !task->handle->completed) {
            task->handle->status = task->handle->cancel_requested
                ? GT_TASK_STATUS_CANCELLED
                : GT_TASK_STATUS_WAITING_JOIN;
        }
    } else if (task->state == GT_TASK_DEAD) {
        if (g_rt.live_count > 0) {
            g_rt.live_count--;
        }
        g_rt.completed_count++;
        if (task->handle) {
            gt_handle_complete(task->handle, task->handle->cancel_requested);
        }
        gt_task_destroy(task);
    } else {
        g_rt.run_result = GT_ERR_STATE;
        g_rt.stopping = 1;
        gt_notify_all();
    }
}

#if GT_HAS_OS_THREADS
static void gt_cond_timedwait_ns(uint64_t delay_ns) {
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

static void gt_worker_loop(gt_context_t *scheduler_ctx, int worker_index, int sleep_for_timers) {
    g_tls_scheduler_ctx = scheduler_ctx;
    g_tls_worker_index = worker_index;

    gt_lock();
    g_rt.active_workers++;
    for (;;) {
        if (g_rt.stopping) {
            break;
        }

        gt_wake_expired_timers(gt_now_ns());

        gt_task_t *task = gt_runq_pop();
        if (!task) {
            if (g_rt.live_count == 0) {
                g_rt.stopping = 1;
                gt_notify_all();
                break;
            }

            gt_task_t *next_timer = gt_timer_peek();
            if (!next_timer) {
                if (g_rt.running_tasks == 0 &&
                    (g_rt.channel_waiting_count > 0 || g_rt.join_waiting_count > 0)) {
                    g_rt.run_result = GT_ERR_STATE;
                    g_rt.stopping = 1;
                    gt_notify_all();
                    break;
                }
#if GT_HAS_OS_THREADS
                pthread_cond_wait(&g_rt.cond, &g_rt.lock);
#else
                break;
#endif
                continue;
            }

            uint64_t now_ns = gt_now_ns();
            if (next_timer->wake_ns > now_ns) {
                if (!sleep_for_timers) {
                    g_rt.run_result = GT_ERR_STATE;
                    g_rt.stopping = 1;
                    gt_notify_all();
                    break;
                }
#if GT_HAS_OS_THREADS
                gt_cond_timedwait_ns(next_timer->wake_ns - now_ns);
#else
                gt_unlock();
                gt_sleep_os_ns(next_timer->wake_ns - now_ns);
                gt_lock();
#endif
            }
            continue;
        }

        g_tls_current = task;
        g_rt.current = task;
        g_rt.running_tasks++;
        task->state = GT_TASK_RUNNING;
        if (task->handle && !task->handle->completed) {
            task->handle->status = task->handle->cancel_requested
                ? GT_TASK_STATUS_CANCELLED
                : GT_TASK_STATUS_RUNNING;
        }
        gt_unlock();
        gt_ctx_switch(scheduler_ctx, &task->ctx);
        /* Tasks switch back to their worker scheduler with g_rt.lock held. */
        gt_task_after_switch(task);
    }
    if (g_rt.active_workers > 0) {
        g_rt.active_workers--;
    }
    gt_unlock();
    g_tls_current = NULL;
    g_tls_scheduler_ctx = NULL;
    g_tls_worker_index = 0;
}

#if GT_HAS_OS_THREADS
static void *gt_worker_thread_main(void *arg) {
    int worker_index = (int)(intptr_t)arg;
    gt_context_t scheduler_ctx;
    if (gt_ctx_init_scheduler(&scheduler_ctx) != 0) {
        gt_lock();
        g_rt.run_result = GT_ERR;
        g_rt.stopping = 1;
        gt_notify_all();
        gt_unlock();
        return NULL;
    }
    gt_worker_loop(&scheduler_ctx, worker_index, 1);
    gt_ctx_destroy(&scheduler_ctx);
    return NULL;
}
#endif

static int gt_run_internal(size_t worker_count, int sleep_for_timers) {
    if (!g_rt.initialized && gt_init() != GT_OK) {
        return GT_ERR;
    }
    if (gt_current_task()) {
        return GT_ERR_STATE;
    }

    gt_lock();
    if (g_rt.running) {
        gt_unlock();
        return GT_ERR_STATE;
    }
    g_rt.running = 1;
    g_rt.stopping = 0;
    g_rt.run_result = GT_OK;
    g_rt.worker_count = worker_count;
    gt_unlock();

#if GT_HAS_OS_THREADS
    if (worker_count > 1 && sleep_for_timers) {
        g_rt.workers = (pthread_t *)calloc(worker_count - 1u, sizeof(*g_rt.workers));
        if (!g_rt.workers) {
            gt_lock();
            g_rt.running = 0;
            gt_unlock();
            return GT_ERR_NOMEM;
        }
        g_rt.worker_threads = worker_count - 1u;
        for (size_t i = 0; i < g_rt.worker_threads; ++i) {
            if (pthread_create(&g_rt.workers[i], NULL, gt_worker_thread_main, (void *)(intptr_t)(i + 1u)) != 0) {
                gt_lock();
                g_rt.run_result = GT_ERR;
                g_rt.stopping = 1;
                gt_notify_all();
                gt_unlock();
                for (size_t j = 0; j < i; ++j) {
                    pthread_join(g_rt.workers[j], NULL);
                }
                free(g_rt.workers);
                g_rt.workers = NULL;
                g_rt.worker_threads = 0;
                gt_lock();
                g_rt.running = 0;
                gt_unlock();
                return GT_ERR;
            }
        }
    }
#endif

    gt_worker_loop(&g_rt.scheduler_ctx, 0, sleep_for_timers);

#if GT_HAS_OS_THREADS
    for (size_t i = 0; i < g_rt.worker_threads; ++i) {
        pthread_join(g_rt.workers[i], NULL);
    }
    free(g_rt.workers);
    g_rt.workers = NULL;
    g_rt.worker_threads = 0;
#endif

    gt_lock();
    int rc = g_rt.run_result;
    g_rt.running = 0;
    g_rt.stopping = 0;
    gt_unlock();
    return rc;
}

int gt_run(void) {
    if (!g_rt.initialized && gt_init() != GT_OK) {
        return GT_ERR;
    }
    return gt_run_internal(1, 1);
}

int gt_runtime_start(size_t worker_count) {
    if (worker_count == 0) {
        return GT_ERR_INVALID;
    }
    if (!g_rt.initialized && gt_init() != GT_OK) {
        return GT_ERR;
    }
    if (gt_current_task()) {
        return GT_ERR_STATE;
    }
    gt_lock();
    int already_running = g_rt.running;
    gt_unlock();
    if (already_running) {
        return GT_ERR_STATE;
    }
    return gt_run_internal(worker_count, 1);
}

int gt_runtime_workers(void) {
    if (!g_rt.initialized) {
        return 0;
    }
    gt_lock();
    size_t worker_count = g_rt.running ? g_rt.worker_count : 0;
    gt_unlock();
    if (worker_count > (size_t)INT32_MAX) {
        return INT32_MAX;
    }
    return (int)worker_count;
}

int gt_run_workers(size_t worker_count) {
    return gt_runtime_start(worker_count);
}

#ifdef GT_TESTING
int gt_test_run_until_blocked(void) {
    return gt_run_internal(1, 0);
}
#endif

void gt_yield(void) {
    gt_task_t *task = gt_current_task();
    if (!task) {
        return;
    }

    gt_lock();
    task->state = GT_TASK_READY;
    gt_ctx_switch(&task->ctx, gt_current_scheduler_ctx());
}

void gt_sleep_ms(uint64_t ms) {
    gt_task_t *task = gt_current_task();
    if (!task) {
        return;
    }

    if (ms == 0) {
        gt_yield();
        return;
    }

    gt_lock();
    uint64_t now_ns = gt_now_ns();
    uint64_t sleep_ns = ms > (UINT64_MAX / GT_NS_PER_MS)
        ? UINT64_MAX
        : ms * GT_NS_PER_MS;
    uint64_t deadline_ns = UINT64_MAX - now_ns < sleep_ns
        ? UINT64_MAX
        : now_ns + sleep_ns;

    if (gt_timer_push(task, deadline_ns) != GT_OK) {
        /*
         * gt_sleep_ms() has no error return in the v0.2 API.  If the timer
         * heap cannot grow, preserve safety and approximate semantics by
         * blocking the owning OS thread for this sleep instead of corrupting
         * scheduler queues.
         */
        gt_unlock();
        gt_sleep_os_ns(sleep_ns);
        return;
    }

    gt_ctx_switch(&task->ctx, gt_current_scheduler_ctx());
}

int gt_join(gt_task_handle_t *handle) {
    if (!handle) {
        return GT_ERR_INVALID;
    }
    gt_lock();
    if (handle->completed) {
        int rc = handle->join_result;
        gt_unlock();
        return rc;
    }

    gt_task_t *task = gt_current_task();
    if (!task) {
        gt_unlock();
        return GT_ERR_STATE;
    }
    if (handle->task == task) {
        gt_unlock();
        return GT_ERR_STATE;
    }

    task->join_waiting_on = handle;
    task->join_result = GT_OK;
    task->state = GT_TASK_WAITING_JOIN;
    g_rt.join_waiting_count++;
    gt_chan_waitq_push(&handle->join_head, &handle->join_tail, &handle->join_waiters, task);
    gt_ctx_switch(&task->ctx, gt_current_scheduler_ctx());
    return task->join_result;
}

int gt_task_cancelled(void) {
    gt_task_t *task = gt_current_task();
    return task && task->handle && task->handle->cancel_requested;
}

static void gt_task_wake_for_cancel(gt_task_t *task) {
    if (!task) {
        return;
    }
    switch (task->state) {
        case GT_TASK_READY:
            if (gt_runq_remove(task)) {
                task->state = GT_TASK_DEAD;
                if (g_rt.live_count > 0) {
                    g_rt.live_count--;
                }
                g_rt.completed_count++;
                if (task->handle) {
                    gt_handle_complete(task->handle, 1);
                }
                gt_task_destroy(task);
            }
            break;
        case GT_TASK_SLEEPING:
            if (gt_timer_remove(task)) {
                task->state = GT_TASK_READY;
                gt_runq_push(task);
            }
            break;
        case GT_TASK_WAITING_CHAN:
            if (gt_chan_remove_waiter(task)) {
                task->chan_result = GT_ERR_CANCELLED;
                task->state = GT_TASK_READY;
                gt_runq_push(task);
            }
            break;
        case GT_TASK_WAITING_SELECT:
            gt_select_unpark_task(task, GT_ERR_CANCELLED, task->select_index);
            break;
        case GT_TASK_WAITING_JOIN:
            if (gt_handle_remove_joiner(task->join_waiting_on, task)) {
                task->join_result = GT_ERR_CANCELLED;
                task->state = GT_TASK_READY;
                gt_runq_push(task);
            }
            break;
        case GT_TASK_RUNNING:
        case GT_TASK_DEAD:
            break;
    }
}

int gt_task_cancel(gt_task_handle_t *handle) {
    if (!handle) {
        return GT_ERR_INVALID;
    }
    gt_lock();
    handle->cancel_requested = 1;
    if (handle->completed) {
        gt_unlock();
        return GT_OK;
    }
    handle->status = GT_TASK_STATUS_CANCELLED;
    gt_task_wake_for_cancel(handle->task);
    gt_unlock();
    return GT_OK;
}

int gt_task_status(gt_task_handle_t *handle, gt_task_status_t *out_status) {
    if (!handle || !out_status) {
        return GT_ERR_INVALID;
    }
    gt_lock();
    if (handle->completed) {
        *out_status = handle->status;
        gt_unlock();
        return GT_OK;
    }
    if (handle->cancel_requested) {
        *out_status = GT_TASK_STATUS_CANCELLED;
        gt_unlock();
        return GT_OK;
    }
    if (handle->task) {
        *out_status = gt_status_from_task(handle->task);
        gt_unlock();
        return GT_OK;
    }
    *out_status = handle->status;
    gt_unlock();
    return GT_OK;
}

void gt_task_handle_release(gt_task_handle_t *handle) {
    if (!handle) {
        return;
    }
    gt_lock();
    handle->released = 1;
    gt_handle_free_if_possible(handle);
    gt_unlock();
}

gt_chan_t *gt_chan_create(size_t elem_size, size_t capacity) {
    if (elem_size == 0) {
        return NULL;
    }
    if (!g_rt.initialized && gt_init() != GT_OK) {
        return NULL;
    }
    gt_lock();
    if (capacity > 0 && elem_size > (SIZE_MAX / capacity)) {
        gt_unlock();
        return NULL;
    }

    gt_chan_t *ch = gt_alloc_channel_memory();
    if (!ch) {
        gt_unlock();
        return NULL;
    }

    ch->elem_size = elem_size;
    ch->capacity = capacity;

    if (capacity > 0) {
        ch->buffer = gt_alloc_channel_buffer(elem_size * capacity);
        if (!ch->buffer) {
            gt_free_channel_memory(ch);
            gt_unlock();
            return NULL;
        }
    }

    gt_chan_register(ch);
    gt_unlock();
    return ch;
}

int gt_chan_send(gt_chan_t *ch, const void *value) {
    if (!ch || !value) {
        return GT_ERR_INVALID;
    }
    gt_lock();
    if (ch->closed) {
        gt_unlock();
        return GT_ERR_CLOSED;
    }

    gt_task_t *receiver = gt_chan_waitq_pop(&ch->recv_head, &ch->recv_tail, &ch->recv_waiters);
    if (receiver) {
        memcpy(receiver->chan_value, value, ch->elem_size);
        gt_chan_ready_waiter(receiver, GT_OK);
        gt_unlock();
        return GT_OK;
    }

    gt_select_waiter_t *select_receiver = gt_select_waitq_pop(&ch->select_recv_head,
                                                              &ch->select_recv_tail,
                                                              &ch->select_recv_waiters);
    if (select_receiver) {
        memcpy(select_receiver->value, value, ch->elem_size);
        gt_select_complete_waiter(select_receiver, GT_OK);
        gt_unlock();
        return GT_OK;
    }

    if (ch->capacity > 0 && ch->len < ch->capacity) {
        gt_chan_buffer_push(ch, value);
        gt_unlock();
        return GT_OK;
    }

    gt_task_t *task = gt_current_task();
    if (!task) {
        gt_unlock();
        return GT_ERR_STATE;
    }

    task->chan_wait_kind = GT_CHAN_WAIT_SEND;
    task->chan_wait_ch = ch;
    task->chan_value = (void *)value;
    task->chan_result = GT_OK;
    task->state = GT_TASK_WAITING_CHAN;
    g_rt.channel_waiting_count++;
    gt_chan_waitq_push(&ch->send_head, &ch->send_tail, &ch->send_waiters, task);
    gt_ctx_switch(&task->ctx, gt_current_scheduler_ctx());
    return task->chan_result;
}

int gt_chan_recv(gt_chan_t *ch, void *out) {
    if (!ch || !out) {
        return GT_ERR_INVALID;
    }
    gt_lock();

    if (ch->len > 0) {
        gt_chan_buffer_pop(ch, out);
        gt_chan_fill_buffer_from_waiting_sender(ch);
        gt_unlock();
        return GT_OK;
    }

    gt_task_t *sender = gt_chan_waitq_pop(&ch->send_head, &ch->send_tail, &ch->send_waiters);
    if (sender) {
        if (ch->closed) {
            gt_chan_ready_waiter(sender, GT_ERR_CLOSED);
            gt_unlock();
            return GT_ERR_CLOSED;
        }
        memcpy(out, sender->chan_value, ch->elem_size);
        gt_chan_ready_waiter(sender, GT_OK);
        gt_unlock();
        return GT_OK;
    }

    gt_select_waiter_t *select_sender = gt_select_waitq_pop(&ch->select_send_head,
                                                            &ch->select_send_tail,
                                                            &ch->select_send_waiters);
    if (select_sender) {
        if (ch->closed) {
            gt_select_complete_waiter(select_sender, GT_ERR_CLOSED);
            gt_unlock();
            return GT_ERR_CLOSED;
        }
        memcpy(out, select_sender->value, ch->elem_size);
        gt_select_complete_waiter(select_sender, GT_OK);
        gt_unlock();
        return GT_OK;
    }

    if (ch->closed) {
        gt_unlock();
        return GT_ERR_CLOSED;
    }

    gt_task_t *task = gt_current_task();
    if (!task) {
        gt_unlock();
        return GT_ERR_STATE;
    }

    task->chan_wait_kind = GT_CHAN_WAIT_RECV;
    task->chan_wait_ch = ch;
    task->chan_value = out;
    task->chan_result = GT_OK;
    task->state = GT_TASK_WAITING_CHAN;
    g_rt.channel_waiting_count++;
    gt_chan_waitq_push(&ch->recv_head, &ch->recv_tail, &ch->recv_waiters, task);
    gt_ctx_switch(&task->ctx, gt_current_scheduler_ctx());
    return task->chan_result;
}

int gt_chan_try_send(gt_chan_t *ch, const void *value) {
    if (!ch || !value) {
        return GT_ERR_INVALID;
    }
    gt_lock();
    if (ch->closed) {
        gt_unlock();
        return GT_ERR_CLOSED;
    }

    gt_task_t *receiver = gt_chan_waitq_pop(&ch->recv_head, &ch->recv_tail, &ch->recv_waiters);
    if (receiver) {
        memcpy(receiver->chan_value, value, ch->elem_size);
        gt_chan_ready_waiter(receiver, GT_OK);
        gt_unlock();
        return GT_OK;
    }

    gt_select_waiter_t *select_receiver = gt_select_waitq_pop(&ch->select_recv_head,
                                                              &ch->select_recv_tail,
                                                              &ch->select_recv_waiters);
    if (select_receiver) {
        memcpy(select_receiver->value, value, ch->elem_size);
        gt_select_complete_waiter(select_receiver, GT_OK);
        gt_unlock();
        return GT_OK;
    }

    if (ch->capacity > 0 && ch->len < ch->capacity) {
        gt_chan_buffer_push(ch, value);
        gt_unlock();
        return GT_OK;
    }

    gt_unlock();
    return GT_ERR_WOULD_BLOCK;
}

int gt_chan_try_recv(gt_chan_t *ch, void *out) {
    if (!ch || !out) {
        return GT_ERR_INVALID;
    }
    gt_lock();

    if (ch->len > 0) {
        gt_chan_buffer_pop(ch, out);
        gt_chan_fill_buffer_from_waiting_sender(ch);
        gt_unlock();
        return GT_OK;
    }

    gt_task_t *sender = gt_chan_waitq_pop(&ch->send_head, &ch->send_tail, &ch->send_waiters);
    if (sender) {
        if (ch->closed) {
            gt_chan_ready_waiter(sender, GT_ERR_CLOSED);
            gt_unlock();
            return GT_ERR_CLOSED;
        }
        memcpy(out, sender->chan_value, ch->elem_size);
        gt_chan_ready_waiter(sender, GT_OK);
        gt_unlock();
        return GT_OK;
    }

    gt_select_waiter_t *select_sender = gt_select_waitq_pop(&ch->select_send_head,
                                                            &ch->select_send_tail,
                                                            &ch->select_send_waiters);
    if (select_sender) {
        if (ch->closed) {
            gt_select_complete_waiter(select_sender, GT_ERR_CLOSED);
            gt_unlock();
            return GT_ERR_CLOSED;
        }
        memcpy(out, select_sender->value, ch->elem_size);
        gt_select_complete_waiter(select_sender, GT_OK);
        gt_unlock();
        return GT_OK;
    }

    int rc = ch->closed ? GT_ERR_CLOSED : GT_ERR_WOULD_BLOCK;
    gt_unlock();
    return rc;
}

static int gt_select_try_case(gt_select_case_t *scase) {
    if (!scase) {
        return GT_ERR_INVALID;
    }
    switch (scase->op) {
        case GT_SELECT_RECV:
            return gt_chan_try_recv(scase->ch, scase->value);
        case GT_SELECT_SEND:
            return gt_chan_try_send(scase->ch, scase->value);
        default:
            return GT_ERR_INVALID;
    }
}

int gt_select(gt_select_case_t *cases, size_t count, size_t *selected_index) {
    if (!cases || count == 0 || !selected_index) {
        return GT_ERR_INVALID;
    }

    size_t default_index = SIZE_MAX;
    size_t timeout_index = SIZE_MAX;
    uint64_t timeout_ms = 0;
    size_t channel_cases = 0;

    for (size_t i = 0; i < count; ++i) {
        switch (cases[i].op) {
            case GT_SELECT_RECV:
                if (!cases[i].ch || !cases[i].value) {
                    return GT_ERR_INVALID;
                }
                channel_cases++;
                break;
            case GT_SELECT_SEND:
                if (!cases[i].ch || !cases[i].value) {
                    return GT_ERR_INVALID;
                }
                channel_cases++;
                break;
            case GT_SELECT_DEFAULT:
                if (default_index != SIZE_MAX) {
                    return GT_ERR_INVALID;
                }
                default_index = i;
                break;
            case GT_SELECT_TIMEOUT:
                if (timeout_index != SIZE_MAX) {
                    return GT_ERR_INVALID;
                }
                timeout_index = i;
                timeout_ms = cases[i].timeout_ms;
                break;
            default:
                return GT_ERR_INVALID;
        }
    }

    for (size_t i = 0; i < count; ++i) {
        if (cases[i].op != GT_SELECT_RECV && cases[i].op != GT_SELECT_SEND) {
            continue;
        }
        int rc = gt_select_try_case(&cases[i]);
        if (rc != GT_ERR_WOULD_BLOCK) {
            *selected_index = i;
            return rc;
        }
    }

    if (default_index != SIZE_MAX) {
        *selected_index = default_index;
        return GT_OK;
    }

    if (timeout_index != SIZE_MAX && timeout_ms == 0) {
        *selected_index = timeout_index;
        return GT_OK;
    }

    gt_lock();
    gt_task_t *task = gt_current_task();
    if (!task) {
        gt_unlock();
        return GT_ERR_STATE;
    }
    if (channel_cases == 0 && timeout_index == SIZE_MAX) {
        gt_unlock();
        return GT_ERR_INVALID;
    }

    task->select_waiters = NULL;
    task->select_waiter_count = 0;
    task->select_result = GT_OK;
    task->select_index = timeout_index != SIZE_MAX ? timeout_index : 0;
    task->select_in_timer = 0;
    task->select_counted_waiting = 0;

    for (size_t i = 0; i < count; ++i) {
        if (cases[i].op != GT_SELECT_RECV && cases[i].op != GT_SELECT_SEND) {
            continue;
        }
        gt_select_waiter_t *waiter = gt_alloc_select_waiter();
        if (!waiter) {
            gt_select_free_task_waiters(task);
            gt_unlock();
            return GT_ERR_NOMEM;
        }
        waiter->task = task;
        waiter->ch = cases[i].ch;
        waiter->op = cases[i].op;
        waiter->value = cases[i].value;
        waiter->index = i;
        waiter->task_next = task->select_waiters;
        task->select_waiters = waiter;
        task->select_waiter_count++;

        if (waiter->op == GT_SELECT_SEND) {
            gt_select_waitq_push(&waiter->ch->select_send_head,
                                 &waiter->ch->select_send_tail,
                                 &waiter->ch->select_send_waiters,
                                 waiter);
        } else {
            gt_select_waitq_push(&waiter->ch->select_recv_head,
                                 &waiter->ch->select_recv_tail,
                                 &waiter->ch->select_recv_waiters,
                                 waiter);
        }
    }

    if (timeout_index != SIZE_MAX) {
        uint64_t now_ns = gt_now_ns();
        uint64_t timeout_ns = timeout_ms > (UINT64_MAX / GT_NS_PER_MS)
            ? UINT64_MAX
            : timeout_ms * GT_NS_PER_MS;
        uint64_t deadline_ns = UINT64_MAX - now_ns < timeout_ns
            ? UINT64_MAX
            : now_ns + timeout_ns;
        task->select_index = timeout_index;
        if (gt_timer_push_state(task, deadline_ns, GT_TASK_WAITING_SELECT) != GT_OK) {
            gt_select_free_task_waiters(task);
            gt_unlock();
            return GT_ERR_NOMEM;
        }
        task->select_in_timer = 1;
    } else {
        task->state = GT_TASK_WAITING_SELECT;
    }

    g_rt.channel_waiting_count++;
    task->select_counted_waiting = 1;
    gt_ctx_switch(&task->ctx, gt_current_scheduler_ctx());

    *selected_index = task->select_index;
    return task->select_result;
}

int gt_chan_close(gt_chan_t *ch) {
    if (!ch) {
        return GT_ERR_INVALID;
    }
    gt_lock();
    if (ch->closed) {
        gt_unlock();
        return GT_ERR_CLOSED;
    }
    ch->closed = 1;
    gt_chan_wake_closed_waiters(ch);
    gt_unlock();
    return GT_OK;
}

int gt_chan_destroy(gt_chan_t *ch) {
    if (!ch) {
        return GT_ERR_INVALID;
    }
    gt_lock();

    if (ch->send_waiters || ch->recv_waiters) {
        gt_unlock();
        return GT_ERR_STATE;
    }

    /*
     * v0.5 select waiters must not be left blocked when a channel is
     * destroyed.  Preserve the v0.3 blocking send/recv contract above while
     * making select destruction behave like a terminal close.
     */
    if (ch->select_send_waiters || ch->select_recv_waiters) {
        ch->closed = 1;
        gt_chan_wake_closed_waiters(ch);
    }

    gt_chan_unregister(ch);
    gt_free_channel_buffer(ch->buffer);
    memset(ch, 0, sizeof(*ch));
    gt_free_channel_memory(ch);
    gt_unlock();
    return GT_OK;
}

size_t gt_chan_len(const gt_chan_t *ch) {
    if (!ch) {
        return 0;
    }
    gt_lock();
    size_t len = ch->len;
    gt_unlock();
    return len;
}

size_t gt_chan_capacity(const gt_chan_t *ch) {
    if (!ch) {
        return 0;
    }
    gt_lock();
    size_t cap = ch->capacity;
    gt_unlock();
    return cap;
}

int gt_chan_is_closed(const gt_chan_t *ch) {
    if (!ch) {
        return 0;
    }
    gt_lock();
    int closed = ch->closed;
    gt_unlock();
    return closed;
}

void gt_shutdown(void) {
    if (gt_current_task()) {
        /*
         * Shutting down from inside a running green thread would require
         * freeing the currently executing stack/context.  v0.1 treats this as
         * misuse and makes the call a safe no-op instead of corrupting the
         * scheduler.  The caller can return to the scheduler and shut down
         * from the owning OS thread.
         */
        return;
    }

    if (!g_rt.initialized) {
        return;
    }

    gt_lock();
    if (g_rt.running) {
        g_rt.stopping = 1;
        g_rt.run_result = GT_ERR_CANCELLED;
        gt_notify_all();
        gt_unlock();
        return;
    }

    for (gt_chan_t *ch = g_rt.channels; ch; ch = ch->registry_next) {
        ch->closed = 1;
        ch->send_head = NULL;
        ch->send_tail = NULL;
        ch->recv_head = NULL;
        ch->recv_tail = NULL;
        ch->send_waiters = 0;
        ch->recv_waiters = 0;
        ch->select_send_head = NULL;
        ch->select_send_tail = NULL;
        ch->select_recv_head = NULL;
        ch->select_recv_tail = NULL;
        ch->select_send_waiters = 0;
        ch->select_recv_waiters = 0;
    }

    for (gt_task_handle_t *handle = g_rt.handles; handle; handle = handle->registry_next) {
        handle->cancel_requested = 1;
        handle->completed = 1;
        handle->status = GT_TASK_STATUS_CANCELLED;
        handle->join_result = GT_ERR_CANCELLED;
        handle->task = NULL;
        handle->join_head = NULL;
        handle->join_tail = NULL;
        handle->join_waiters = 0;
    }

    while (g_rt.all_tasks) {
        gt_task_t *task = g_rt.all_tasks;
        gt_select_free_task_waiters(task);
        if (g_rt.live_count > 0) {
            g_rt.live_count--;
        }
        gt_task_destroy(task);
    }

    g_rt.runq_head = NULL;
    g_rt.runq_tail = NULL;
    g_rt.runnable_count = 0;
    g_rt.timers.len = 0;
    g_rt.channel_waiting_count = 0;
    g_rt.join_waiting_count = 0;

    gt_free_timer_memory(g_rt.timers.items);
    g_rt.timers.items = NULL;
    g_rt.timers.len = 0;
    g_rt.timers.cap = 0;

    gt_ctx_destroy(&g_rt.scheduler_ctx);

    gt_task_handle_t *handle = g_rt.handles;
    while (handle) {
        gt_task_handle_t *next = handle->registry_next;
        if (handle->released) {
            gt_free_handle_memory(handle);
        }
        handle = next;
    }

#if GT_HAS_OS_THREADS
    pthread_mutex_unlock(&g_rt.lock);
    pthread_cond_destroy(&g_rt.cond);
    pthread_mutex_destroy(&g_rt.lock);
#else
    gt_unlock();
#endif
    memset(&g_rt, 0, sizeof(g_rt));
}

size_t gt_debug_runnable_count(void) {
    gt_lock();
    size_t v = g_rt.runnable_count;
    gt_unlock();
    return v;
}

size_t gt_debug_live_task_count(void) {
    gt_lock();
    size_t v = g_rt.live_count;
    gt_unlock();
    return v;
}

size_t gt_debug_completed_task_count(void) {
    gt_lock();
    size_t v = g_rt.completed_count;
    gt_unlock();
    return v;
}

size_t gt_debug_sleeping_task_count(void) {
    gt_lock();
    size_t v = g_rt.timers.len;
    gt_unlock();
    return v;
}

size_t gt_debug_channel_waiting_task_count(void) {
    gt_lock();
    size_t v = g_rt.channel_waiting_count;
    gt_unlock();
    return v;
}

size_t gt_debug_join_waiting_task_count(void) {
    gt_lock();
    size_t v = g_rt.join_waiting_count;
    gt_unlock();
    return v;
}

int gt_debug_current_task_id(void) {
    gt_task_t *task = gt_current_task();
    return task ? task->id : 0;
}

#ifdef GT_TESTING
void gt_test_fail_next_task_alloc(void) {
    g_fail_next_task_alloc = 1;
}

void gt_test_fail_next_stack_alloc(void) {
    g_fail_next_stack_alloc = 1;
}

void gt_test_fail_next_context_make(void) {
    g_fail_next_context_make = 1;
}

void gt_test_fail_next_timer_alloc(void) {
    g_fail_next_timer_alloc = 1;
}

void gt_test_fail_next_clock_read(void) {
    g_fail_next_clock_read = 1;
}

void gt_test_fail_next_channel_alloc(void) {
    g_fail_next_channel_alloc = 1;
}

void gt_test_fail_next_channel_buffer_alloc(void) {
    g_fail_next_channel_buffer_alloc = 1;
}

void gt_test_fail_next_handle_alloc(void) {
    g_fail_next_handle_alloc = 1;
}

void gt_test_fail_next_select_alloc(void) {
    g_fail_next_select_alloc = 1;
}

void gt_test_reset_faults(void) {
    g_fail_next_task_alloc = 0;
    g_fail_next_stack_alloc = 0;
    g_fail_next_context_make = 0;
    g_fail_next_timer_alloc = 0;
    g_fail_next_clock_read = 0;
    g_fail_next_channel_alloc = 0;
    g_fail_next_channel_buffer_alloc = 0;
    g_fail_next_handle_alloc = 0;
    g_fail_next_select_alloc = 0;
}

void *gt_test_current_stack_base(void) {
    gt_task_t *task = gt_current_task();
    return task ? task->stack.usable : NULL;
}

size_t gt_test_current_stack_size(void) {
    gt_task_t *task = gt_current_task();
    return task ? task->stack.usable_size : 0;
}

size_t gt_test_current_stack_guard_size(void) {
    gt_task_t *task = gt_current_task();
    return task ? task->stack.guard_size : 0;
}

void gt_test_memory_counters(size_t *task_allocs,
                             size_t *task_frees,
                             size_t *stack_allocs,
                             size_t *stack_frees,
                             size_t *timer_allocs,
                             size_t *timer_frees) {
    if (task_allocs) {
        *task_allocs = g_task_allocs;
    }
    if (task_frees) {
        *task_frees = g_task_frees;
    }
    if (stack_allocs) {
        *stack_allocs = g_stack_allocs;
    }
    if (stack_frees) {
        *stack_frees = g_stack_frees;
    }
    if (timer_allocs) {
        *timer_allocs = g_timer_allocs;
    }
    if (timer_frees) {
        *timer_frees = g_timer_frees;
    }
}

void gt_test_channel_memory_counters(size_t *channel_allocs,
                                     size_t *channel_frees,
                                     size_t *buffer_allocs,
                                     size_t *buffer_frees) {
    if (channel_allocs) {
        *channel_allocs = g_channel_allocs;
    }
    if (channel_frees) {
        *channel_frees = g_channel_frees;
    }
    if (buffer_allocs) {
        *buffer_allocs = g_channel_buffer_allocs;
    }
    if (buffer_frees) {
        *buffer_frees = g_channel_buffer_frees;
    }
}

void gt_test_handle_memory_counters(size_t *handle_allocs,
                                    size_t *handle_frees) {
    if (handle_allocs) {
        *handle_allocs = g_handle_allocs;
    }
    if (handle_frees) {
        *handle_frees = g_handle_frees;
    }
}

void gt_test_select_memory_counters(size_t *select_allocs,
                                    size_t *select_frees) {
    if (select_allocs) {
        *select_allocs = g_select_allocs;
    }
    if (select_frees) {
        *select_frees = g_select_frees;
    }
}
#endif

static void gt_task_entry(void *arg) {
    gt_task_t *task = (gt_task_t *)arg;
    task->fn(task->arg);
    gt_lock();
    task->state = GT_TASK_DEAD;
    gt_ctx_switch(&task->ctx, gt_current_scheduler_ctx());
    abort();
}
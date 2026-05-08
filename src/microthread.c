#if defined(__APPLE__) && !defined(_DARWIN_C_SOURCE)
#define _DARWIN_C_SOURCE
#endif
#if !defined(_WIN32) && !defined(_POSIX_C_SOURCE)
#define _POSIX_C_SOURCE 200809L
#endif

#include "microthread.h"

#include "context.h"

#include <errno.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#define MT_HAS_OS_THREADS 0
#else
#include <arpa/inet.h>
#include <fcntl.h>
#include <netdb.h>
#include <pthread.h>
#include <poll.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>
#if defined(__linux__) && !defined(MT_FORCE_POLL_BACKEND)
#include <sys/epoll.h>
#define MT_HAVE_EPOLL 1
#elif (defined(__APPLE__) || defined(__FreeBSD__) || defined(__OpenBSD__) || defined(__NetBSD__)) && !defined(MT_FORCE_POLL_BACKEND)
#include <sys/event.h>
#define MT_HAVE_KQUEUE 1
#endif
#define MT_HAS_OS_THREADS 1
#if !defined(MAP_ANONYMOUS) && defined(MAP_ANON)
#define MAP_ANONYMOUS MAP_ANON
#endif
#endif

#define MT_NS_PER_MS UINT64_C(1000000)

typedef enum mt_task_state {
    MT_TASK_READY = 0,
    MT_TASK_RUNNING,
    MT_TASK_SLEEPING,
    MT_TASK_WAITING_CHAN,
    MT_TASK_WAITING_SELECT,
    MT_TASK_WAITING_FD,
    MT_TASK_WAITING_JOIN,
    MT_TASK_DEAD
} mt_task_state_t;

typedef enum mt_chan_wait_kind {
    MT_CHAN_WAIT_NONE = 0,
    MT_CHAN_WAIT_SEND,
    MT_CHAN_WAIT_RECV
} mt_chan_wait_kind_t;

typedef struct mt_stack {
    void *mapping;
    size_t mapping_size;
    void *usable;
    size_t usable_size;
    size_t guard_size;
    int alloc_kind;
} mt_stack_t;

typedef struct mt_select_waiter mt_select_waiter_t;
typedef struct mt_fd_waiter mt_fd_waiter_t;

enum {
    MT_STACK_ALLOC_NONE = 0,
    MT_STACK_ALLOC_MMAP = 1,
    MT_STACK_ALLOC_MALLOC = 2
};

typedef struct mt_task {
    int id;
    mt_fn fn;
    void *arg;
    mt_stack_t stack;
    mt_context_t ctx;
    mt_task_state_t state;
    uint64_t wake_ns;
    uint64_t timer_seq;
    struct mt_task *next;
    struct mt_task *wait_next;
    struct mt_task *all_next;
    mt_task_handle_t *handle;
    mt_task_handle_t *join_waiting_on;
    mt_chan_wait_kind_t chan_wait_kind;
    mt_chan_t *chan_wait_ch;
    void *chan_value;
    int chan_result;
    int join_result;
    mt_select_waiter_t *select_waiters;
    size_t select_waiter_count;
    size_t select_index;
    int select_result;
    int select_in_timer;
    int select_counted_waiting;
    mt_fd_waiter_t *fd_waiter;
    int fd_result;
    int fd_ready_events;
    int fd_in_timer;
} mt_task_t;

typedef struct mt_fd_generation {
    int fd;
    uint64_t generation;
    struct mt_fd_generation *next;
} mt_fd_generation_t;

struct mt_select_waiter {
    mt_task_t *task;
    mt_chan_t *ch;
    mt_select_op_t op;
    void *value;
    size_t index;
    int active;
    mt_select_waiter_t *task_next;
    mt_select_waiter_t *chan_next;
};

struct mt_fd_waiter {
    mt_task_t *task;
    int fd;
    int events;
    uint64_t generation;
    int ready_events;
    int active;
    mt_fd_waiter_t *next;
};

typedef enum mt_io_backend_kind {
    MT_IO_BACKEND_NONE = 0,
    MT_IO_BACKEND_POLL,
    MT_IO_BACKEND_EPOLL,
    MT_IO_BACKEND_KQUEUE
} mt_io_backend_kind_t;

struct mt_task_handle {
    mt_task_t *task;
    mt_task_status_t status;
    int cancel_requested;
    int completed;
    int released;
    int join_result;
    mt_task_t *join_head;
    mt_task_t *join_tail;
    size_t join_waiters;
    struct mt_task_handle *registry_next;
};

struct mt_chan {
    size_t elem_size;
    size_t capacity;
    size_t len;
    size_t head;
    size_t tail;
    unsigned char *buffer;
    int closed;
    mt_task_t *send_head;
    mt_task_t *send_tail;
    mt_task_t *recv_head;
    mt_task_t *recv_tail;
    size_t send_waiters;
    size_t recv_waiters;
    mt_select_waiter_t *select_send_head;
    mt_select_waiter_t *select_send_tail;
    mt_select_waiter_t *select_recv_head;
    mt_select_waiter_t *select_recv_tail;
    size_t select_send_waiters;
    size_t select_recv_waiters;
    struct mt_chan *registry_next;
};

typedef struct mt_timer_heap {
    mt_task_t **items;
    size_t len;
    size_t cap;
    uint64_t next_seq;
} mt_timer_heap_t;

typedef struct mt_runtime {
    int initialized;
    int running;
    int stopping;
    int run_result;
    int next_id;
    size_t worker_count;
    size_t active_workers;
    size_t running_tasks;
#if MT_HAS_OS_THREADS
    pthread_mutex_t lock;
    pthread_cond_t cond;
    pthread_t *workers;
    size_t worker_threads;
#endif
    mt_context_t scheduler_ctx;
    mt_task_t *current;
    mt_task_t *all_tasks;
    mt_task_t *runq_head;
    mt_task_t *runq_tail;
    mt_chan_t *channels;
    mt_task_handle_t *handles;
    mt_timer_heap_t timers;
    size_t runnable_count;
    size_t live_count;
    size_t completed_count;
    size_t channel_waiting_count;
    size_t join_waiting_count;
    mt_fd_waiter_t *fd_waiters;
    size_t fd_waiting_count;
    mt_fd_generation_t *fd_generations;
    mt_io_backend_kind_t io_backend_kind;
    int io_backend_fd;
    int io_wake_read_fd;
    int io_wake_write_fd;
    int io_polling;
} mt_runtime_t;

static mt_runtime_t g_rt;

static void mt_io_backend_wake(void);
static int mt_io_backend_init(void);
static void mt_io_backend_shutdown(void);
static const char *mt_io_backend_name_locked(void);

#if MT_HAS_OS_THREADS
static __thread mt_task_t *g_tls_current;
static __thread mt_context_t *g_tls_scheduler_ctx;
static __thread int g_tls_worker_index;
#else
static mt_task_t *g_tls_current;
static mt_context_t *g_tls_scheduler_ctx;
static int g_tls_worker_index;
#endif

static void mt_lock(void) {
#if MT_HAS_OS_THREADS
    if (g_rt.initialized) {
        pthread_mutex_lock(&g_rt.lock);
    }
#endif
}

static void mt_unlock(void) {
#if MT_HAS_OS_THREADS
    if (g_rt.initialized) {
        pthread_mutex_unlock(&g_rt.lock);
    }
#endif
}

static void mt_notify_one(void) {
#if MT_HAS_OS_THREADS
    if (g_rt.initialized) {
        pthread_cond_signal(&g_rt.cond);
        mt_io_backend_wake();
    }
#endif
}

static void mt_notify_all(void) {
#if MT_HAS_OS_THREADS
    if (g_rt.initialized) {
        pthread_cond_broadcast(&g_rt.cond);
        mt_io_backend_wake();
    }
#endif
}

static mt_task_t *mt_current_task(void) {
    return g_tls_current;
}

static mt_context_t *mt_current_scheduler_ctx(void) {
    return g_tls_scheduler_ctx ? g_tls_scheduler_ctx : &g_rt.scheduler_ctx;
}

static void mt_runq_push(mt_task_t *task);
static void mt_select_timeout_ready(mt_task_t *task);
static void mt_fd_ready_waiter(mt_fd_waiter_t *waiter, int result, int ready_events);
static void mt_fd_timeout_ready(mt_task_t *task);
static void mt_poll_fd_waiters_once(uint64_t now_ns);
#if MT_HAS_OS_THREADS
static void mt_cond_timedwait_ns(uint64_t delay_ns);
#endif

static void mt_task_register(mt_task_t *task) {
    task->all_next = g_rt.all_tasks;
    g_rt.all_tasks = task;
}

static void mt_task_unregister(mt_task_t *task) {
    mt_task_t **link = &g_rt.all_tasks;
    while (*link) {
        if (*link == task) {
            *link = task->all_next;
            task->all_next = NULL;
            return;
        }
        link = &(*link)->all_next;
    }
}

static void mt_chan_register(mt_chan_t *ch) {
    ch->registry_next = g_rt.channels;
    g_rt.channels = ch;
}

static void mt_chan_unregister(mt_chan_t *ch) {
    mt_chan_t **link = &g_rt.channels;
    while (*link) {
        if (*link == ch) {
            *link = ch->registry_next;
            ch->registry_next = NULL;
            return;
        }
        link = &(*link)->registry_next;
    }
}

static void mt_handle_register(mt_task_handle_t *handle) {
    handle->registry_next = g_rt.handles;
    g_rt.handles = handle;
}

static void mt_handle_unregister(mt_task_handle_t *handle) {
    mt_task_handle_t **link = &g_rt.handles;
    while (*link) {
        if (*link == handle) {
            *link = handle->registry_next;
            handle->registry_next = NULL;
            return;
        }
        link = &(*link)->registry_next;
    }
}

#ifdef MT_TESTING
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
static int g_fail_next_fd_waiter_alloc;
static int g_fail_next_io_backend_init;
static int g_fail_next_io_backend_register;
static int g_fail_next_io_backend_unregister;
static size_t g_fd_waiter_allocs;
static size_t g_fd_waiter_frees;
static size_t g_io_backend_inits;
static size_t g_io_backend_shutdowns;
static size_t g_io_backend_registers;
static size_t g_io_backend_unregisters;

#define MT_TEST_COUNTER_INC(counter) \
    ((void)__atomic_add_fetch(&(counter), (size_t)1, __ATOMIC_RELAXED))
#define MT_TEST_COUNTER_LOAD(counter) \
    __atomic_load_n(&(counter), __ATOMIC_RELAXED)

static void *mt_alloc_task_memory(size_t size) {
    if (g_fail_next_task_alloc) {
        g_fail_next_task_alloc = 0;
        return NULL;
    }
    void *ptr = calloc(1, size);
    if (ptr) {
        MT_TEST_COUNTER_INC(g_task_allocs);
    }
    return ptr;
}

static void mt_free_task_memory(void *ptr) {
    if (ptr) {
        MT_TEST_COUNTER_INC(g_task_frees);
    }
    free(ptr);
}

static int mt_make_context(mt_context_t *ctx,
                           void *stack,
                           size_t stack_size,
                           void (*entry)(void *),
                           void *arg) {
    if (g_fail_next_context_make) {
        g_fail_next_context_make = 0;
        return -1;
    }
    return mt_ctx_make(ctx, stack, stack_size, entry, arg);
}

static void *mt_alloc_timer_memory(size_t size) {
    if (g_fail_next_timer_alloc) {
        g_fail_next_timer_alloc = 0;
        return NULL;
    }
    void *ptr = realloc(NULL, size);
    if (ptr) {
        MT_TEST_COUNTER_INC(g_timer_allocs);
    }
    return ptr;
}

static void *mt_realloc_timer_memory(void *ptr, size_t size) {
    if (g_fail_next_timer_alloc) {
        g_fail_next_timer_alloc = 0;
        return NULL;
    }
    void *new_ptr = realloc(ptr, size);
    if (new_ptr && !ptr) {
        MT_TEST_COUNTER_INC(g_timer_allocs);
    }
    return new_ptr;
}

static void mt_free_timer_memory(void *ptr) {
    if (ptr) {
        MT_TEST_COUNTER_INC(g_timer_frees);
    }
    free(ptr);
}

static mt_chan_t *mt_alloc_channel_memory(void) {
    if (g_fail_next_channel_alloc) {
        g_fail_next_channel_alloc = 0;
        return NULL;
    }
    mt_chan_t *ch = (mt_chan_t *)calloc(1, sizeof(*ch));
    if (ch) {
        MT_TEST_COUNTER_INC(g_channel_allocs);
    }
    return ch;
}

static void mt_free_channel_memory(mt_chan_t *ch) {
    if (ch) {
        MT_TEST_COUNTER_INC(g_channel_frees);
    }
    free(ch);
}

static unsigned char *mt_alloc_channel_buffer(size_t size) {
    if (g_fail_next_channel_buffer_alloc) {
        g_fail_next_channel_buffer_alloc = 0;
        return NULL;
    }
    unsigned char *buffer = (unsigned char *)malloc(size);
    if (buffer) {
        MT_TEST_COUNTER_INC(g_channel_buffer_allocs);
    }
    return buffer;
}

static void mt_free_channel_buffer(unsigned char *buffer) {
    if (buffer) {
        MT_TEST_COUNTER_INC(g_channel_buffer_frees);
    }
    free(buffer);
}

static mt_task_handle_t *mt_alloc_handle_memory(void) {
    if (g_fail_next_handle_alloc) {
        g_fail_next_handle_alloc = 0;
        return NULL;
    }
    mt_task_handle_t *handle = (mt_task_handle_t *)calloc(1, sizeof(*handle));
    if (handle) {
        MT_TEST_COUNTER_INC(g_handle_allocs);
    }
    return handle;
}

static void mt_free_handle_memory(mt_task_handle_t *handle) {
    if (handle) {
        MT_TEST_COUNTER_INC(g_handle_frees);
    }
    free(handle);
}

static mt_select_waiter_t *mt_alloc_select_waiter(void) {
    if (g_fail_next_select_alloc) {
        g_fail_next_select_alloc = 0;
        return NULL;
    }
    mt_select_waiter_t *waiter = (mt_select_waiter_t *)calloc(1, sizeof(*waiter));
    if (waiter) {
        MT_TEST_COUNTER_INC(g_select_allocs);
    }
    return waiter;
}

static void mt_free_select_waiter(mt_select_waiter_t *waiter) {
    if (waiter) {
        MT_TEST_COUNTER_INC(g_select_frees);
    }
    free(waiter);
}

static mt_fd_waiter_t *mt_alloc_fd_waiter(void) {
    if (g_fail_next_fd_waiter_alloc) {
        g_fail_next_fd_waiter_alloc = 0;
        return NULL;
    }
    mt_fd_waiter_t *waiter = (mt_fd_waiter_t *)calloc(1, sizeof(*waiter));
    if (waiter) {
        MT_TEST_COUNTER_INC(g_fd_waiter_allocs);
    }
    return waiter;
}

static void mt_free_fd_waiter(mt_fd_waiter_t *waiter) {
    if (waiter) {
        MT_TEST_COUNTER_INC(g_fd_waiter_frees);
    }
    free(waiter);
}

#else
static void *mt_alloc_task_memory(size_t size) {
    return calloc(1, size);
}

static void mt_free_task_memory(void *ptr) {
    free(ptr);
}

static int mt_make_context(mt_context_t *ctx,
                           void *stack,
                           size_t stack_size,
                           void (*entry)(void *),
                           void *arg) {
    return mt_ctx_make(ctx, stack, stack_size, entry, arg);
}

static void *mt_alloc_timer_memory(size_t size) {
    return realloc(NULL, size);
}

static void *mt_realloc_timer_memory(void *ptr, size_t size) {
    return realloc(ptr, size);
}

static void mt_free_timer_memory(void *ptr) {
    free(ptr);
}

static mt_chan_t *mt_alloc_channel_memory(void) {
    return (mt_chan_t *)calloc(1, sizeof(mt_chan_t));
}

static void mt_free_channel_memory(mt_chan_t *ch) {
    free(ch);
}

static unsigned char *mt_alloc_channel_buffer(size_t size) {
    return (unsigned char *)malloc(size);
}

static void mt_free_channel_buffer(unsigned char *buffer) {
    free(buffer);
}

static mt_task_handle_t *mt_alloc_handle_memory(void) {
    return (mt_task_handle_t *)calloc(1, sizeof(mt_task_handle_t));
}

static void mt_free_handle_memory(mt_task_handle_t *handle) {
    free(handle);
}

static mt_select_waiter_t *mt_alloc_select_waiter(void) {
    return (mt_select_waiter_t *)calloc(1, sizeof(mt_select_waiter_t));
}

static void mt_free_select_waiter(mt_select_waiter_t *waiter) {
    free(waiter);
}

static mt_fd_waiter_t *mt_alloc_fd_waiter(void) {
    return (mt_fd_waiter_t *)calloc(1, sizeof(mt_fd_waiter_t));
}

static void mt_free_fd_waiter(mt_fd_waiter_t *waiter) {
    free(waiter);
}
#endif

static void mt_task_entry(void *arg);

static size_t mt_page_size(void) {
#if defined(_WIN32)
    SYSTEM_INFO info;
    GetSystemInfo(&info);
    return (size_t)info.dwPageSize;
#else
    long page = sysconf(_SC_PAGESIZE);
    return page > 0 ? (size_t)page : 4096u;
#endif
}

static size_t mt_round_up(size_t value, size_t align) {
    if (align == 0) {
        return value;
    }
    size_t rem = value % align;
    return rem == 0 ? value : value + (align - rem);
}

static int mt_stack_alloc(mt_stack_t *stack, size_t requested_size) {
    if (!stack) {
        return MT_ERR_INVALID;
    }
    memset(stack, 0, sizeof(*stack));

    if (requested_size == 0) {
        requested_size = MT_DEFAULT_STACK_SIZE;
    }
    if (requested_size < MT_MIN_STACK_SIZE) {
        return MT_ERR_INVALID;
    }

#if defined(_WIN32)
    /*
     * Windows Fibers allocate/manage their own stack inside CreateFiber().
     * The runtime records the requested usable size for debug metadata, but
     * no separate stack mapping is needed here.
     */
#ifdef MT_TESTING
    if (g_fail_next_stack_alloc) {
        g_fail_next_stack_alloc = 0;
        return MT_ERR_NOMEM;
    }
#endif
    stack->usable_size = requested_size;
#if defined(MT_DISABLE_GUARD_PAGES)
    stack->guard_size = 0;
#else
    stack->guard_size = mt_page_size();
#endif
    stack->alloc_kind = MT_STACK_ALLOC_NONE;
#ifdef MT_TESTING
    MT_TEST_COUNTER_INC(g_stack_allocs);
#endif
    return MT_OK;
#else
    const size_t page = mt_page_size();
    const size_t usable = mt_round_up(requested_size, page);
#if defined(MT_DISABLE_GUARD_PAGES)
    const size_t total = usable;
#else
    const size_t guard = page;
    const size_t total = usable + guard;
#endif

#ifdef MT_TESTING
    if (g_fail_next_stack_alloc) {
        g_fail_next_stack_alloc = 0;
        return MT_ERR_NOMEM;
    }
#endif

#if defined(MT_DISABLE_GUARD_PAGES)
    void *mapping = malloc(total);
    if (!mapping) {
        return MT_ERR_NOMEM;
    }

    stack->mapping = mapping;
    stack->mapping_size = total;
    stack->usable = mapping;
    stack->usable_size = usable;
    stack->guard_size = 0;
    stack->alloc_kind = MT_STACK_ALLOC_MALLOC;
#ifdef MT_TESTING
    MT_TEST_COUNTER_INC(g_stack_allocs);
#endif
    return MT_OK;
#else
    void *mapping = mmap(NULL, total, PROT_READ | PROT_WRITE,
                         MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (mapping == MAP_FAILED) {
        return MT_ERR_NOMEM;
    }

    if (mprotect(mapping, guard, PROT_NONE) != 0) {
        munmap(mapping, total);
        return MT_ERR;
    }

    stack->mapping = mapping;
    stack->mapping_size = total;
    stack->usable = (char *)mapping + guard;
    stack->usable_size = usable;
    stack->guard_size = guard;
    stack->alloc_kind = MT_STACK_ALLOC_MMAP;
#ifdef MT_TESTING
    MT_TEST_COUNTER_INC(g_stack_allocs);
#endif
    return MT_OK;
#endif
#endif
}

static void mt_stack_free(mt_stack_t *stack) {
    if (!stack) {
        return;
    }
#if defined(_WIN32)
    if (stack->alloc_kind != MT_STACK_ALLOC_NONE || stack->usable_size != 0) {
#ifdef MT_TESTING
        MT_TEST_COUNTER_INC(g_stack_frees);
#endif
    }
    memset(stack, 0, sizeof(*stack));
#else
    if (stack->alloc_kind == MT_STACK_ALLOC_MMAP && stack->mapping && stack->mapping_size > 0) {
        munmap(stack->mapping, stack->mapping_size);
#ifdef MT_TESTING
        MT_TEST_COUNTER_INC(g_stack_frees);
#endif
    } else if (stack->alloc_kind == MT_STACK_ALLOC_MALLOC && stack->mapping) {
        free(stack->mapping);
#ifdef MT_TESTING
        MT_TEST_COUNTER_INC(g_stack_frees);
#endif
    }
    memset(stack, 0, sizeof(*stack));
#endif
}

static void *mt_stack_context_base(mt_stack_t *stack) {
    return stack->usable;
}

static size_t mt_stack_context_size(mt_stack_t *stack) {
    return stack->usable_size;
}

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

static uint64_t mt_now_ns(void) {
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

static int mt_timer_push_state(mt_task_t *task, uint64_t deadline_ns, mt_task_state_t state) {
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

static int mt_timer_remove(mt_task_t *task) {
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

static void mt_runq_push(mt_task_t *task) {
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

static void mt_select_timeout_ready(mt_task_t *task) {
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

#if !defined(_WIN32)
#define MT_IO_WAKE_SENTINEL (-1)

static short mt_fd_events_to_poll(int events) {
    short pevents = 0;
    if (events & MT_FD_READ) {
        pevents |= POLLIN;
    }
    if (events & MT_FD_WRITE) {
        pevents |= POLLOUT;
    }
    return pevents;
}

static int mt_poll_revents_to_fd_events(short revents) {
    int events = 0;
    if (revents & (POLLIN | POLLHUP | POLLERR | POLLNVAL)) {
        events |= MT_FD_READ;
    }
    if (revents & (POLLOUT | POLLHUP | POLLERR | POLLNVAL)) {
        events |= MT_FD_WRITE;
    }
    return events;
}

#if defined(MT_HAVE_EPOLL)
static int mt_fd_events_from_backend(int events) {
    int out = 0;
    if (events & (EPOLLIN | EPOLLHUP | EPOLLERR)) {
        out |= MT_FD_READ;
    }
    if (events & (EPOLLOUT | EPOLLHUP | EPOLLERR)) {
        out |= MT_FD_WRITE;
    }
    return out;
}
#endif

static uint64_t mt_fd_generation_current(int fd) {
    for (mt_fd_generation_t *g = g_rt.fd_generations; g; g = g->next) {
        if (g->fd == fd) {
            return g->generation;
        }
    }
    mt_fd_generation_t *g = (mt_fd_generation_t *)calloc(1, sizeof(*g));
    if (!g) {
        return 0;
    }
    g->fd = fd;
    g->generation = 1;
    g->next = g_rt.fd_generations;
    g_rt.fd_generations = g;
    return g->generation;
}

static void mt_fd_generation_bump(int fd) {
    for (mt_fd_generation_t *g = g_rt.fd_generations; g; g = g->next) {
        if (g->fd == fd) {
            g->generation++;
            if (g->generation == 0) {
                g->generation = 1;
            }
            return;
        }
    }
    mt_fd_generation_t *g = (mt_fd_generation_t *)calloc(1, sizeof(*g));
    if (!g) {
        return;
    }
    g->fd = fd;
    g->generation = 2;
    g->next = g_rt.fd_generations;
    g_rt.fd_generations = g;
}

static void mt_io_drain_wake_pipe(void) {
    if (g_rt.io_wake_read_fd < 0) {
        return;
    }
    char buf[128];
    for (;;) {
        ssize_t n = read(g_rt.io_wake_read_fd, buf, sizeof(buf));
        if (n > 0) {
            continue;
        }
        if (n < 0 && errno == EINTR) {
            continue;
        }
        break;
    }
}

static void mt_io_backend_wake(void) {
    if (g_rt.io_wake_write_fd < 0) {
        return;
    }
    const char b = 1;
    ssize_t n;
    do {
        n = write(g_rt.io_wake_write_fd, &b, 1);
    } while (n < 0 && errno == EINTR);
}

static int mt_io_make_wake_pipe(void) {
    int fds[2] = {-1, -1};
    if (pipe(fds) != 0) {
        return MT_ERR;
    }
    if (mt_fd_set_nonblocking(fds[0]) != MT_OK || mt_fd_set_nonblocking(fds[1]) != MT_OK) {
        close(fds[0]);
        close(fds[1]);
        return MT_ERR;
    }
    g_rt.io_wake_read_fd = fds[0];
    g_rt.io_wake_write_fd = fds[1];
    return MT_OK;
}

static int mt_io_backend_init(void) {
#ifdef MT_TESTING
    if (g_fail_next_io_backend_init) {
        g_fail_next_io_backend_init = 0;
        return MT_ERR;
    }
    MT_TEST_COUNTER_INC(g_io_backend_inits);
#endif
    g_rt.io_backend_fd = -1;
    g_rt.io_wake_read_fd = -1;
    g_rt.io_wake_write_fd = -1;
    g_rt.io_backend_kind = MT_IO_BACKEND_POLL;
    g_rt.io_polling = 0;

    if (mt_io_make_wake_pipe() != MT_OK) {
        return MT_ERR;
    }

#if defined(MT_HAVE_EPOLL)
    g_rt.io_backend_fd = epoll_create1(EPOLL_CLOEXEC);
    if (g_rt.io_backend_fd < 0) {
        return MT_ERR;
    }
    g_rt.io_backend_kind = MT_IO_BACKEND_EPOLL;
    struct epoll_event ev;
    memset(&ev, 0, sizeof(ev));
    ev.events = EPOLLIN;
    ev.data.fd = MT_IO_WAKE_SENTINEL;
    if (epoll_ctl(g_rt.io_backend_fd, EPOLL_CTL_ADD, g_rt.io_wake_read_fd, &ev) != 0) {
        return MT_ERR;
    }
#elif defined(MT_HAVE_KQUEUE)
    g_rt.io_backend_fd = kqueue();
    if (g_rt.io_backend_fd < 0) {
        return MT_ERR;
    }
    g_rt.io_backend_kind = MT_IO_BACKEND_KQUEUE;
    struct kevent ev;
    EV_SET(&ev, (uintptr_t)g_rt.io_wake_read_fd, EVFILT_READ, EV_ADD | EV_CLEAR, 0, 0, NULL);
    if (kevent(g_rt.io_backend_fd, &ev, 1, NULL, 0, NULL) != 0) {
        return MT_ERR;
    }
#endif
    return MT_OK;
}

static void mt_io_backend_shutdown(void) {
#ifdef MT_TESTING
    if (g_rt.io_backend_kind != MT_IO_BACKEND_NONE || g_rt.io_backend_fd >= 0 ||
        g_rt.io_wake_read_fd >= 0 || g_rt.io_wake_write_fd >= 0) {
        MT_TEST_COUNTER_INC(g_io_backend_shutdowns);
    }
#endif
    if (g_rt.io_backend_fd >= 0) {
        close(g_rt.io_backend_fd);
        g_rt.io_backend_fd = -1;
    }
    if (g_rt.io_wake_read_fd >= 0) {
        close(g_rt.io_wake_read_fd);
        g_rt.io_wake_read_fd = -1;
    }
    if (g_rt.io_wake_write_fd >= 0) {
        close(g_rt.io_wake_write_fd);
        g_rt.io_wake_write_fd = -1;
    }
    mt_fd_generation_t *gen = g_rt.fd_generations;
    while (gen) {
        mt_fd_generation_t *next = gen->next;
        free(gen);
        gen = next;
    }
    g_rt.fd_generations = NULL;
    g_rt.io_backend_kind = MT_IO_BACKEND_NONE;
    g_rt.io_polling = 0;
}

static const char *mt_io_backend_name_locked(void) {
    switch (g_rt.io_backend_kind) {
        case MT_IO_BACKEND_EPOLL:
            return "epoll";
        case MT_IO_BACKEND_KQUEUE:
            return "kqueue";
        case MT_IO_BACKEND_POLL:
            return "poll";
        case MT_IO_BACKEND_NONE:
        default:
            return "none";
    }
}

static int mt_io_backend_add(mt_fd_waiter_t *waiter) {
    if (!waiter) {
        return MT_ERR_INVALID;
    }
#ifdef MT_TESTING
    if (g_fail_next_io_backend_register) {
        g_fail_next_io_backend_register = 0;
        return MT_ERR;
    }
#endif
    if (g_rt.io_backend_kind == MT_IO_BACKEND_POLL) {
#ifdef MT_TESTING
        MT_TEST_COUNTER_INC(g_io_backend_registers);
#endif
        return MT_OK;
    }
#if defined(MT_HAVE_EPOLL)
    if (g_rt.io_backend_kind == MT_IO_BACKEND_EPOLL) {
        struct epoll_event ev;
        memset(&ev, 0, sizeof(ev));
        ev.events = EPOLLERR | EPOLLHUP;
        if (waiter->events & MT_FD_READ) {
            ev.events |= EPOLLIN;
        }
        if (waiter->events & MT_FD_WRITE) {
            ev.events |= EPOLLOUT;
        }
        ev.data.fd = waiter->fd;
        int rc = epoll_ctl(g_rt.io_backend_fd, EPOLL_CTL_ADD, waiter->fd, &ev) == 0
            ? MT_OK
            : (errno == EBADF ? MT_ERR_INVALID : MT_ERR);
#ifdef MT_TESTING
        if (rc == MT_OK) {
            MT_TEST_COUNTER_INC(g_io_backend_registers);
        }
#endif
        return rc;
    }
#endif
#if defined(MT_HAVE_KQUEUE)
    if (g_rt.io_backend_kind == MT_IO_BACKEND_KQUEUE) {
        struct kevent evs[2];
        int n = 0;
        if (waiter->events & MT_FD_READ) {
            EV_SET(&evs[n++], (uintptr_t)waiter->fd, EVFILT_READ, EV_ADD | EV_ENABLE | EV_CLEAR, 0, 0, NULL);
        }
        if (waiter->events & MT_FD_WRITE) {
            EV_SET(&evs[n++], (uintptr_t)waiter->fd, EVFILT_WRITE, EV_ADD | EV_ENABLE | EV_CLEAR, 0, 0, NULL);
        }
        int rc = kevent(g_rt.io_backend_fd, evs, n, NULL, 0, NULL) == 0
            ? MT_OK
            : (errno == EBADF ? MT_ERR_INVALID : MT_ERR);
#ifdef MT_TESTING
        if (rc == MT_OK) {
            MT_TEST_COUNTER_INC(g_io_backend_registers);
        }
#endif
        return rc;
    }
#endif
#ifdef MT_TESTING
    MT_TEST_COUNTER_INC(g_io_backend_registers);
#endif
    return MT_OK;
}

static void mt_io_backend_remove(mt_fd_waiter_t *waiter) {
    if (!waiter) {
        return;
    }
#ifdef MT_TESTING
    MT_TEST_COUNTER_INC(g_io_backend_unregisters);
    if (g_fail_next_io_backend_unregister) {
        g_fail_next_io_backend_unregister = 0;
        return;
    }
#endif
    if (g_rt.io_backend_kind == MT_IO_BACKEND_POLL || g_rt.io_backend_fd < 0) {
        return;
    }
#if defined(MT_HAVE_EPOLL)
    if (g_rt.io_backend_kind == MT_IO_BACKEND_EPOLL) {
        (void)epoll_ctl(g_rt.io_backend_fd, EPOLL_CTL_DEL, waiter->fd, NULL);
        return;
    }
#endif
#if defined(MT_HAVE_KQUEUE)
    if (g_rt.io_backend_kind == MT_IO_BACKEND_KQUEUE) {
        struct kevent evs[2];
        int n = 0;
        if (waiter->events & MT_FD_READ) {
            EV_SET(&evs[n++], (uintptr_t)waiter->fd, EVFILT_READ, EV_DELETE, 0, 0, NULL);
        }
        if (waiter->events & MT_FD_WRITE) {
            EV_SET(&evs[n++], (uintptr_t)waiter->fd, EVFILT_WRITE, EV_DELETE, 0, 0, NULL);
        }
        if (n > 0) {
            (void)kevent(g_rt.io_backend_fd, evs, n, NULL, 0, NULL);
        }
    }
#endif
}

static mt_fd_waiter_t *mt_fd_find_waiter(int fd, int ready_events) {
    for (mt_fd_waiter_t *w = g_rt.fd_waiters; w; w = w->next) {
        if (w->active && w->fd == fd && (w->events & ready_events) != 0) {
            if (w->generation == mt_fd_generation_current(fd)) {
                return w;
            }
            mt_fd_ready_waiter(w, MT_ERR_CLOSED, 0);
            return NULL;
        }
    }
    return NULL;
}

static int mt_fd_waiter_conflicts(int fd, int events) {
    (void)events;
    for (mt_fd_waiter_t *w = g_rt.fd_waiters; w; w = w->next) {
        if (w->active && w->fd == fd) {
            return 1;
        }
    }
    return 0;
}

static int mt_fd_waiter_add(mt_fd_waiter_t *waiter) {
    int rc = mt_io_backend_add(waiter);
    if (rc != MT_OK) {
        return rc;
    }
    waiter->active = 1;
    waiter->next = g_rt.fd_waiters;
    g_rt.fd_waiters = waiter;
    g_rt.fd_waiting_count++;
    mt_notify_all();
    return MT_OK;
}

static int mt_fd_waiter_remove(mt_fd_waiter_t *waiter) {
    if (!waiter || !waiter->active) {
        return 0;
    }
    mt_fd_waiter_t **link = &g_rt.fd_waiters;
    while (*link) {
        if (*link == waiter) {
            *link = waiter->next;
            waiter->next = NULL;
            waiter->active = 0;
            mt_io_backend_remove(waiter);
            if (g_rt.fd_waiting_count > 0) {
                g_rt.fd_waiting_count--;
            }
            mt_notify_all();
            return 1;
        }
        link = &(*link)->next;
    }
    waiter->active = 0;
    return 0;
}

static void mt_fd_free_waiter(mt_fd_waiter_t *waiter) {
    mt_free_fd_waiter(waiter);
}

static void mt_fd_ready_waiter(mt_fd_waiter_t *waiter, int result, int ready_events) {
    if (!waiter || !waiter->task) {
        return;
    }
    mt_task_t *task = waiter->task;
    mt_fd_waiter_remove(waiter);
    if (task->fd_in_timer) {
        mt_timer_remove(task);
        task->fd_in_timer = 0;
    }
    task->fd_result = result;
    task->fd_ready_events = ready_events;
    if (task->state == MT_TASK_WAITING_FD) {
        task->state = MT_TASK_READY;
        mt_runq_push(task);
    }
}

static void mt_fd_timeout_ready(mt_task_t *task) {
    if (!task) {
        return;
    }
    task->fd_in_timer = 0;
    mt_fd_waiter_t *waiter = task->fd_waiter;
    if (waiter) {
        mt_fd_waiter_remove(waiter);
    }
    task->fd_result = MT_ERR_TIMEOUT;
    task->fd_ready_events = 0;
    task->state = MT_TASK_READY;
    mt_runq_push(task);
}

static void mt_poll_fd_waiters_locked(int timeout_ms) {
    size_t count = 1;
    for (mt_fd_waiter_t *w = g_rt.fd_waiters; w; w = w->next) {
        if (w->active) {
            count++;
        }
    }
    struct pollfd *pfds = (struct pollfd *)calloc(count, sizeof(*pfds));
    if (!pfds) {
        return;
    }
    size_t i = 0;
    pfds[i].fd = g_rt.io_wake_read_fd;
    pfds[i].events = POLLIN;
    pfds[i].revents = 0;
    i++;
    for (mt_fd_waiter_t *w = g_rt.fd_waiters; w; w = w->next) {
        if (!w->active) {
            continue;
        }
        pfds[i].fd = w->fd;
        pfds[i].events = mt_fd_events_to_poll(w->events) | POLLERR | POLLHUP;
        pfds[i].revents = 0;
        i++;
    }
    count = i;

    mt_unlock();
    int nready;
    do {
        nready = poll(pfds, count, timeout_ms);
    } while (nready < 0 && errno == EINTR);
    mt_lock();

    if (nready > 0) {
        for (i = 0; i < count; ++i) {
            if (pfds[i].revents == 0) {
                continue;
            }
            if (pfds[i].fd == g_rt.io_wake_read_fd) {
                mt_io_drain_wake_pipe();
                continue;
            }
            int ready_events = mt_poll_revents_to_fd_events(pfds[i].revents);
            mt_fd_waiter_t *w = mt_fd_find_waiter(pfds[i].fd, ready_events);
            if (!w) {
                continue;
            }
            ready_events &= w->events;
            if (ready_events == 0 && (pfds[i].revents & (POLLERR | POLLHUP | POLLNVAL))) {
                ready_events = w->events;
            }
            mt_fd_ready_waiter(w, MT_OK, ready_events);
        }
    }
    free(pfds);
}

static void mt_backend_fd_waiters_locked(int timeout_ms) {
    if (g_rt.io_backend_kind == MT_IO_BACKEND_POLL) {
        mt_poll_fd_waiters_locked(timeout_ms);
        return;
    }
#if defined(MT_HAVE_EPOLL)
    if (g_rt.io_backend_kind == MT_IO_BACKEND_EPOLL) {
        struct epoll_event events[64];
        mt_unlock();
        int nready;
        do {
            nready = epoll_wait(g_rt.io_backend_fd, events, 64, timeout_ms);
        } while (nready < 0 && errno == EINTR);
        mt_lock();
        if (nready > 0) {
            for (int i = 0; i < nready; ++i) {
                if (events[i].data.fd == MT_IO_WAKE_SENTINEL) {
                    mt_io_drain_wake_pipe();
                    continue;
                }
                int ready_events = mt_fd_events_from_backend((int)events[i].events);
                mt_fd_waiter_t *w = mt_fd_find_waiter(events[i].data.fd, ready_events);
                if (!w) {
                    continue;
                }
                ready_events &= w->events;
                if (ready_events == 0 && (events[i].events & (EPOLLERR | EPOLLHUP))) {
                    ready_events = w->events;
                }
                mt_fd_ready_waiter(w, MT_OK, ready_events);
            }
        }
        return;
    }
#endif
#if defined(MT_HAVE_KQUEUE)
    if (g_rt.io_backend_kind == MT_IO_BACKEND_KQUEUE) {
        struct kevent events[64];
        struct timespec ts;
        struct timespec *tsp = NULL;
        if (timeout_ms >= 0) {
            ts.tv_sec = timeout_ms / 1000;
            ts.tv_nsec = (long)(timeout_ms % 1000) * 1000000L;
            tsp = &ts;
        }
        mt_unlock();
        int nready;
        do {
            nready = kevent(g_rt.io_backend_fd, NULL, 0, events, 64, tsp);
        } while (nready < 0 && errno == EINTR);
        mt_lock();
        if (nready > 0) {
            for (int i = 0; i < nready; ++i) {
                int fd = (int)events[i].ident;
                if (fd == g_rt.io_wake_read_fd) {
                    mt_io_drain_wake_pipe();
                    continue;
                }
                int ready_events = 0;
                if (events[i].filter == EVFILT_READ) {
                    ready_events |= MT_FD_READ;
                } else if (events[i].filter == EVFILT_WRITE) {
                    ready_events |= MT_FD_WRITE;
                }
                if (events[i].flags & (EV_EOF | EV_ERROR)) {
                    ready_events = MT_FD_READ | MT_FD_WRITE;
                }
                mt_fd_waiter_t *w = mt_fd_find_waiter(fd, ready_events);
                if (!w) {
                    continue;
                }
                ready_events &= w->events;
                if (ready_events == 0) {
                    ready_events = w->events;
                }
                mt_fd_ready_waiter(w, MT_OK, ready_events);
            }
        }
        return;
    }
#endif
}

static void mt_poll_fd_waiters_with_timeout(int timeout_ms) {
    if (g_rt.fd_waiting_count == 0 && g_rt.io_wake_read_fd < 0) {
        return;
    }
    if (g_rt.io_polling) {
#if MT_HAS_OS_THREADS
        if (timeout_ms > 0) {
            mt_cond_timedwait_ns((uint64_t)timeout_ms * MT_NS_PER_MS);
        } else {
            pthread_cond_wait(&g_rt.cond, &g_rt.lock);
        }
#endif
        return;
    }
    g_rt.io_polling = 1;
    mt_backend_fd_waiters_locked(timeout_ms);
    g_rt.io_polling = 0;
    mt_notify_all();
}

static void mt_poll_fd_waiters_once(uint64_t now_ns) {
    (void)now_ns;
    if (g_rt.fd_waiting_count > 0) {
        mt_poll_fd_waiters_with_timeout(0);
    }
}

static void mt_fd_wake_all(int result) {
    mt_fd_waiter_t *w = g_rt.fd_waiters;
    while (w) {
        mt_fd_waiter_t *next = w->next;
        if (w->active) {
            mt_fd_ready_waiter(w, result, 0);
        }
        w = next;
    }
}

static void mt_fd_wake_for_close(int fd) {
    mt_fd_generation_bump(fd);
    mt_fd_waiter_t *w = g_rt.fd_waiters;
    while (w) {
        mt_fd_waiter_t *next = w->next;
        if (w->active && w->fd == fd) {
            mt_fd_ready_waiter(w, MT_ERR_CLOSED, 0);
        }
        w = next;
    }
}
#else
static void mt_io_backend_wake(void) {
}

static int mt_io_backend_init(void) {
    return MT_OK;
}

static void mt_io_backend_shutdown(void) {
}

static const char *mt_io_backend_name_locked(void) {
    return "unsupported";
}

static void mt_fd_timeout_ready(mt_task_t *task) {
    if (task) {
        task->state = MT_TASK_READY;
        mt_runq_push(task);
    }
}

static void mt_poll_fd_waiters_once(uint64_t now_ns) {
    (void)now_ns;
}

static void mt_poll_fd_waiters_with_timeout(int timeout_ms) {
    (void)timeout_ms;
}

static void mt_fd_wake_all(int result) {
    mt_fd_waiter_t *w = g_rt.fd_waiters;
    while (w) {
        mt_fd_waiter_t *next = w->next;
        if (w->active) {
            mt_fd_ready_waiter(w, result, 0);
        }
        w = next;
    }
}

static void mt_fd_wake_for_close(int fd) {
    (void)fd;
}
#endif

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
        case MT_TASK_WAITING_SELECT: return MT_TASK_STATUS_WAITING_CHAN;
        case MT_TASK_WAITING_FD: return MT_TASK_STATUS_WAITING_CHAN;
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

    int rc = mt_stack_alloc(&task->stack, stack_size);
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

int mt_init(void) {
    if (g_rt.initialized) {
        return MT_OK;
    }

    memset(&g_rt, 0, sizeof(g_rt));
    g_rt.io_backend_kind = MT_IO_BACKEND_NONE;
    g_rt.io_backend_fd = -1;
    g_rt.io_wake_read_fd = -1;
    g_rt.io_wake_write_fd = -1;
#if MT_HAS_OS_THREADS
    if (pthread_mutex_init(&g_rt.lock, NULL) != 0) {
        return MT_ERR;
    }
    if (pthread_cond_init(&g_rt.cond, NULL) != 0) {
        pthread_mutex_destroy(&g_rt.lock);
        return MT_ERR;
    }
#endif
    if (mt_ctx_init_scheduler(&g_rt.scheduler_ctx) != 0) {
#if MT_HAS_OS_THREADS
        pthread_cond_destroy(&g_rt.cond);
        pthread_mutex_destroy(&g_rt.lock);
#endif
        return MT_ERR;
    }
#if !defined(_WIN32)
    if (mt_io_backend_init() != MT_OK) {
        mt_ctx_destroy(&g_rt.scheduler_ctx);
#if MT_HAS_OS_THREADS
        pthread_cond_destroy(&g_rt.cond);
        pthread_mutex_destroy(&g_rt.lock);
#endif
        mt_io_backend_shutdown();
        return MT_ERR;
    }
#else
    g_rt.io_backend_kind = MT_IO_BACKEND_NONE;
    g_rt.io_backend_fd = -1;
    g_rt.io_wake_read_fd = -1;
    g_rt.io_wake_write_fd = -1;
#endif

    g_rt.initialized = 1;
    g_rt.next_id = 1;
    g_rt.worker_count = 1;
    g_rt.run_result = MT_OK;
    return MT_OK;
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
                : MT_TASK_STATUS_WAITING_CHAN;
        }
    } else if (task->state == MT_TASK_WAITING_FD) {
        if (task->handle && !task->handle->completed) {
            task->handle->status = task->handle->cancel_requested
                ? MT_TASK_STATUS_CANCELLED
                : MT_TASK_STATUS_WAITING_CHAN;
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
static void mt_cond_timedwait_ns(uint64_t delay_ns) {
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

    task->join_waiting_on = handle;
    task->join_result = MT_OK;
    task->state = MT_TASK_WAITING_JOIN;
    g_rt.join_waiting_count++;
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
                task->state = MT_TASK_DEAD;
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
                task->state = MT_TASK_READY;
                mt_runq_push(task);
            }
            break;
        case MT_TASK_WAITING_CHAN:
            if (mt_chan_remove_waiter(task)) {
                task->chan_result = MT_ERR_CANCELLED;
                task->state = MT_TASK_READY;
                mt_runq_push(task);
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
                task->join_result = MT_ERR_CANCELLED;
                task->state = MT_TASK_READY;
                mt_runq_push(task);
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

    task->chan_wait_kind = MT_CHAN_WAIT_SEND;
    task->chan_wait_ch = ch;
    task->chan_value = (void *)value;
    task->chan_result = MT_OK;
    task->state = MT_TASK_WAITING_CHAN;
    g_rt.channel_waiting_count++;
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

    task->chan_wait_kind = MT_CHAN_WAIT_RECV;
    task->chan_wait_ch = ch;
    task->chan_value = out;
    task->chan_result = MT_OK;
    task->state = MT_TASK_WAITING_CHAN;
    g_rt.channel_waiting_count++;
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
        task->state = MT_TASK_WAITING_SELECT;
    }

    g_rt.channel_waiting_count++;
    task->select_counted_waiting = 1;
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

#if !defined(_WIN32)
static int mt_fd_validate_events(int events) {
    return events != 0 && (events & ~(MT_FD_READ | MT_FD_WRITE)) == 0;
}

static uint64_t mt_deadline_from_timeout(uint64_t timeout_ms) {
    uint64_t now_ns = mt_now_ns();
    uint64_t timeout_ns = timeout_ms > (UINT64_MAX / MT_NS_PER_MS)
        ? UINT64_MAX
        : timeout_ms * MT_NS_PER_MS;
    return UINT64_MAX - now_ns < timeout_ns ? UINT64_MAX : now_ns + timeout_ns;
}

static uint64_t mt_timeout_left_ms(uint64_t deadline_ns) {
    uint64_t now_ns = mt_now_ns();
    if (deadline_ns <= now_ns) {
        return 0;
    }
    uint64_t left_ns = deadline_ns - now_ns;
    return (left_ns + MT_NS_PER_MS - 1u) / MT_NS_PER_MS;
}

int mt_fd_set_nonblocking(int fd) {
    if (fd < 0) {
        return MT_ERR_INVALID;
    }
#if defined(_WIN32)
    (void)fd;
    return MT_ERR_STATE;
#else
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0) {
        return errno == EBADF ? MT_ERR_INVALID : MT_ERR;
    }
    if ((flags & O_NONBLOCK) != 0) {
        return MT_OK;
    }
    return fcntl(fd, F_SETFL, flags | O_NONBLOCK) == 0 ? MT_OK : MT_ERR;
#endif
}

const char *mt_io_backend_name(void) {
    if (!g_rt.initialized) {
        return "none";
    }
    mt_lock();
    const char *name = mt_io_backend_name_locked();
    mt_unlock();
    return name;
}

int mt_fd_wait(int fd, int events, uint64_t timeout_ms, int *ready_events) {
    if (ready_events) {
        *ready_events = 0;
    }
    if (fd < 0 || !mt_fd_validate_events(events) || !ready_events) {
        return MT_ERR_INVALID;
    }
    mt_task_t *task = mt_current_task();
    if (!task) {
        return MT_ERR_STATE;
    }

    struct pollfd pfd;
    pfd.fd = fd;
    pfd.events = mt_fd_events_to_poll(events) | POLLERR | POLLHUP;
    pfd.revents = 0;
    int nready;
    do {
        nready = poll(&pfd, 1, 0);
    } while (nready < 0 && errno == EINTR);
    if (nready < 0) {
        return errno == EBADF ? MT_ERR_INVALID : MT_ERR;
    }
    if (nready > 0) {
        *ready_events = mt_poll_revents_to_fd_events(pfd.revents) & events;
        if (*ready_events == 0 && (pfd.revents & (POLLERR | POLLHUP | POLLNVAL))) {
            *ready_events = events;
        }
        return MT_OK;
    }
    if (timeout_ms == 0) {
        return MT_ERR_TIMEOUT;
    }

    mt_lock();
    if (mt_fd_waiter_conflicts(fd, events)) {
        mt_unlock();
        return MT_ERR_STATE;
    }
    mt_fd_waiter_t *waiter = mt_alloc_fd_waiter();
    if (!waiter) {
        mt_unlock();
        return MT_ERR_NOMEM;
    }
    waiter->task = task;
    waiter->fd = fd;
    waiter->events = events;
    waiter->generation = mt_fd_generation_current(fd);
    task->fd_waiter = waiter;
    task->fd_result = MT_OK;
    task->fd_ready_events = 0;
    task->fd_in_timer = 0;
    int add_rc = mt_fd_waiter_add(waiter);
    if (add_rc != MT_OK) {
        task->fd_waiter = NULL;
        mt_fd_free_waiter(waiter);
        mt_unlock();
        return add_rc;
    }

    uint64_t deadline_ns = mt_deadline_from_timeout(timeout_ms);
    if (mt_timer_push_state(task, deadline_ns, MT_TASK_WAITING_FD) != MT_OK) {
        mt_fd_waiter_remove(waiter);
        task->fd_waiter = NULL;
        mt_fd_free_waiter(waiter);
        mt_unlock();
        return MT_ERR_NOMEM;
    }
    task->fd_in_timer = 1;
    mt_ctx_switch(&task->ctx, mt_current_scheduler_ctx());

    int rc = task->fd_result;
    if (rc == MT_OK) {
        *ready_events = task->fd_ready_events;
    }
    if (task->fd_waiter == waiter) {
        task->fd_waiter = NULL;
    }
    mt_fd_free_waiter(waiter);
    return rc;
}

int mt_fd_wait_read(int fd, uint64_t timeout_ms) {
    int ready = 0;
    return mt_fd_wait(fd, MT_FD_READ, timeout_ms, &ready);
}

int mt_fd_wait_write(int fd, uint64_t timeout_ms) {
    int ready = 0;
    return mt_fd_wait(fd, MT_FD_WRITE, timeout_ms, &ready);
}

ssize_t mt_fd_read(int fd, void *buf, size_t len, uint64_t timeout_ms) {
    if (fd < 0 || (!buf && len > 0)) {
        return MT_ERR_INVALID;
    }
    if (len == 0) {
        return 0;
    }
    uint64_t deadline_ns = mt_deadline_from_timeout(timeout_ms);
    for (;;) {
        ssize_t n = read(fd, buf, len);
        if (n >= 0) {
            return n;
        }
        if (errno == EINTR) {
            continue;
        }
        if (errno != EAGAIN && errno != EWOULDBLOCK) {
            return errno == EBADF ? MT_ERR_INVALID : MT_ERR;
        }
        uint64_t left_ms = mt_timeout_left_ms(deadline_ns);
        int rc = mt_fd_wait_read(fd, left_ms);
        if (rc != MT_OK) {
            return rc;
        }
    }
}

ssize_t mt_fd_write(int fd, const void *buf, size_t len, uint64_t timeout_ms) {
    if (fd < 0 || (!buf && len > 0)) {
        return MT_ERR_INVALID;
    }
    if (len == 0) {
        return 0;
    }
    uint64_t deadline_ns = mt_deadline_from_timeout(timeout_ms);
    const unsigned char *p = (const unsigned char *)buf;
    size_t total = 0;
    while (total < len) {
        ssize_t n = write(fd, p + total, len - total);
        if (n > 0) {
            total += (size_t)n;
            continue;
        }
        if (n == 0) {
            continue;
        }
        if (errno == EINTR) {
            continue;
        }
        if (errno != EAGAIN && errno != EWOULDBLOCK) {
            return total > 0 ? (ssize_t)total : (errno == EBADF ? MT_ERR_INVALID : MT_ERR);
        }
        uint64_t left_ms = mt_timeout_left_ms(deadline_ns);
        int rc = mt_fd_wait_write(fd, left_ms);
        if (rc != MT_OK) {
            return total > 0 ? (ssize_t)total : rc;
        }
    }
    return (ssize_t)total;
}

int mt_fd_close(int fd) {
    if (fd < 0) {
        return MT_ERR_INVALID;
    }
    mt_lock();
    mt_fd_wake_for_close(fd);
    mt_unlock();
    return close(fd) == 0 ? MT_OK : (errno == EBADF ? MT_ERR_INVALID : MT_ERR);
}

int mt_net_listen_tcp(const char *host, const char *port, int backlog) {
    if (!port || backlog < 0) {
        return MT_ERR_INVALID;
    }
    struct addrinfo hints;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;

    struct addrinfo *res = NULL;
    int gai = getaddrinfo(host, port, &hints, &res);
    if (gai != 0) {
        return MT_ERR;
    }

    int listen_fd = -1;
    for (struct addrinfo *ai = res; ai; ai = ai->ai_next) {
        int fd = socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
        if (fd < 0) {
            continue;
        }
        int yes = 1;
        (void)setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
        if (bind(fd, ai->ai_addr, ai->ai_addrlen) == 0 && listen(fd, backlog) == 0) {
            if (mt_fd_set_nonblocking(fd) == MT_OK) {
                listen_fd = fd;
                break;
            }
        }
        close(fd);
    }
    freeaddrinfo(res);
    return listen_fd >= 0 ? listen_fd : MT_ERR;
}

int mt_net_accept(int listen_fd, struct sockaddr *addr, socklen_t *addrlen,
                  uint64_t timeout_ms) {
    if (listen_fd < 0) {
        return MT_ERR_INVALID;
    }
    uint64_t deadline_ns = mt_deadline_from_timeout(timeout_ms);
    for (;;) {
        int fd = accept(listen_fd, addr, addrlen);
        if (fd >= 0) {
            if (mt_fd_set_nonblocking(fd) != MT_OK) {
                close(fd);
                return MT_ERR;
            }
            return fd;
        }
        if (errno == EINTR) {
            continue;
        }
        if (errno != EAGAIN && errno != EWOULDBLOCK) {
            return errno == EBADF ? MT_ERR_INVALID : MT_ERR;
        }
        uint64_t left_ms = mt_timeout_left_ms(deadline_ns);
        int rc = mt_fd_wait_read(listen_fd, left_ms);
        if (rc != MT_OK) {
            return rc;
        }
    }
}

ssize_t mt_net_read(int fd, void *buf, size_t len, uint64_t timeout_ms) {
    return mt_fd_read(fd, buf, len, timeout_ms);
}

ssize_t mt_net_write(int fd, const void *buf, size_t len, uint64_t timeout_ms) {
    if (fd < 0 || (!buf && len > 0)) {
        return MT_ERR_INVALID;
    }
    if (len == 0) {
        return 0;
    }
#if defined(SO_NOSIGPIPE)
    int one = 1;
    (void)setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof(one));
#endif
    uint64_t deadline_ns = mt_deadline_from_timeout(timeout_ms);
    const unsigned char *p = (const unsigned char *)buf;
    size_t total = 0;
    while (total < len) {
#if defined(MSG_NOSIGNAL)
        ssize_t n = send(fd, p + total, len - total, MSG_NOSIGNAL);
#else
        ssize_t n = send(fd, p + total, len - total, 0);
#endif
        if (n > 0) {
            total += (size_t)n;
            continue;
        }
        if (n == 0) {
            continue;
        }
        if (errno == EINTR) {
            continue;
        }
        if (errno != EAGAIN && errno != EWOULDBLOCK) {
            return total > 0 ? (ssize_t)total : (errno == EBADF ? MT_ERR_INVALID : MT_ERR);
        }
        uint64_t left_ms = mt_timeout_left_ms(deadline_ns);
        int rc = mt_fd_wait_write(fd, left_ms);
        if (rc != MT_OK) {
            return total > 0 ? (ssize_t)total : rc;
        }
    }
    return (ssize_t)total;
}

int mt_net_close(int fd) {
    return mt_fd_close(fd);
}
#else
int mt_fd_set_nonblocking(int fd) {
    return fd < 0 ? MT_ERR_INVALID : MT_ERR_STATE;
}

const char *mt_io_backend_name(void) {
    return mt_io_backend_name_locked();
}

int mt_fd_wait(int fd, int events, uint64_t timeout_ms, int *ready_events) {
    (void)events;
    (void)timeout_ms;
    if (ready_events) {
        *ready_events = 0;
    }
    return fd < 0 || !ready_events ? MT_ERR_INVALID : MT_ERR_STATE;
}

int mt_fd_wait_read(int fd, uint64_t timeout_ms) {
    int ready = 0;
    return mt_fd_wait(fd, MT_FD_READ, timeout_ms, &ready);
}

int mt_fd_wait_write(int fd, uint64_t timeout_ms) {
    int ready = 0;
    return mt_fd_wait(fd, MT_FD_WRITE, timeout_ms, &ready);
}

ssize_t mt_fd_read(int fd, void *buf, size_t len, uint64_t timeout_ms) {
    (void)buf;
    (void)len;
    (void)timeout_ms;
    return fd < 0 ? MT_ERR_INVALID : MT_ERR_STATE;
}

ssize_t mt_fd_write(int fd, const void *buf, size_t len, uint64_t timeout_ms) {
    (void)buf;
    (void)len;
    (void)timeout_ms;
    return fd < 0 ? MT_ERR_INVALID : MT_ERR_STATE;
}

int mt_fd_close(int fd) {
    return fd < 0 ? MT_ERR_INVALID : MT_ERR_STATE;
}

int mt_net_listen_tcp(const char *host, const char *port, int backlog) {
    (void)host;
    (void)backlog;
    return port ? MT_ERR_STATE : MT_ERR_INVALID;
}

int mt_net_accept(int listen_fd, struct sockaddr *addr, socklen_t *addrlen,
                  uint64_t timeout_ms) {
    (void)addr;
    (void)addrlen;
    (void)timeout_ms;
    return listen_fd < 0 ? MT_ERR_INVALID : MT_ERR_STATE;
}

ssize_t mt_net_read(int fd, void *buf, size_t len, uint64_t timeout_ms) {
    return mt_fd_read(fd, buf, len, timeout_ms);
}

ssize_t mt_net_write(int fd, const void *buf, size_t len, uint64_t timeout_ms) {
    return mt_fd_write(fd, buf, len, timeout_ms);
}

int mt_net_close(int fd) {
    return mt_fd_close(fd);
}
#endif

void mt_shutdown(void) {
    if (mt_current_task()) {
        /*
         * Shutting down from inside a running microthread would require
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

    mt_lock();
    if (g_rt.running) {
        g_rt.stopping = 1;
        g_rt.run_result = MT_ERR_CANCELLED;
        mt_fd_wake_all(MT_ERR_CANCELLED);
        mt_notify_all();
        mt_unlock();
        return;
    }

    for (mt_chan_t *ch = g_rt.channels; ch; ch = ch->registry_next) {
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

    for (mt_task_handle_t *handle = g_rt.handles; handle; handle = handle->registry_next) {
        handle->cancel_requested = 1;
        handle->completed = 1;
        handle->status = MT_TASK_STATUS_CANCELLED;
        handle->join_result = MT_ERR_CANCELLED;
        handle->task = NULL;
        handle->join_head = NULL;
        handle->join_tail = NULL;
        handle->join_waiters = 0;
    }

    while (g_rt.all_tasks) {
        mt_task_t *task = g_rt.all_tasks;
        mt_select_free_task_waiters(task);
        if (task->fd_waiter) {
            mt_fd_waiter_remove(task->fd_waiter);
            mt_fd_free_waiter(task->fd_waiter);
            task->fd_waiter = NULL;
        }
        if (g_rt.live_count > 0) {
            g_rt.live_count--;
        }
        mt_task_destroy(task);
    }

    g_rt.runq_head = NULL;
    g_rt.runq_tail = NULL;
    g_rt.runnable_count = 0;
    g_rt.timers.len = 0;
    g_rt.channel_waiting_count = 0;
    g_rt.join_waiting_count = 0;
    g_rt.fd_waiting_count = 0;

    mt_free_timer_memory(g_rt.timers.items);
    g_rt.timers.items = NULL;
    g_rt.timers.len = 0;
    g_rt.timers.cap = 0;

    mt_io_backend_shutdown();

    mt_ctx_destroy(&g_rt.scheduler_ctx);

    mt_task_handle_t *handle = g_rt.handles;
    while (handle) {
        mt_task_handle_t *next = handle->registry_next;
        if (handle->released) {
            mt_free_handle_memory(handle);
        }
        handle = next;
    }

#if MT_HAS_OS_THREADS
    pthread_mutex_unlock(&g_rt.lock);
    pthread_cond_destroy(&g_rt.cond);
    pthread_mutex_destroy(&g_rt.lock);
#else
    mt_unlock();
#endif
    memset(&g_rt, 0, sizeof(g_rt));
}

size_t mt_debug_runnable_count(void) {
    mt_lock();
    size_t v = g_rt.runnable_count;
    mt_unlock();
    return v;
}

size_t mt_debug_live_task_count(void) {
    mt_lock();
    size_t v = g_rt.live_count;
    mt_unlock();
    return v;
}

size_t mt_debug_completed_task_count(void) {
    mt_lock();
    size_t v = g_rt.completed_count;
    mt_unlock();
    return v;
}

size_t mt_debug_sleeping_task_count(void) {
    mt_lock();
    size_t v = g_rt.timers.len;
    mt_unlock();
    return v;
}

size_t mt_debug_channel_waiting_task_count(void) {
    mt_lock();
    size_t v = g_rt.channel_waiting_count;
    mt_unlock();
    return v;
}

size_t mt_debug_join_waiting_task_count(void) {
    mt_lock();
    size_t v = g_rt.join_waiting_count;
    mt_unlock();
    return v;
}

size_t mt_debug_fd_waiting_task_count(void) {
    mt_lock();
    size_t v = g_rt.fd_waiting_count;
    mt_unlock();
    return v;
}

int mt_debug_current_task_id(void) {
    mt_task_t *task = mt_current_task();
    return task ? task->id : 0;
}

#ifdef MT_TESTING
void mt_test_fail_next_task_alloc(void) {
    g_fail_next_task_alloc = 1;
}

void mt_test_fail_next_stack_alloc(void) {
    g_fail_next_stack_alloc = 1;
}

void mt_test_fail_next_context_make(void) {
    g_fail_next_context_make = 1;
}

void mt_test_fail_next_timer_alloc(void) {
    g_fail_next_timer_alloc = 1;
}

void mt_test_fail_next_clock_read(void) {
    g_fail_next_clock_read = 1;
}

void mt_test_fail_next_channel_alloc(void) {
    g_fail_next_channel_alloc = 1;
}

void mt_test_fail_next_channel_buffer_alloc(void) {
    g_fail_next_channel_buffer_alloc = 1;
}

void mt_test_fail_next_handle_alloc(void) {
    g_fail_next_handle_alloc = 1;
}

void mt_test_fail_next_select_alloc(void) {
    g_fail_next_select_alloc = 1;
}

void mt_test_fail_next_fd_waiter_alloc(void) {
    g_fail_next_fd_waiter_alloc = 1;
}

void mt_test_fail_next_io_backend_init(void) {
    g_fail_next_io_backend_init = 1;
}

void mt_test_fail_next_io_backend_register(void) {
    g_fail_next_io_backend_register = 1;
}

void mt_test_fail_next_io_backend_unregister(void) {
    g_fail_next_io_backend_unregister = 1;
}

void mt_test_reset_faults(void) {
    g_fail_next_task_alloc = 0;
    g_fail_next_stack_alloc = 0;
    g_fail_next_context_make = 0;
    g_fail_next_timer_alloc = 0;
    g_fail_next_clock_read = 0;
    g_fail_next_channel_alloc = 0;
    g_fail_next_channel_buffer_alloc = 0;
    g_fail_next_handle_alloc = 0;
    g_fail_next_select_alloc = 0;
    g_fail_next_fd_waiter_alloc = 0;
    g_fail_next_io_backend_init = 0;
    g_fail_next_io_backend_register = 0;
    g_fail_next_io_backend_unregister = 0;
}

void *mt_test_current_stack_base(void) {
    mt_task_t *task = mt_current_task();
    return task ? task->stack.usable : NULL;
}

size_t mt_test_current_stack_size(void) {
    mt_task_t *task = mt_current_task();
    return task ? task->stack.usable_size : 0;
}

size_t mt_test_current_stack_guard_size(void) {
    mt_task_t *task = mt_current_task();
    return task ? task->stack.guard_size : 0;
}

void mt_test_memory_counters(size_t *task_allocs,
                             size_t *task_frees,
                             size_t *stack_allocs,
                             size_t *stack_frees,
                             size_t *timer_allocs,
                             size_t *timer_frees) {
    if (task_allocs) {
        *task_allocs = MT_TEST_COUNTER_LOAD(g_task_allocs);
    }
    if (task_frees) {
        *task_frees = MT_TEST_COUNTER_LOAD(g_task_frees);
    }
    if (stack_allocs) {
        *stack_allocs = MT_TEST_COUNTER_LOAD(g_stack_allocs);
    }
    if (stack_frees) {
        *stack_frees = MT_TEST_COUNTER_LOAD(g_stack_frees);
    }
    if (timer_allocs) {
        *timer_allocs = MT_TEST_COUNTER_LOAD(g_timer_allocs);
    }
    if (timer_frees) {
        *timer_frees = MT_TEST_COUNTER_LOAD(g_timer_frees);
    }
}

void mt_test_channel_memory_counters(size_t *channel_allocs,
                                     size_t *channel_frees,
                                     size_t *buffer_allocs,
                                     size_t *buffer_frees) {
    if (channel_allocs) {
        *channel_allocs = MT_TEST_COUNTER_LOAD(g_channel_allocs);
    }
    if (channel_frees) {
        *channel_frees = MT_TEST_COUNTER_LOAD(g_channel_frees);
    }
    if (buffer_allocs) {
        *buffer_allocs = MT_TEST_COUNTER_LOAD(g_channel_buffer_allocs);
    }
    if (buffer_frees) {
        *buffer_frees = MT_TEST_COUNTER_LOAD(g_channel_buffer_frees);
    }
}

void mt_test_handle_memory_counters(size_t *handle_allocs,
                                    size_t *handle_frees) {
    if (handle_allocs) {
        *handle_allocs = MT_TEST_COUNTER_LOAD(g_handle_allocs);
    }
    if (handle_frees) {
        *handle_frees = MT_TEST_COUNTER_LOAD(g_handle_frees);
    }
}

void mt_test_select_memory_counters(size_t *select_allocs,
                                    size_t *select_frees) {
    if (select_allocs) {
        *select_allocs = MT_TEST_COUNTER_LOAD(g_select_allocs);
    }
    if (select_frees) {
        *select_frees = MT_TEST_COUNTER_LOAD(g_select_frees);
    }
}

void mt_test_io_memory_counters(size_t *fd_waiter_allocs,
                                size_t *fd_waiter_frees,
                                size_t *backend_inits,
                                size_t *backend_shutdowns,
                                size_t *backend_registers,
                                size_t *backend_unregisters) {
    if (fd_waiter_allocs) {
        *fd_waiter_allocs = MT_TEST_COUNTER_LOAD(g_fd_waiter_allocs);
    }
    if (fd_waiter_frees) {
        *fd_waiter_frees = MT_TEST_COUNTER_LOAD(g_fd_waiter_frees);
    }
    if (backend_inits) {
        *backend_inits = MT_TEST_COUNTER_LOAD(g_io_backend_inits);
    }
    if (backend_shutdowns) {
        *backend_shutdowns = MT_TEST_COUNTER_LOAD(g_io_backend_shutdowns);
    }
    if (backend_registers) {
        *backend_registers = MT_TEST_COUNTER_LOAD(g_io_backend_registers);
    }
    if (backend_unregisters) {
        *backend_unregisters = MT_TEST_COUNTER_LOAD(g_io_backend_unregisters);
    }
}
#endif

static void mt_task_entry(void *arg) {
    mt_task_t *task = (mt_task_t *)arg;
    task->fn(task->arg);
    mt_lock();
    task->state = MT_TASK_DEAD;
    mt_ctx_switch(&task->ctx, mt_current_scheduler_ctx());
    abort();
}
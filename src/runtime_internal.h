#ifndef MICROTHREAD_RUNTIME_INTERNAL_H
#define MICROTHREAD_RUNTIME_INTERNAL_H

#if defined(__APPLE__) && !defined(_DARWIN_C_SOURCE)
#define _DARWIN_C_SOURCE
#endif
#if !defined(_WIN32) && !defined(_POSIX_C_SOURCE)
#define _POSIX_C_SOURCE 200809L
#endif

#include "microthread.h"
#include "context.h"
#include "status_internal.h"

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

#ifdef MT_TESTING
#ifndef MT_TEST_COUNTER_INC
#define MT_TEST_COUNTER_INC(counter) \
    ((void)__atomic_add_fetch(&(counter), (size_t)1, __ATOMIC_RELAXED))
#define MT_TEST_COUNTER_LOAD(counter) \
    __atomic_load_n(&(counter), __ATOMIC_RELAXED)
#endif
#endif

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

typedef struct mt_task mt_task_t;
typedef struct mt_select_waiter mt_select_waiter_t;
typedef struct mt_fd_waiter mt_fd_waiter_t;
typedef struct mt_fd_generation mt_fd_generation_t;

enum {
    MT_STACK_ALLOC_NONE = 0,
    MT_STACK_ALLOC_MMAP = 1,
    MT_STACK_ALLOC_MALLOC = 2
};

struct mt_task {
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
};

struct mt_fd_generation {
    int fd;
    uint64_t generation;
    int closing;
    int adopted;
    int original_flags;
    struct mt_fd_generation *next;
};

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
    /*
     * A waiter remains reserved after the backend wakes it until the owning
     * task resumes and consumes the result.  This keeps duplicate waiters for
     * the same fd/event from slipping into the ready-but-not-yet-resumed
     * window on threaded backends.
     */
    int pending_ready;
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
    size_t default_stack_size;
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

extern mt_runtime_t g_rt;

#if MT_HAS_OS_THREADS
extern __thread mt_task_t *g_tls_current;
extern __thread mt_context_t *g_tls_scheduler_ctx;
extern __thread int g_tls_worker_index;
#else
extern mt_task_t *g_tls_current;
extern mt_context_t *g_tls_scheduler_ctx;
extern int g_tls_worker_index;
#endif

void mt_lock(void);
void mt_unlock(void);
void mt_notify_one(void);
void mt_notify_all(void);
mt_task_t *mt_current_task(void);
mt_context_t *mt_current_scheduler_ctx(void);
void mt_runq_push(mt_task_t *task);
int mt_timer_push_state(mt_task_t *task, uint64_t deadline_ns, mt_task_state_t state);
int mt_timer_remove(mt_task_t *task);
uint64_t mt_now_ns(void);
void mt_cond_timedwait_ns(uint64_t delay_ns);
mt_fd_waiter_t *mt_alloc_fd_waiter(void);
void mt_free_fd_waiter(mt_fd_waiter_t *waiter);

void mt_select_timeout_ready(mt_task_t *task);
void mt_fd_timeout_ready(mt_task_t *task);
void mt_fd_ready_waiter(mt_fd_waiter_t *waiter, int result, int ready_events);
void mt_poll_fd_waiters_once(uint64_t now_ns);
void mt_poll_fd_waiters_with_timeout(int timeout_ms);
void mt_fd_wake_all(int result);
void mt_fd_wake_for_close(int fd);
int mt_io_backend_init(void);
void mt_io_backend_shutdown(void);
void mt_io_backend_wake(void);
const char *mt_io_backend_name_locked(void);
int mt_io_backend_add(mt_fd_waiter_t *waiter);
void mt_io_backend_remove(mt_fd_waiter_t *waiter);

uint64_t mt_fd_generation_current(int fd);
mt_fd_generation_t *mt_fd_generation_find(int fd);
int mt_fd_is_closing(int fd);
int mt_fd_set_closing(int fd, int closing);
void mt_fd_generation_bump(int fd);
int mt_fd_generation_remove(int fd);
int mt_fd_waiter_conflicts(int fd, int events);
int mt_fd_active_events_for_fd(int fd, mt_fd_waiter_t *exclude, int extra_events);
int mt_fd_waiter_add(mt_fd_waiter_t *waiter);
int mt_fd_waiter_remove(mt_fd_waiter_t *waiter);
mt_fd_waiter_t *mt_fd_find_waiter(int fd, int ready_events);
void mt_fd_free_waiter(mt_fd_waiter_t *waiter);
void mt_io_drain_wake_pipe(void);
short mt_fd_events_to_poll(int events);
int mt_poll_revents_to_fd_events(short revents);

#ifdef MT_TESTING
extern int g_fail_next_io_backend_init;
extern int g_fail_next_io_backend_register;
extern int g_fail_next_io_backend_unregister;
extern size_t g_io_backend_inits;
extern size_t g_io_backend_shutdowns;
extern size_t g_io_backend_registers;
extern size_t g_io_backend_unregisters;
#endif

#endif /* MICROTHREAD_RUNTIME_INTERNAL_H */

/* Internal implementation shard included by microthread.c. */

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
int g_fail_next_io_backend_init;
int g_fail_next_io_backend_register;
int g_fail_next_io_backend_unregister;
static size_t g_fd_waiter_allocs;
static size_t g_fd_waiter_frees;
size_t g_io_backend_inits;
size_t g_io_backend_shutdowns;
size_t g_io_backend_registers;
size_t g_io_backend_unregisters;

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

mt_fd_waiter_t *mt_alloc_fd_waiter(void) {
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

void mt_free_fd_waiter(mt_fd_waiter_t *waiter) {
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

mt_fd_waiter_t *mt_alloc_fd_waiter(void) {
    return (mt_fd_waiter_t *)calloc(1, sizeof(mt_fd_waiter_t));
}

void mt_free_fd_waiter(mt_fd_waiter_t *waiter) {
    free(waiter);
}
#endif

static void mt_task_entry(void *arg);

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


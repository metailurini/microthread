/* Internal implementation shard included by microthread.c. */

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


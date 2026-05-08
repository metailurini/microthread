#if defined(_WIN32)

#include "context.h"

typedef struct mt_win_entry_arg {
    void (*entry)(void *);
    void *arg;
} mt_win_entry_arg_t;

static VOID CALLBACK mt_fiber_entry(void *raw) {
    mt_win_entry_arg_t *entry_arg = (mt_win_entry_arg_t *)raw;
    entry_arg->entry(entry_arg->arg);
    /* The runtime entry function should never return. */
    ExitThread(1);
}

int mt_ctx_init_scheduler(mt_context_t *ctx) {
    if (!ctx) {
        return -1;
    }

    if (IsThreadAFiber()) {
        ctx->fiber = GetCurrentFiber();
        ctx->data = NULL;
        ctx->owns_fiber = 0;
        return 0;
    }

    ctx->fiber = ConvertThreadToFiber(NULL);
    ctx->data = NULL;
    /*
     * This fiber represents the caller's OS thread.  It must be converted
     * back with ConvertFiberToThread(), not destroyed with DeleteFiber().
     */
    ctx->owns_fiber = ctx->fiber ? 2 : 0;
    return ctx->fiber ? 0 : -1;
}

int mt_ctx_make(mt_context_t *ctx,
                void *stack,
                size_t stack_size,
                void (*entry)(void *),
                void *arg) {
    if (!ctx || !entry) {
        return -1;
    }

    (void)stack;

    mt_win_entry_arg_t *entry_arg =
        (mt_win_entry_arg_t *)HeapAlloc(GetProcessHeap(), 0, sizeof(*entry_arg));
    if (!entry_arg) {
        return -1;
    }

    entry_arg->entry = entry;
    entry_arg->arg = arg;

    ctx->fiber = CreateFiber(stack_size, mt_fiber_entry, entry_arg);
    ctx->data = entry_arg;
    ctx->owns_fiber = 1;
    if (!ctx->fiber) {
        HeapFree(GetProcessHeap(), 0, entry_arg);
        ctx->data = NULL;
        return -1;
    }
    return 0;
}

void mt_ctx_switch(mt_context_t *from, mt_context_t *to) {
    (void)from;
    SwitchToFiber(to->fiber);
}

void mt_ctx_destroy(mt_context_t *ctx) {
    if (!ctx || !ctx->fiber) {
        return;
    }

    if (ctx->owns_fiber == 2) {
        ConvertFiberToThread();
        ctx->fiber = NULL;
        ctx->data = NULL;
        ctx->owns_fiber = 0;
        return;
    }

    if (ctx->owns_fiber == 1) {
        DeleteFiber(ctx->fiber);
        if (ctx->data) {
            HeapFree(GetProcessHeap(), 0, ctx->data);
        }
        ctx->fiber = NULL;
        ctx->data = NULL;
        ctx->owns_fiber = 0;
    }
}

#endif /* _WIN32 */
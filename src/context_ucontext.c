#if !defined(_WIN32) && \
    !(defined(__APPLE__) && (defined(__x86_64__) || defined(__aarch64__)))

#include "context.h"

#include <stdint.h>
#include <stdlib.h>

static void mt_ctx_entry(uintptr_t entry_raw, uintptr_t arg_raw) {
    void (*entry)(void *) = (void (*)(void *))entry_raw;
    void *arg = (void *)arg_raw;
    entry(arg);
    abort();
}

int mt_ctx_init_scheduler(mt_context_t *ctx) {
    if (!ctx) {
        return -1;
    }
    return getcontext(&ctx->uc) == 0 ? 0 : -1;
}

int mt_ctx_make(mt_context_t *ctx,
                void *stack,
                size_t stack_size,
                void (*entry)(void *),
                void *arg) {
    if (!ctx || !stack || stack_size == 0 || !entry) {
        return -1;
    }

    if (getcontext(&ctx->uc) != 0) {
        return -1;
    }

    ctx->uc.uc_stack.ss_sp = stack;
    ctx->uc.uc_stack.ss_size = stack_size;
    ctx->uc.uc_stack.ss_flags = 0;
    ctx->uc.uc_link = NULL;

    makecontext(&ctx->uc,
                (void (*)(void))mt_ctx_entry,
                2,
                (uintptr_t)entry,
                (uintptr_t)arg);
    return 0;
}

void mt_ctx_switch(mt_context_t *from, mt_context_t *to) {
    swapcontext(&from->uc, &to->uc);
}

void mt_ctx_destroy(mt_context_t *ctx) {
    (void)ctx;
}

#endif /* !_WIN32 */
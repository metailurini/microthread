#if !defined(_WIN32)

#include "context.h"

#include <stdint.h>
#include <stdlib.h>

static void gt_ctx_entry(uintptr_t entry_raw, uintptr_t arg_raw) {
    void (*entry)(void *) = (void (*)(void *))entry_raw;
    void *arg = (void *)arg_raw;
    entry(arg);
    abort();
}

int gt_ctx_init_scheduler(gt_context_t *ctx) {
    if (!ctx) {
        return -1;
    }
    return getcontext(&ctx->uc) == 0 ? 0 : -1;
}

int gt_ctx_make(gt_context_t *ctx,
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
                (void (*)(void))gt_ctx_entry,
                2,
                (uintptr_t)entry,
                (uintptr_t)arg);
    return 0;
}

void gt_ctx_switch(gt_context_t *from, gt_context_t *to) {
    swapcontext(&from->uc, &to->uc);
}

void gt_ctx_destroy(gt_context_t *ctx) {
    (void)ctx;
}

#endif /* !_WIN32 */
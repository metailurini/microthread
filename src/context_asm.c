#if defined(__APPLE__) && (defined(__x86_64__) || defined(__aarch64__))

#include "context.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

void gt_ctx_switch_asm(gt_context_t *from, gt_context_t *to);
void gt_ctx_start(void);

void gt_ctx_entry(uintptr_t entry_raw, uintptr_t arg_raw) {
    void (*entry)(void *) = (void (*)(void *))entry_raw;
    void *arg = (void *)arg_raw;
    entry(arg);
    abort();
}

static uintptr_t gt_align_down(uintptr_t value, uintptr_t alignment) {
    return value & ~(alignment - 1U);
}

int gt_ctx_init_scheduler(gt_context_t *ctx) {
    if (!ctx) {
        return -1;
    }
    memset(ctx, 0, sizeof(*ctx));
    return 0;
}

int gt_ctx_make(gt_context_t *ctx,
                void *stack,
                size_t stack_size,
                void (*entry)(void *),
                void *arg) {
    if (!ctx || !stack || stack_size < 64 || !entry) {
        return -1;
    }

    memset(ctx, 0, sizeof(*ctx));

    uintptr_t top = (uintptr_t)stack + stack_size;
    top = gt_align_down(top, 16U);

#if defined(__x86_64__)
    uintptr_t *sp = (uintptr_t *)(top - 16U);
    sp[0] = (uintptr_t)gt_ctx_start;
    ctx->rsp = sp;
    ctx->r12 = (void *)entry;
    ctx->r13 = arg;
#else
    ctx->sp = (void *)top;
    ctx->x19 = (void *)entry;
    ctx->x20 = arg;
    ctx->lr = (void *)gt_ctx_start;
#endif

    return 0;
}

void gt_ctx_switch(gt_context_t *from, gt_context_t *to) {
    gt_ctx_switch_asm(from, to);
}

void gt_ctx_destroy(gt_context_t *ctx) {
    (void)ctx;
}

#endif /* __APPLE__ asm backend */

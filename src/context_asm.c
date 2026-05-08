#if defined(__APPLE__) && (defined(__x86_64__) || defined(__aarch64__))

#include "context.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

void mt_ctx_switch_asm(mt_context_t *from, mt_context_t *to);
void mt_ctx_start(void);

void mt_ctx_entry(uintptr_t entry_raw, uintptr_t arg_raw) {
    void (*entry)(void *) = (void (*)(void *))entry_raw;
    void *arg = (void *)arg_raw;
    entry(arg);
    abort();
}

static uintptr_t mt_align_down(uintptr_t value, uintptr_t alignment) {
    return value & ~(alignment - 1U);
}

int mt_ctx_init_scheduler(mt_context_t *ctx) {
    if (!ctx) {
        return -1;
    }
    memset(ctx, 0, sizeof(*ctx));
    return 0;
}

int mt_ctx_make(mt_context_t *ctx,
                void *stack,
                size_t stack_size,
                void (*entry)(void *),
                void *arg) {
    if (!ctx || !stack || stack_size < 64 || !entry) {
        return -1;
    }

    memset(ctx, 0, sizeof(*ctx));

    uintptr_t top = (uintptr_t)stack + stack_size;
    top = mt_align_down(top, 16U);

#if defined(__x86_64__)
    uintptr_t *sp = (uintptr_t *)(top - 16U);
    sp[0] = (uintptr_t)mt_ctx_start;
    ctx->rsp = sp;
    ctx->r12 = (void *)entry;
    ctx->r13 = arg;
#else
    ctx->sp = (void *)top;
    ctx->x19 = (void *)entry;
    ctx->x20 = arg;
    ctx->lr = (void *)mt_ctx_start;
#endif

    return 0;
}

void mt_ctx_switch(mt_context_t *from, mt_context_t *to) {
    mt_ctx_switch_asm(from, to);
}

void mt_ctx_destroy(mt_context_t *ctx) {
    (void)ctx;
}

#endif /* __APPLE__ asm backend */

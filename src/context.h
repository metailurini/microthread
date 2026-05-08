#ifndef GT_CONTEXT_H
#define GT_CONTEXT_H

#if !defined(_WIN32)
#if !defined(_XOPEN_SOURCE)
#define _XOPEN_SOURCE 700
#endif
#if defined(__APPLE__) && !defined(_DARWIN_C_SOURCE)
#define _DARWIN_C_SOURCE
#endif
#endif

#include <stddef.h>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
typedef struct gt_context {
    void *fiber;
    void *data;
    int owns_fiber;
} gt_context_t;
#else
#include <ucontext.h>
typedef struct gt_context {
    ucontext_t uc;
} gt_context_t;
#endif

int  gt_ctx_init_scheduler(gt_context_t *ctx);
int  gt_ctx_make(gt_context_t *ctx,
                 void *stack,
                 size_t stack_size,
                 void (*entry)(void *),
                 void *arg);
void gt_ctx_switch(gt_context_t *from, gt_context_t *to);
void gt_ctx_destroy(gt_context_t *ctx);

#endif /* GT_CONTEXT_H */
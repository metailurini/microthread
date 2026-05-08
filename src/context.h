#ifndef GT_CONTEXT_H
#define GT_CONTEXT_H

#include <stddef.h>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
typedef struct gt_context {
    void *fiber;
    void *data;
    int owns_fiber;
} gt_context_t;
#elif defined(__APPLE__) && (defined(__x86_64__) || defined(__aarch64__))
typedef struct gt_context {
#if defined(__x86_64__)
    void *rsp;
    void *r15;
    void *r14;
    void *r13;
    void *r12;
    void *rbx;
    void *rbp;
#else
    void *sp;
    void *x19;
    void *x20;
    void *x21;
    void *x22;
    void *x23;
    void *x24;
    void *x25;
    void *x26;
    void *x27;
    void *x28;
    void *fp;
    void *lr;
#endif
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
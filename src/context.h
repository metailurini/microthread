#ifndef MT_CONTEXT_H
#define MT_CONTEXT_H

#include <stddef.h>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
typedef struct mt_context {
    void *fiber;
    void *data;
    int owns_fiber;
} mt_context_t;
#elif defined(__APPLE__) && (defined(__x86_64__) || defined(__aarch64__))
typedef struct mt_context {
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
} mt_context_t;
#else
#include <ucontext.h>
typedef struct mt_context {
    ucontext_t uc;
} mt_context_t;
#endif

int  mt_ctx_init_scheduler(mt_context_t *ctx);
int  mt_ctx_make(mt_context_t *ctx,
                 void *stack,
                 size_t stack_size,
                 void (*entry)(void *),
                 void *arg);
void mt_ctx_switch(mt_context_t *from, mt_context_t *to);
void mt_ctx_destroy(mt_context_t *ctx);

#endif /* MT_CONTEXT_H */
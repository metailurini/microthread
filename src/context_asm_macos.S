#if defined(__APPLE__) && defined(__x86_64__)

.text
.globl _mt_ctx_switch_asm
.p2align 4, 0x90
_mt_ctx_switch_asm:
    movq %rsp, 0(%rdi)
    movq %r15, 8(%rdi)
    movq %r14, 16(%rdi)
    movq %r13, 24(%rdi)
    movq %r12, 32(%rdi)
    movq %rbx, 40(%rdi)
    movq %rbp, 48(%rdi)

    movq 0(%rsi), %rsp
    movq 8(%rsi), %r15
    movq 16(%rsi), %r14
    movq 24(%rsi), %r13
    movq 32(%rsi), %r12
    movq 40(%rsi), %rbx
    movq 48(%rsi), %rbp
    ret

.globl _mt_ctx_start
.p2align 4, 0x90
_mt_ctx_start:
    subq $8, %rsp
    movq %r12, %rdi
    movq %r13, %rsi
    callq _mt_ctx_entry
    ud2

#elif defined(__APPLE__) && defined(__aarch64__)

.text
.globl _mt_ctx_switch_asm
.p2align 2
_mt_ctx_switch_asm:
    mov x9, sp
    str x9, [x0, #0]
    stp x19, x20, [x0, #8]
    stp x21, x22, [x0, #24]
    stp x23, x24, [x0, #40]
    stp x25, x26, [x0, #56]
    stp x27, x28, [x0, #72]
    stp x29, x30, [x0, #88]

    ldr x9, [x1, #0]
    ldp x19, x20, [x1, #8]
    ldp x21, x22, [x1, #24]
    ldp x23, x24, [x1, #40]
    ldp x25, x26, [x1, #56]
    ldp x27, x28, [x1, #72]
    ldp x29, x30, [x1, #88]
    mov sp, x9
    ret

.globl _mt_ctx_start
.p2align 2
_mt_ctx_start:
    mov x0, x19
    mov x1, x20
    bl _mt_ctx_entry
    brk #0

#endif

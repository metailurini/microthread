# Green Threads Runtime Suggestion

This project implements a small goroutine-like runtime in C. The core runtime should stay focused on user-level scheduling, not networking.

## Recommended Core Scope

```text
v0.1: stackful green threads, scheduler, yield
v0.2: custom/guarded stacks, sleep, timer heap
v0.3: buffered/unbuffered channels
v0.4: task handles, join, cooperative cancellation
v0.5: nonblocking channel operations and channel select
v0.6: multi-worker scheduler and thread-safe core
```

## Core Components

A minimal goroutine-like runtime needs:

```text
task object
per-task stack
saved execution context
scheduler context
run queue
yield/resume mechanism
timer heap or sleep queue
channel wait queues
optional task handles/join/cancellation
```

It does **not** need sockets or networking in the core.

## Context Switching

Pure ISO C cannot portably switch stacks. Keep context switching behind a replaceable backend:

```c
typedef struct gt_context gt_context_t;

int  gt_ctx_init_scheduler(gt_context_t *ctx);
int  gt_ctx_make(gt_context_t *ctx,
                 void *stack,
                 size_t stack_size,
                 void (*entry)(void *),
                 void *arg);
void gt_ctx_switch(gt_context_t *from, gt_context_t *to);
void gt_ctx_destroy(gt_context_t *ctx);
```

Suggested first backends:

```text
Unix prototype:    ucontext
Windows prototype: Fibers
Future:            assembly or proven context library
```

## Scheduler Model

The scheduler repeatedly:

```text
wake expired timers
pop a READY task
switch scheduler -> task
task yields/sleeps/blocks/returns
switch task -> scheduler
requeue, park, or destroy task
```

## Stack Strategy

Use fixed-size stacks with guard pages where possible:

```text
Unix:    mmap + mprotect
Windows: Fiber-created stacks
Fallback: malloc without guard pages
```

Avoid Go-style growable stacks in C unless the compiler/runtime cooperate. C stack pointers can escape, so moving stacks is unsafe.

## Channels

Channels are a good core feature because they provide goroutine-style coordination without depending on OS networking:

```c
gt_chan_t *ch = gt_chan_create(sizeof(int), 16);
gt_chan_send(ch, &value);
gt_chan_recv(ch, &out);
```

Buffered channels store values. Unbuffered channels rendezvous sender and receiver.

## Cancellation and Join

After channels, the next useful core features are task handles and cooperative cancellation:

```c
gt_task_handle_t *t = gt_go_handle(fn, arg);
gt_join(t);
gt_task_cancel(t);
```

Cancellation should be cooperative. Do not asynchronously kill a task or free a running task stack.

## Networking

Networking can be useful for a separate optional async-I/O module later, but it is not part of the core goroutine implementation. If it is ever added, it should live behind a separate header/source module so users who want a local-only scheduler do not compile or expose network APIs.

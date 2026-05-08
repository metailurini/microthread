#include "microthread.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if !defined(_WIN32)
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

#if defined(__GNUC__)
#define NOINLINE __attribute__((noinline))
#else
#define NOINLINE
#endif

static volatile unsigned long g_sink;

typedef void (*recurse_fn_t)(unsigned depth);
static recurse_fn_t g_recurse_fn;

static NOINLINE void consume_stack_forever(unsigned depth) {
    volatile char buffer[4096];
    memset((void *)buffer, (int)(depth & 0xffu), sizeof(buffer));
    g_sink += (unsigned long)buffer[depth % sizeof(buffer)];
    g_recurse_fn(depth + 1u);
}

static void overflow_task(void *arg) {
    (void)arg;
    g_recurse_fn = consume_stack_forever;
    consume_stack_forever(1u);
}

#if !defined(_WIN32)
static int run_child(void) {
    assert(mt_init() == MT_OK);
    assert(mt_go_with_stack(overflow_task, NULL, MT_MIN_STACK_SIZE) > 0);
    (void)mt_run();
    mt_shutdown();
    return 0;
}
#endif

int main(int argc, char **argv) {
#if defined(_WIN32)
    (void)argc;
    (void)argv;
    printf("guard overflow subprocess test skipped on Windows in this make target\n");
    return 0;
#else
    if (argc == 2 && strcmp(argv[1], "--child") == 0) {
        return run_child();
    }

    pid_t pid = fork();
    assert(pid >= 0);
    if (pid == 0) {
        execl(argv[0], argv[0], "--child", (char *)NULL);
        _exit(127);
    }

    int status = 0;
    assert(waitpid(pid, &status, 0) == pid);
    assert(WIFSIGNALED(status));
    assert(WTERMSIG(status) == SIGSEGV || WTERMSIG(status) == SIGBUS);
    printf("guard overflow expected-crash test passed\n");
    return 0;
#endif
}

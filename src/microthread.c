#include "runtime_internal.h"

/*
 * The runtime is built as one translation unit so implementation helpers can
 * remain file-local while the source is split into focused, navigable shards.
 */
static void mt_task_destroy(mt_task_t *task);

#define MT_RUNTIME_SHARD_BUILD 1
#include "runtime.c"
#include "testing_hooks.c"
#include "stack.c"
#include "timer.c"
#include "run_queue.c"
#include "task_state.c"
#include "wait_queue.c"
#include "select_wait.c"
#include "join.c"
#include "scheduler.c"
#include "task.c"
#include "channel.c"
#include "runtime_lifecycle.c"

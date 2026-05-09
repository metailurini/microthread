#include "runtime_internal.h"

/*
 * The runtime is built as one translation unit so implementation helpers can
 * remain file-local while the source is split into focused, navigable shards.
 */
static void mt_task_destroy(mt_task_t *task);

#include "runtime.c"
#include "testing_hooks.c"
#include "stack.c"
#include "timer.c"
#include "scheduler.c"
#include "task.c"
#include "channel.c"
#include "runtime_lifecycle.c"

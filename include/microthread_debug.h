#ifndef MICROTHREAD_DEBUG_H
#define MICROTHREAD_DEBUG_H

#include "microthread.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Diagnostic counters for tests, examples, and local debugging. These values
 * are snapshots of runtime state, not synchronization primitives. The debug
 * API is intentionally separate from <microthread.h>'s default public surface.
 */
size_t mt_debug_runnable_count(void);
size_t mt_debug_live_task_count(void);
size_t mt_debug_completed_task_count(void);
size_t mt_debug_sleeping_task_count(void);
size_t mt_debug_channel_waiting_task_count(void);
size_t mt_debug_join_waiting_task_count(void);
size_t mt_debug_fd_waiting_task_count(void);
int    mt_debug_current_task_id(void);

#ifdef __cplusplus
}
#endif

#endif /* MICROTHREAD_DEBUG_H */

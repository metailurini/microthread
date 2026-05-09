#ifndef MICROTHREAD_FD_WAIT_INTERNAL_H
#define MICROTHREAD_FD_WAIT_INTERNAL_H

#include <stdint.h>

typedef struct mt_task mt_task_t;
typedef struct mt_fd_waiter mt_fd_waiter_t;

typedef enum mt_fd_waiter_state {
    MT_FD_WAITER_REMOVED = 0,
    /* Registered with the OS backend and counted by fd_waiting_count. */
    MT_FD_WAITER_ACTIVE,
    /*
     * Backend-ready, but still conflict-reserved until the owner resumes and
     * consumes the wait result. This closes the duplicate-waiter race between
     * backend wakeup and task resumption on threaded schedulers.
     */
    MT_FD_WAITER_READY_RESERVED
} mt_fd_waiter_state_t;

struct mt_fd_waiter {
    mt_task_t *task;
    int fd;
    int events;
    uint64_t generation;
    int ready_events;
    mt_fd_waiter_state_t state;
    mt_fd_waiter_t *next;
};

#endif /* MICROTHREAD_FD_WAIT_INTERNAL_H */

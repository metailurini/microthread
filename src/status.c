#include "microthread.h"

#include "status_internal.h"

static _Thread_local int mt_tls_last_os_error;

void mt_set_last_os_error(int err) {
    mt_tls_last_os_error = err;
}

const char *mt_strerror(int rc) {
    switch (rc) {
        case MT_OK: return "ok";
        case MT_ERR: return "microthread error";
        case MT_ERR_INVALID: return "invalid argument";
        case MT_ERR_NOMEM: return "out of memory";
        case MT_ERR_STATE: return "invalid runtime state";
        case MT_ERR_CLOSED: return "closed";
        case MT_ERR_CANCELLED: return "cancelled";
        case MT_ERR_WOULD_BLOCK: return "operation would block";
        case MT_ERR_TIMEOUT: return "operation timed out";
        case MT_ERR_IO: return "i/o error";
        case MT_ERR_BACKEND: return "i/o backend error";
        case MT_ERR_ADDRINFO: return "address resolution error";
        case MT_ERR_UNSUPPORTED: return "operation unsupported on this platform";
        default: return "unknown microthread error";
    }
}

const char *mt_task_status_name(mt_task_status_t status) {
    switch (status) {
        case MT_TASK_STATUS_READY: return "ready";
        case MT_TASK_STATUS_RUNNING: return "running";
        case MT_TASK_STATUS_SLEEPING: return "sleeping";
        case MT_TASK_STATUS_WAITING_CHAN: return "waiting_chan";
        case MT_TASK_STATUS_WAITING_JOIN: return "waiting_join";
        case MT_TASK_STATUS_DONE: return "done";
        case MT_TASK_STATUS_CANCELLED: return "cancelled";
        case MT_TASK_STATUS_WAITING_SELECT: return "waiting_select";
        case MT_TASK_STATUS_WAITING_FD: return "waiting_fd";
        default: return "unknown";
    }
}

int mt_last_os_error(void) {
    return mt_tls_last_os_error;
}

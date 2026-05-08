#ifndef GT_TESTING
#define GT_TESTING
#endif
#include "gt.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define CHECK(cond) do { \
    if (!(cond)) { \
        fprintf(stderr, "CHECK failed at %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        abort(); \
    } \
} while (0)

#define ARRAY_LEN(a) (sizeof(a) / sizeof((a)[0]))

static int g_counter;
static int g_events[8192];
static size_t g_event_count;

static void reset_log(void) {
    memset(g_events, 0, sizeof(g_events));
    g_event_count = 0;
}

static void log_event(int event) {
    CHECK(g_event_count < ARRAY_LEN(g_events));
    g_events[g_event_count++] = event;
}

static void reset_runtime(void) {
    gt_shutdown();
    gt_test_reset_faults();
    CHECK(gt_init() == GT_OK);
    g_counter = 0;
    reset_log();
}

static void finish_runtime(void) {
    gt_shutdown();
    gt_test_reset_faults();
}

static void handle_counts(size_t *allocs, size_t *frees) {
    gt_test_handle_memory_counters(allocs, frees);
}

static void noop_task(void *arg) {
    if (arg) {
        (*(int *)arg)++;
    }
}

static void yield_once_task(void *arg) {
    (void)arg;
    log_event(10);
    gt_yield();
    log_event(11);
}

static void sleep_then_done_task(void *arg) {
    int ms = arg ? *(int *)arg : 1;
    log_event(20);
    gt_sleep_ms((uint64_t)ms);
    log_event(21);
}

static void many_yields_task(void *arg) {
    int n = arg ? *(int *)arg : 1;
    for (int i = 0; i < n; ++i) {
        gt_yield();
    }
    g_counter++;
}

typedef struct status_arg {
    gt_task_handle_t *handle;
    gt_task_status_t expected;
    int result;
} status_arg_t;

static void self_status_task(void *arg) {
    status_arg_t *sa = (status_arg_t *)arg;
    gt_task_status_t status = GT_TASK_STATUS_DONE;
    sa->result = gt_task_status(sa->handle, &status);
    CHECK(sa->result == GT_OK);
    CHECK(status == GT_TASK_STATUS_RUNNING);
}

typedef struct join_arg {
    gt_task_handle_t *handle;
    int result;
    int event_before;
    int event_after;
} join_arg_t;

static void joiner_task(void *arg) {
    join_arg_t *ja = (join_arg_t *)arg;
    if (ja->event_before) {
        log_event(ja->event_before);
    }
    ja->result = gt_join(ja->handle);
    if (ja->event_after) {
        log_event(ja->event_after);
    }
}

typedef struct self_join_arg {
    gt_task_handle_t *self;
    int result;
} self_join_arg_t;

static void self_join_task(void *arg) {
    self_join_arg_t *sja = (self_join_arg_t *)arg;
    sja->result = gt_join(sja->self);
}

typedef struct chan_wait_arg {
    gt_chan_t *ch;
    int value;
    int result;
} chan_wait_arg_t;

static void chan_send_wait_task(void *arg) {
    chan_wait_arg_t *ca = (chan_wait_arg_t *)arg;
    ca->result = gt_chan_send(ca->ch, &ca->value);
}

static void chan_recv_wait_task(void *arg) {
    chan_wait_arg_t *ca = (chan_wait_arg_t *)arg;
    ca->result = gt_chan_recv(ca->ch, &ca->value);
}

typedef struct status_observer_arg {
    gt_task_handle_t *target;
    gt_task_status_t expected;
    int should_cancel;
    int result;
} status_observer_arg_t;

static void status_observer_task(void *arg) {
    status_observer_arg_t *oa = (status_observer_arg_t *)arg;
    gt_task_status_t status = GT_TASK_STATUS_DONE;
    oa->result = gt_task_status(oa->target, &status);
    CHECK(oa->result == GT_OK);
    CHECK(status == oa->expected);
    if (oa->should_cancel) {
        CHECK(gt_task_cancel(oa->target) == GT_OK);
    }
}

typedef struct wake_sender_arg {
    gt_chan_t *ch;
    int value;
} wake_sender_arg_t;

static void wake_sender_task(void *arg) {
    wake_sender_arg_t *wa = (wake_sender_arg_t *)arg;
    CHECK(gt_chan_send(wa->ch, &wa->value) == GT_OK);
}

static void wake_receiver_task(void *arg) {
    wake_sender_arg_t *wa = (wake_sender_arg_t *)arg;
    int out = 0;
    CHECK(gt_chan_recv(wa->ch, &out) == GT_OK);
    CHECK(out == wa->value);
}

typedef struct parent_child_arg {
    int child_ran;
    int join_result;
} parent_child_arg_t;

static void child_marks_task(void *arg) {
    parent_child_arg_t *pa = (parent_child_arg_t *)arg;
    pa->child_ran++;
    gt_yield();
    pa->child_ran++;
}

static void parent_joins_child_task(void *arg) {
    parent_child_arg_t *pa = (parent_child_arg_t *)arg;
    gt_task_handle_t *h = gt_go_handle(child_marks_task, pa);
    CHECK(h != NULL);
    pa->join_result = gt_join(h);
    gt_task_handle_release(h);
}

typedef struct cancel_loop_arg {
    gt_task_handle_t *self;
    int saw_cancel;
} cancel_loop_arg_t;

static void self_cancel_task(void *arg) {
    cancel_loop_arg_t *ca = (cancel_loop_arg_t *)arg;
    CHECK(gt_task_cancel(ca->self) == GT_OK);
    ca->saw_cancel = gt_task_cancelled();
}

typedef struct cancel_sleep_arg {
    int saw_cancel;
} cancel_sleep_arg_t;

static void cancellable_sleep_task(void *arg) {
    cancel_sleep_arg_t *ca = (cancel_sleep_arg_t *)arg;
    gt_sleep_ms(1000);
    ca->saw_cancel = gt_task_cancelled();
}

typedef struct cancel_after_yield_arg {
    gt_task_handle_t *target;
    int join_result;
} cancel_after_yield_arg_t;

static void cancel_after_yield_task(void *arg) {
    cancel_after_yield_arg_t *ca = (cancel_after_yield_arg_t *)arg;
    gt_yield();
    CHECK(gt_task_cancel(ca->target) == GT_OK);
    ca->join_result = gt_join(ca->target);
}

static void cpu_bound_task(void *arg) {
    int *flag = (int *)arg;
    volatile unsigned long sum = 0;
    for (unsigned long i = 0; i < 200000UL; ++i) {
        sum += i;
    }
    *flag = (sum > 0) ? 1 : 2;
}

static void run_regression_tests(void) {
    reset_runtime();
    CHECK(gt_go(noop_task, &g_counter) > 0);
    CHECK(gt_go_with_stack(noop_task, &g_counter, GT_MIN_STACK_SIZE) > 0);
    CHECK(gt_run() == GT_OK);
    CHECK(g_counter == 2);
    CHECK(gt_debug_live_task_count() == 0);
    finish_runtime();

    reset_runtime();
    gt_chan_t *ch = gt_chan_create(sizeof(int), 0);
    CHECK(ch != NULL);
    wake_sender_arg_t arg = { ch, 42 };
    CHECK(gt_go(wake_sender_task, &arg) > 0);
    CHECK(gt_go(wake_receiver_task, &arg) > 0);
    CHECK(gt_run() == GT_OK);
    CHECK(gt_chan_destroy(ch) == GT_OK);
    finish_runtime();
}

static void run_handle_creation_tests(void) {
    reset_runtime();
    gt_task_handle_t *h = gt_go_handle(noop_task, &g_counter);
    CHECK(h != NULL);
    gt_task_status_t status = GT_TASK_STATUS_DONE;
    CHECK(gt_task_status(h, &status) == GT_OK);
    CHECK(status == GT_TASK_STATUS_READY);
    CHECK(gt_run() == GT_OK);
    CHECK(g_counter == 1);
    CHECK(gt_task_status(h, &status) == GT_OK);
    CHECK(status == GT_TASK_STATUS_DONE);
    gt_task_handle_release(h);
    finish_runtime();

    reset_runtime();
    CHECK(gt_go_handle(NULL, NULL) == NULL);
    h = gt_go_handle_with_stack(noop_task, &g_counter, GT_MIN_STACK_SIZE);
    CHECK(h != NULL);
    CHECK(gt_go_handle_with_stack(noop_task, &g_counter, GT_MIN_STACK_SIZE - 1u) == NULL);
    CHECK(gt_run() == GT_OK);
    gt_task_handle_release(h);
    finish_runtime();

    reset_runtime();
    enum { MANY = 64 };
    gt_task_handle_t *handles[MANY];
    for (int i = 0; i < MANY; ++i) {
        handles[i] = gt_go_handle(noop_task, &g_counter);
        CHECK(handles[i] != NULL);
        for (int j = 0; j < i; ++j) {
            CHECK(handles[i] != handles[j]);
        }
    }
    CHECK(gt_go(noop_task, &g_counter) > 0);
    CHECK(gt_run() == GT_OK);
    CHECK(g_counter == MANY + 1);
    for (int i = 0; i < MANY; ++i) {
        gt_task_handle_release(handles[i]);
    }
    finish_runtime();

    reset_runtime();
    size_t ha0 = 0, hf0 = 0, ha1 = 0, hf1 = 0;
    handle_counts(&ha0, &hf0);
    gt_test_fail_next_handle_alloc();
    CHECK(gt_go_handle(noop_task, NULL) == NULL);
    gt_test_fail_next_task_alloc();
    CHECK(gt_go_handle(noop_task, NULL) == NULL);
    gt_test_fail_next_stack_alloc();
    CHECK(gt_go_handle(noop_task, NULL) == NULL);
    gt_test_fail_next_context_make();
    CHECK(gt_go_handle(noop_task, NULL) == NULL);
    handle_counts(&ha1, &hf1);
    CHECK((ha1 - ha0) == (hf1 - hf0));
    CHECK(gt_debug_live_task_count() == 0);
    finish_runtime();
}

static void run_join_tests(void) {
    reset_runtime();
    gt_task_handle_t *target = gt_go_handle(noop_task, &g_counter);
    CHECK(target != NULL);
    join_arg_t joiner = { target, 123, 1, 2 };
    CHECK(gt_go(joiner_task, &joiner) > 0);
    CHECK(gt_run() == GT_OK);
    CHECK(joiner.result == GT_OK);
    CHECK(g_event_count == 2 && g_events[0] == 1 && g_events[1] == 2);
    gt_task_handle_release(target);
    finish_runtime();

    reset_runtime();
    target = gt_go_handle(yield_once_task, NULL);
    CHECK(target != NULL);
    joiner = (join_arg_t){ target, 123, 3, 4 };
    CHECK(gt_go(joiner_task, &joiner) > 0);
    CHECK(gt_run() == GT_OK);
    CHECK(joiner.result == GT_OK);
    CHECK(g_events[0] == 10 && g_events[1] == 3 && g_events[2] == 11 && g_events[3] == 4);
    gt_task_handle_release(target);
    finish_runtime();

    reset_runtime();
    int ms = 1;
    target = gt_go_handle(sleep_then_done_task, &ms);
    CHECK(target != NULL);
    joiner = (join_arg_t){ target, 123, 5, 6 };
    CHECK(gt_go(joiner_task, &joiner) > 0);
    CHECK(gt_run() == GT_OK);
    CHECK(joiner.result == GT_OK);
    gt_task_handle_release(target);
    finish_runtime();

    reset_runtime();
    gt_chan_t *ch = gt_chan_create(sizeof(int), 0);
    CHECK(ch != NULL);
    chan_wait_arg_t send_arg = { ch, 7, 123 };
    target = gt_go_handle(chan_send_wait_task, &send_arg);
    CHECK(target != NULL);
    joiner = (join_arg_t){ target, 123, 0, 0 };
    wake_sender_arg_t wake = { ch, 7 };
    CHECK(gt_go(joiner_task, &joiner) > 0);
    CHECK(gt_go(wake_receiver_task, &wake) > 0);
    CHECK(gt_run() == GT_OK);
    CHECK(send_arg.result == GT_OK);
    CHECK(joiner.result == GT_OK);
    gt_task_handle_release(target);
    CHECK(gt_chan_destroy(ch) == GT_OK);
    finish_runtime();

    reset_runtime();
    ch = gt_chan_create(sizeof(int), 0);
    CHECK(ch != NULL);
    chan_wait_arg_t recv_arg = { ch, 0, 123 };
    target = gt_go_handle(chan_recv_wait_task, &recv_arg);
    CHECK(target != NULL);
    joiner = (join_arg_t){ target, 123, 0, 0 };
    wake = (wake_sender_arg_t){ ch, 99 };
    CHECK(gt_go(joiner_task, &joiner) > 0);
    CHECK(gt_go(wake_sender_task, &wake) > 0);
    CHECK(gt_run() == GT_OK);
    CHECK(recv_arg.result == GT_OK);
    CHECK(recv_arg.value == 99);
    CHECK(joiner.result == GT_OK);
    gt_task_handle_release(target);
    CHECK(gt_chan_destroy(ch) == GT_OK);
    finish_runtime();

    reset_runtime();
    target = gt_go_handle(noop_task, &g_counter);
    CHECK(target != NULL);
    CHECK(gt_run() == GT_OK);
    CHECK(gt_join(target) == GT_OK);
    gt_task_handle_release(target);
    finish_runtime();

    reset_runtime();
    ms = 1;
    target = gt_go_handle(sleep_then_done_task, &ms);
    CHECK(target != NULL);
    enum { JOINERS = 5 };
    join_arg_t joiners[JOINERS];
    for (int i = 0; i < JOINERS; ++i) {
        joiners[i] = (join_arg_t){ target, 123, 0, 20 + i };
        CHECK(gt_go(joiner_task, &joiners[i]) > 0);
    }
    CHECK(gt_run() == GT_OK);
    for (int i = 0; i < JOINERS; ++i) {
        CHECK(joiners[i].result == GT_OK);
    }
    gt_task_handle_release(target);
    finish_runtime();

    reset_runtime();
    self_join_arg_t sja = { NULL, 0 };
    gt_task_handle_t *h = gt_go_handle(self_join_task, &sja);
    CHECK(h != NULL);
    sja.self = h;
    CHECK(gt_run() == GT_OK);
    CHECK(sja.result == GT_ERR_STATE);
    gt_task_handle_release(h);
    finish_runtime();

    reset_runtime();
    CHECK(gt_join(NULL) == GT_ERR_INVALID);
    target = gt_go_handle(sleep_then_done_task, &ms);
    CHECK(target != NULL);
    CHECK(gt_join(target) == GT_ERR_STATE);
    CHECK(gt_run() == GT_OK);
    gt_task_handle_release(target);
    finish_runtime();

    reset_runtime();
    gt_task_handle_t *c = gt_go_handle(noop_task, &g_counter);
    CHECK(c != NULL);
    join_arg_t b_arg = { c, 123, 0, 0 };
    gt_task_handle_t *b = gt_go_handle(joiner_task, &b_arg);
    CHECK(b != NULL);
    join_arg_t a_arg = { b, 123, 0, 0 };
    gt_task_handle_t *a = gt_go_handle(joiner_task, &a_arg);
    CHECK(a != NULL);
    CHECK(gt_run() == GT_OK);
    CHECK(a_arg.result == GT_OK && b_arg.result == GT_OK);
    gt_task_handle_release(a);
    gt_task_handle_release(b);
    gt_task_handle_release(c);
    finish_runtime();

    reset_runtime();
    gt_task_handle_t *ha = NULL;
    gt_task_handle_t *hb = NULL;
    join_arg_t ja = { NULL, 0, 0, 0 };
    join_arg_t jb = { NULL, 0, 0, 0 };
    ha = gt_go_handle(joiner_task, &ja);
    hb = gt_go_handle(joiner_task, &jb);
    CHECK(ha != NULL && hb != NULL);
    ja.handle = hb;
    jb.handle = ha;
    CHECK(gt_run() == GT_ERR_STATE);
    CHECK(gt_debug_join_waiting_task_count() == 2);
    gt_shutdown();
    gt_task_handle_release(ha);
    gt_task_handle_release(hb);
}

static void run_status_tests(void) {
    reset_runtime();
    gt_task_handle_t *h = gt_go_handle(noop_task, &g_counter);
    CHECK(h != NULL);
    gt_task_status_t status = GT_TASK_STATUS_DONE;
    CHECK(gt_task_status(h, &status) == GT_OK);
    CHECK(status == GT_TASK_STATUS_READY);
    CHECK(gt_task_status(NULL, &status) == GT_ERR_INVALID);
    CHECK(gt_task_status(h, NULL) == GT_ERR_INVALID);
    CHECK(gt_run() == GT_OK);
    CHECK(gt_task_status(h, &status) == GT_OK);
    CHECK(status == GT_TASK_STATUS_DONE);
    gt_task_handle_release(h);
    finish_runtime();

    reset_runtime();
    status_arg_t sa = { NULL, GT_TASK_STATUS_RUNNING, 0 };
    h = gt_go_handle(self_status_task, &sa);
    CHECK(h != NULL);
    sa.handle = h;
    CHECK(gt_run() == GT_OK);
    gt_task_handle_release(h);
    finish_runtime();

    reset_runtime();
    int ms = 10;
    h = gt_go_handle(sleep_then_done_task, &ms);
    CHECK(h != NULL);
    status_observer_arg_t obs = { h, GT_TASK_STATUS_SLEEPING, 1, 0 };
    CHECK(gt_go(status_observer_task, &obs) > 0);
    CHECK(gt_run() == GT_OK);
    CHECK(gt_task_status(h, &status) == GT_OK);
    CHECK(status == GT_TASK_STATUS_CANCELLED);
    gt_task_handle_release(h);
    finish_runtime();

    reset_runtime();
    gt_chan_t *ch = gt_chan_create(sizeof(int), 0);
    CHECK(ch != NULL);
    chan_wait_arg_t ca = { ch, 55, 123 };
    h = gt_go_handle(chan_send_wait_task, &ca);
    CHECK(h != NULL);
    obs = (status_observer_arg_t){ h, GT_TASK_STATUS_WAITING_CHAN, 1, 0 };
    CHECK(gt_go(status_observer_task, &obs) > 0);
    CHECK(gt_run() == GT_OK);
    CHECK(ca.result == GT_ERR_CANCELLED);
    gt_task_handle_release(h);
    CHECK(gt_chan_destroy(ch) == GT_OK);
    finish_runtime();

    reset_runtime();
    ch = gt_chan_create(sizeof(int), 0);
    CHECK(ch != NULL);
    ca = (chan_wait_arg_t){ ch, 0, 123 };
    h = gt_go_handle(chan_recv_wait_task, &ca);
    CHECK(h != NULL);
    obs = (status_observer_arg_t){ h, GT_TASK_STATUS_WAITING_CHAN, 1, 0 };
    CHECK(gt_go(status_observer_task, &obs) > 0);
    CHECK(gt_run() == GT_OK);
    CHECK(ca.result == GT_ERR_CANCELLED);
    gt_task_handle_release(h);
    CHECK(gt_chan_destroy(ch) == GT_OK);
    finish_runtime();

    reset_runtime();
    int long_ms = 10;
    gt_task_handle_t *target = gt_go_handle(sleep_then_done_task, &long_ms);
    CHECK(target != NULL);
    join_arg_t jarg = { target, 123, 0, 0 };
    h = gt_go_handle(joiner_task, &jarg);
    CHECK(h != NULL);
    obs = (status_observer_arg_t){ h, GT_TASK_STATUS_WAITING_JOIN, 1, 0 };
    CHECK(gt_go(status_observer_task, &obs) > 0);
    CHECK(gt_run() == GT_OK);
    CHECK(jarg.result == GT_ERR_CANCELLED);
    gt_task_handle_release(h);
    gt_task_handle_release(target);
    finish_runtime();
}

static void run_cancellation_tests(void) {
    reset_runtime();
    gt_task_handle_t *h = gt_go_handle(noop_task, &g_counter);
    CHECK(h != NULL);
    CHECK(gt_task_cancel(h) == GT_OK);
    CHECK(gt_run() == GT_OK);
    CHECK(g_counter == 0);
    CHECK(gt_join(h) == GT_ERR_CANCELLED);
    gt_task_status_t status = GT_TASK_STATUS_DONE;
    CHECK(gt_task_status(h, &status) == GT_OK);
    CHECK(status == GT_TASK_STATUS_CANCELLED);
    gt_task_handle_release(h);
    finish_runtime();

    reset_runtime();
    cancel_loop_arg_t cla = { NULL, 0 };
    h = gt_go_handle(self_cancel_task, &cla);
    CHECK(h != NULL);
    cla.self = h;
    CHECK(gt_run() == GT_OK);
    CHECK(cla.saw_cancel != 0);
    CHECK(gt_join(h) == GT_ERR_CANCELLED);
    gt_task_handle_release(h);
    finish_runtime();

    reset_runtime();
    cancel_sleep_arg_t csa = { 0 };
    h = gt_go_handle(cancellable_sleep_task, &csa);
    CHECK(h != NULL);
    cancel_after_yield_arg_t canceller = { h, 0 };
    CHECK(gt_go(cancel_after_yield_task, &canceller) > 0);
    CHECK(gt_run() == GT_OK);
    CHECK(csa.saw_cancel != 0);
    CHECK(canceller.join_result == GT_ERR_CANCELLED);
    gt_task_handle_release(h);
    finish_runtime();

    reset_runtime();
    gt_chan_t *ch = gt_chan_create(sizeof(int), 0);
    CHECK(ch != NULL);
    chan_wait_arg_t send_arg = { ch, 1, 123 };
    h = gt_go_handle(chan_send_wait_task, &send_arg);
    CHECK(h != NULL);
    canceller = (cancel_after_yield_arg_t){ h, 0 };
    CHECK(gt_go(cancel_after_yield_task, &canceller) > 0);
    CHECK(gt_run() == GT_OK);
    CHECK(send_arg.result == GT_ERR_CANCELLED);
    CHECK(canceller.join_result == GT_ERR_CANCELLED);
    gt_task_handle_release(h);
    CHECK(gt_chan_destroy(ch) == GT_OK);
    finish_runtime();

    reset_runtime();
    ch = gt_chan_create(sizeof(int), 0);
    CHECK(ch != NULL);
    chan_wait_arg_t recv_arg = { ch, 0, 123 };
    h = gt_go_handle(chan_recv_wait_task, &recv_arg);
    CHECK(h != NULL);
    canceller = (cancel_after_yield_arg_t){ h, 0 };
    CHECK(gt_go(cancel_after_yield_task, &canceller) > 0);
    CHECK(gt_run() == GT_OK);
    CHECK(recv_arg.result == GT_ERR_CANCELLED);
    CHECK(canceller.join_result == GT_ERR_CANCELLED);
    gt_task_handle_release(h);
    CHECK(gt_chan_destroy(ch) == GT_OK);
    finish_runtime();

    reset_runtime();
    CHECK(gt_task_cancel(NULL) == GT_ERR_INVALID);
    h = gt_go_handle(noop_task, &g_counter);
    CHECK(h != NULL);
    CHECK(gt_task_cancel(h) == GT_OK);
    CHECK(gt_task_cancel(h) == GT_OK);
    CHECK(gt_run() == GT_OK);
    gt_task_handle_release(h);
    finish_runtime();

    reset_runtime();
    int flag = 0;
    h = gt_go_handle(cpu_bound_task, &flag);
    CHECK(h != NULL);
    CHECK(gt_task_cancel(h) == GT_OK);
    CHECK(gt_run() == GT_OK);
    CHECK(flag == 0); /* Ready task cancellation prevents it from starting. */
    gt_task_handle_release(h);
    finish_runtime();
}

static void run_release_and_lifecycle_tests(void) {
    reset_runtime();
    size_t ha0 = 0, hf0 = 0, ha1 = 0, hf1 = 0;
    handle_counts(&ha0, &hf0);
    gt_task_handle_t *h = gt_go_handle(noop_task, &g_counter);
    CHECK(h != NULL);
    CHECK(gt_run() == GT_OK);
    gt_task_handle_release(h);
    handle_counts(&ha1, &hf1);
    CHECK((ha1 - ha0) == (hf1 - hf0));
    finish_runtime();

    reset_runtime();
    handle_counts(&ha0, &hf0);
    h = gt_go_handle(noop_task, &g_counter);
    CHECK(h != NULL);
    gt_task_handle_release(h);
    CHECK(gt_run() == GT_OK);
    handle_counts(&ha1, &hf1);
    CHECK((ha1 - ha0) == (hf1 - hf0));
    CHECK(g_counter == 1);
    gt_task_handle_release(NULL);
    finish_runtime();

    reset_runtime();
    parent_child_arg_t pa = { 0, 123 };
    h = gt_go_handle(parent_joins_child_task, &pa);
    CHECK(h != NULL);
    CHECK(gt_run() == GT_OK);
    CHECK(pa.child_ran == 2);
    CHECK(pa.join_result == GT_OK);
    gt_task_handle_release(h);
    finish_runtime();

    reset_runtime();
    int ms = 1000;
    h = gt_go_handle(sleep_then_done_task, &ms);
    CHECK(h != NULL);
    gt_shutdown();
    gt_task_handle_release(h);

    reset_runtime();
    gt_chan_t *ch = gt_chan_create(sizeof(int), 0);
    CHECK(ch != NULL);
    chan_wait_arg_t ca = { ch, 0, 123 };
    h = gt_go_handle(chan_recv_wait_task, &ca);
    CHECK(h != NULL);
    CHECK(gt_run() == GT_ERR_STATE);
    gt_shutdown();
    gt_task_handle_release(h);
    CHECK(gt_chan_destroy(ch) == GT_OK || gt_chan_destroy(ch) == GT_ERR_INVALID);
}

static void run_scheduler_tests(void) {
    reset_runtime();
    int ms = 1;
    gt_task_handle_t *target = gt_go_handle(sleep_then_done_task, &ms);
    CHECK(target != NULL);
    join_arg_t joiner = { target, 123, 1, 3 };
    CHECK(gt_go(joiner_task, &joiner) > 0);
    CHECK(gt_go(noop_task, &g_counter) > 0);
    CHECK(gt_run() == GT_OK);
    CHECK(g_counter == 1);
    CHECK(joiner.result == GT_OK);
    gt_task_handle_release(target);
    finish_runtime();

    reset_runtime();
    target = gt_go_handle(noop_task, &g_counter);
    CHECK(target != NULL);
    enum { JOINERS = 3 };
    join_arg_t joiners[JOINERS];
    for (int i = 0; i < JOINERS; ++i) {
        joiners[i] = (join_arg_t){ target, 0, 0, 10 + i };
        CHECK(gt_go(joiner_task, &joiners[i]) > 0);
    }
    CHECK(gt_run() == GT_OK);
    CHECK(gt_debug_join_waiting_task_count() == 0);
    for (int i = 0; i < JOINERS; ++i) {
        CHECK(joiners[i].result == GT_OK);
    }
    gt_task_handle_release(target);
    finish_runtime();
}

static void run_memory_tests(void) {
    reset_runtime();
    size_t ha0 = 0, hf0 = 0, ha1 = 0, hf1 = 0;
    handle_counts(&ha0, &hf0);
    enum { N = 32 };
    gt_task_handle_t *handles[N];
    for (int i = 0; i < N; ++i) {
        handles[i] = gt_go_handle(noop_task, &g_counter);
        CHECK(handles[i] != NULL);
    }
    CHECK(gt_run() == GT_OK);
    for (int i = 0; i < N; ++i) {
        CHECK(gt_join(handles[i]) == GT_OK);
        gt_task_handle_release(handles[i]);
    }
    handle_counts(&ha1, &hf1);
    CHECK((ha1 - ha0) == (hf1 - hf0));
    finish_runtime();
}

static void run_stress_tests(void) {
    reset_runtime();
#ifdef GT_FULL_STRESS
    enum { TASKS = 10000 };
#else
    enum { TASKS = 1000 };
#endif
    gt_task_handle_t **handles = (gt_task_handle_t **)calloc((size_t)TASKS, sizeof(*handles));
    CHECK(handles != NULL);
    for (int i = 0; i < TASKS; ++i) {
        handles[i] = gt_go_handle(noop_task, &g_counter);
        CHECK(handles[i] != NULL);
    }
    CHECK(gt_run() == GT_OK);
    CHECK(g_counter == TASKS);
    for (int i = 0; i < TASKS; ++i) {
        CHECK(gt_join(handles[i]) == GT_OK);
        gt_task_handle_release(handles[i]);
    }
    free(handles);
    finish_runtime();

    reset_runtime();
#ifdef GT_FULL_STRESS
    enum { YTASKS = 1000, YIELDS = 100 };
#else
    enum { YTASKS = 100, YIELDS = 20 };
#endif
    handles = (gt_task_handle_t **)calloc((size_t)YTASKS, sizeof(*handles));
    CHECK(handles != NULL);
    int yield_count = YIELDS;
    for (int i = 0; i < YTASKS; ++i) {
        handles[i] = gt_go_handle(many_yields_task, &yield_count);
        CHECK(handles[i] != NULL);
    }
    CHECK(gt_run() == GT_OK);
    CHECK(g_counter == YTASKS);
    for (int i = 0; i < YTASKS; ++i) {
        CHECK(gt_join(handles[i]) == GT_OK);
        gt_task_handle_release(handles[i]);
    }
    free(handles);
    finish_runtime();

    reset_runtime();
#ifdef GT_FULL_STRESS
    enum { ONE_TARGET_JOINERS = 100 };
#else
    enum { ONE_TARGET_JOINERS = 20 };
#endif
    int sleep_ms = 1;
    gt_task_handle_t *target = gt_go_handle(sleep_then_done_task, &sleep_ms);
    CHECK(target != NULL);
    join_arg_t *joiners = (join_arg_t *)calloc((size_t)ONE_TARGET_JOINERS, sizeof(*joiners));
    CHECK(joiners != NULL);
    for (int i = 0; i < ONE_TARGET_JOINERS; ++i) {
        joiners[i].handle = target;
        joiners[i].result = 123;
        CHECK(gt_go(joiner_task, &joiners[i]) > 0);
    }
    CHECK(gt_run() == GT_OK);
    for (int i = 0; i < ONE_TARGET_JOINERS; ++i) {
        CHECK(joiners[i].result == GT_OK);
    }
    free(joiners);
    gt_task_handle_release(target);
    finish_runtime();
}

int main(void) {
    run_regression_tests();
    run_handle_creation_tests();
    run_join_tests();
    run_status_tests();
    run_cancellation_tests();
    run_release_and_lifecycle_tests();
    run_scheduler_tests();
    run_memory_tests();
    run_stress_tests();
    printf("v0.4 handle/join/cancellation tests passed\n");
    return 0;
}

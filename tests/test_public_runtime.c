#include "microthread.h"
#include "microthread_io.h"

#include <assert.h>
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>

static mt_chan_t *g_ch;
static atomic_int g_sum;
static atomic_int g_selected;
static atomic_int g_workers_seen;

static void producer(void *arg) {
    (void)arg;
    for (int i = 1; i <= 5; ++i) {
        assert(mt_chan_send(g_ch, &i) == MT_OK);
    }
    assert(mt_chan_close(g_ch) == MT_OK);
}

static void consumer(void *arg) {
    (void)arg;
    int value = 0;
    while (mt_chan_recv(g_ch, &value) == MT_OK) {
        atomic_fetch_add(&g_sum, value);
        mt_yield();
    }
}

static void select_timeout_task(void *arg) {
    (void)arg;
    int value = 0;
    size_t selected = 99;
    mt_select_case_t cases[2];
    memset(cases, 0, sizeof(cases));
    cases[0].op = MT_SELECT_RECV;
    cases[0].ch = g_ch;
    cases[0].value = &value;
    cases[1].op = MT_SELECT_TIMEOUT;
    cases[1].timeout_ms = 1;
    assert(mt_select(cases, 2, &selected) == MT_OK);
    atomic_store(&g_selected, (int)selected);
}

static void worker_counter(void *arg) {
    (void)arg;
    atomic_fetch_add(&g_workers_seen, mt_runtime_workers() > 0 ? 1 : 0);
    mt_yield();
}

int main(void) {
    assert(mt_init() == MT_OK);
    g_ch = mt_chan_create(sizeof(int), 2);
    assert(g_ch != NULL);
    assert(mt_go(producer, NULL) > 0);
    assert(mt_go(consumer, NULL) > 0);
    assert(mt_runtime_start(2) == MT_OK);
    assert(atomic_load(&g_sum) == 15);
    assert(mt_chan_destroy(g_ch) == MT_OK);
    g_ch = NULL;
    mt_shutdown();

    assert(mt_init() == MT_OK);
    g_ch = mt_chan_create(sizeof(int), 0);
    assert(g_ch != NULL);
    assert(mt_go(select_timeout_task, NULL) > 0);
    assert(mt_run() == MT_OK);
    assert(atomic_load(&g_selected) == 1);
    assert(mt_chan_destroy(g_ch) == MT_OK);
    g_ch = NULL;
    mt_shutdown();

    assert(mt_init() == MT_OK);
    for (int i = 0; i < 16; ++i) {
        assert(mt_go(worker_counter, NULL) > 0);
    }
    assert(mt_runtime_start(3) == MT_OK);
    assert(atomic_load(&g_workers_seen) == 16);
    mt_shutdown();

    puts("public runtime user-story test passed");
    return 0;
}

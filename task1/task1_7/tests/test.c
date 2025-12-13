#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <pthread.h>

#include "uthread.h"
#include "uthread_map.h"

#define N_TASKS (3 * UTHREAD_WORKER_COUNT)

static int global_steps = 0;

static void *mapped_task(void *arg) {
    long id = (long)arg;

    pthread_t self = pthread_self();

    for (int i = 0; i < 3; ++i) {
        printf("[mapped_task %ld] step %d, os_thread=%lu\n",
               id, i, (unsigned long)self);

        global_steps++;

        uthread_yield();
    }

    return (void *)(id + 1000);
}

int main(void) {
    printf("[test_uthread_map_3_per_worker] start\n");
    printf("Configured workers (UTHREAD_WORKER_COUNT) = %d\n", UTHREAD_WORKER_COUNT);

    int rc = uthread_map_init();
    assert(rc == 0);

    uthread_t tids[N_TASKS];
    void *results[N_TASKS];

    for (long i = 0; i < N_TASKS; ++i) {
        rc = uthread_map_create(&tids[i], mapped_task, (void *)i);
        assert(rc == 0);
    }

    for (int i = 0; i < N_TASKS; ++i) {
        rc = uthread_map_join(tids[i], &results[i]);
        assert(rc == 0);

        assert((long)results[i] == (long)(i + 1000));
    }

    assert(global_steps == N_TASKS * 3);

    uthread_map_shutdown();

    printf("[test_uthread_map_3_per_worker] OK\n");
    return 0;
}

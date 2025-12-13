#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <time.h>
#include "uthread.h"
#include "uthread_map.h"

static void *mapped_worker(void *arg) {
    int id = (int)(long)arg;

    for (int i = 0; i < 5; ++i) {
        printf("[mapped_worker %d] step %d\n", id, i);
        uthread_yield();
    }

    return (void *)(long)(id * 100);
}

#define N 6

int main(void) {
    printf("[test_uthread_map] start\n");

    int rc = uthread_map_init();
    assert(rc == 0);

    uthread_t threads[N];
    void *results[N] = {0};

    for (int i = 0; i < N; ++i) {
        rc = uthread_map_create(&threads[i], mapped_worker, (void *)(long)i);
        assert(rc == 0);
    }

    for (int i = 0; i < N; ++i) {
        rc = uthread_map_join(threads[i], &results[i]);
        assert(rc == 0);
        assert((long)results[i] == (long)(i * 100));
    }

    uthread_map_shutdown();

    printf("[test_uthread_map] OK\n");
    return 0;
}


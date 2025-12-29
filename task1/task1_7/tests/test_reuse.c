#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <errno.h>
#include <string.h>

#include "uthread.h"

#ifndef STRESS_ITERS
#define STRESS_ITERS 50000
#endif

static void *stress_task(void *arg) {
    long id = (long)arg;
    long sum = 0;

    for (int i = 1; i <= 5; ++i) {
        sum += id * i;
        uthread_yield();
    }

    return (void *)sum;
}

int main(void) {
    printf("[test_uthread_reuse_stress] start, iterations=%d\n", STRESS_ITERS);

    for (long i = 0; i < STRESS_ITERS; ++i) {
        uthread_t t;
        void *ret = NULL;

        int rc = uthread_create(&t, stress_task, (void *)i);
        if (rc != 0) {
            fprintf(stderr,
                    "[stress] FAIL at i=%ld: uthread_create rc=%d errno=%d (%s)\n",
                    i, rc, errno, strerror(errno));
            assert(0 && "uthread_create failed — slots not reused?");
        }

        uthread_run();

        rc = uthread_join(t, &ret);
        assert(rc == 0);

        long expected = i * 15;
        assert((long)ret == expected);

        if ((i % 5000) == 0 && i != 0) {
            printf("[stress] progress: %ld/%d\n", i, STRESS_ITERS);
        }
    }

    printf("[test_uthread_reuse_stress] OK\n");
    return 0;
}

#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <time.h>
#include "uthread.h"

static int counter1 = 0;
static int counter2 = 0;
static int counter3 = 0;

static void *worker1(void *arg) {
    int iterations = (int)(long)arg;

    for (int i = 0; i < iterations; ++i) {
        counter1++;
        if (i % 10 == 0) {
            uthread_yield();
        }
    }

    return (void *)(long)(iterations * 1);
}

static void *worker2(void *arg) {
    int iterations = (int)(long)arg;

    for (int i = 0; i < iterations; ++i) {
        counter2++;
        if (i % 7 == 0) {
            uthread_yield();
        }
    }

    return (void *)(long)(iterations * 2);
}

static void *worker3(void *arg) {
    int iterations = (int)(long)arg;

    time_t start = time(NULL);

    for (int i = 0; i < iterations; ++i) {
        counter3++;
        if (i == iterations / 2) {
            uthread_sleep(1);
        } else if (i % 5 == 0) {
            uthread_yield();
        }
    }

    time_t end = time(NULL);
    assert(end - start >= 1);

    return (void *)(long)(iterations * 3);
}

int main(void) {
    printf("[test_uthread] start\n");

    uthread_t t1, t2, t3;
    int it1 = 100;
    int it2 = 120;
    int it3 = 80;

    int rc;

    rc = uthread_create(&t1, worker1, (void *)(long)it1);
    assert(rc == 0);

    rc = uthread_create(&t2, worker2, (void *)(long)it2);
    assert(rc == 0);

    rc = uthread_create(&t3, worker3, (void *)(long)it3);
    assert(rc == 0);

    uthread_run();

    void *ret1 = NULL;
    void *ret2 = NULL;
    void *ret3 = NULL;

    rc = uthread_join(t1, &ret1);
    assert(rc == 0);

    rc = uthread_join(t2, &ret2);
    assert(rc == 0);

    rc = uthread_join(t3, &ret3);
    assert(rc == 0);

    assert(counter1 == it1);
    assert(counter2 == it2);
    assert(counter3 == it3);

    assert((long)ret1 == it1 * 1);
    assert((long)ret2 == it2 * 2);
    assert((long)ret3 == it3 * 3);

    printf("[test_uthread] OK\n");
    return 0;
}


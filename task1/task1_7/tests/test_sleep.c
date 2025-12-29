#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <time.h>
#include "uthread.h"

static void *worker1(void *arg) {
    (void) arg;
    uthread_sleep(10);

    return (void *)5;
}

int main(void) {
    printf("[test_uthread] start\n");

    uthread_t t1;

    int rc;

    rc = uthread_create(&t1, worker1, NULL);
    assert(rc == 0);

    uthread_run();

    void *ret1 = NULL;

    rc = uthread_join(t1, &ret1);
    assert(rc == 0);


    assert((long)ret1 == 5);

    printf("[test_uthread] OK\n");
    return 0;
}


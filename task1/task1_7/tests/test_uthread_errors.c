// tests/test_uthread_errors.c
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>

#include "uthread.h"

static void *task(void *arg) {
    (void)arg;
    uthread_yield();
    return (void *)1;
}

int main(void) {
    printf("[test_uthread_errors] start\n");

    // 1) create: thread == NULL
    errno = 0;
    int rc = uthread_create(NULL, task, NULL);
    assert(rc == -1);
    assert(errno == EINVAL);

    // 2) create: start_routine == NULL
    errno = 0;
    uthread_t t_bad;
    rc = uthread_create(&t_bad, NULL, NULL);
    assert(rc == -1);
    assert(errno == EINVAL);

    // 3) join: wrong id (negative)
    errno = 0;
    rc = uthread_join(-1, NULL);
    assert(rc == -1);
    assert(errno == EINVAL);

    // 4) join: wrong id (too large)
    errno = 0;
    rc = uthread_join(999999, NULL);
    assert(rc == -1);
    assert(errno == EINVAL);

    // 5) double join
    uthread_t t;
    rc = uthread_create(&t, task, NULL);
    assert(rc == 0);

    uthread_run();

    void *ret = NULL;
    rc = uthread_join(t, &ret);
    assert(rc == 0);
    assert(ret == (void *)1);

    errno = 0;
    rc = uthread_join(t, &ret);
    assert(rc == -1);
    assert(errno == EINVAL);

    printf("[test_uthread_errors] OK\n");
    return 0;
}

#include <assert.h>
#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>

#include "uthread.h"
#include "uthread_map.h"


static void *task(void *arg) {
    (void)arg;

    for (int i = 0; i < 2; ++i) {
        uthread_yield();
    }
    return (void *)5;
}


#define NUM_UTHREADS 4

int main(void) {
    printf("[test_uthread_map_errors] start\n");

    int rc = uthread_map_init(NUM_UTHREADS);
    assert(rc == 0);

    // 1) create: thread == NULL
    errno = 0;
    rc = uthread_map_create(NULL, task, NULL);
    assert(rc == -1);
    assert(errno == EINVAL);

    // 2) create: start_routine == NULL
    errno = 0;
    uthread_t t_bad;
    rc = uthread_map_create(&t_bad, NULL, NULL);
    assert(rc == -1);
    assert(errno == EINVAL);

    // 3) join: invalid id (negative)
    errno = 0;
    rc = uthread_map_join(-1, NULL);
    assert(rc == -1);
    assert(errno == EINVAL);

    // 4) join: invalid id (too large)
    errno = 0;
    rc = uthread_map_join(999999, NULL);
    assert(rc == -1);
    assert(errno == EINVAL);

    // 5) double join
    uthread_t t;
    rc = uthread_map_create(&t, task, NULL);
    assert(rc == 0);

    void *ret = NULL;
    rc = uthread_map_join(t, &ret);
    assert(rc == 0);
    assert(ret == (void *)5);

    errno = 0;
    rc = uthread_map_join(t, &ret);
    assert(rc == -1);
    assert(errno == EINVAL);

    // 6) join с retval == NULL
    uthread_t t2;
    rc = uthread_map_create(&t2, task, NULL);
    assert(rc == 0);
    rc = uthread_map_join(t2, NULL);
    assert(rc == 0);

    uthread_map_shutdown();

    printf("[test_uthread_map_errors] OK\n");
    return 0;
}

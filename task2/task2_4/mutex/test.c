#define _GNU_SOURCE
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdatomic.h>
#include <assert.h>
#include <errno.h>
#include "mutex.h"

#define THREADS 8
#define ITER_PER_THREAD 100000

static mymutex g_mutex;
static int g_counter = 0;

void *worker_increment(void *arg) {
    (void) arg;
    for (int i = 0; i < ITER_PER_THREAD; ++i) {
        int r = mutex_lock(&g_mutex);
        if (r != 0) {
            fprintf(stderr, "mutex_lock failed in worker_increment\n");
            abort();
        }
        g_counter++;
        r = mutex_unlock(&g_mutex);
        if (r != 0) {
            fprintf(stderr, "mutex_unlock failed in worker_increment\n");
            abort();
        }
    }
    return NULL;
}

void test_basic_correctness(void) {
    printf("test_basic_correctness...\n");

    int r = mutex_init(&g_mutex);
    assert(r == 0);
    g_counter = 0;

    pthread_t th[THREADS];

    for (int i = 0; i < THREADS; ++i) {
        int err = pthread_create(&th[i], NULL, worker_increment, NULL);
        assert(err == 0);
    }

    for (int i = 0; i < THREADS; ++i) {
        pthread_join(th[i], NULL);
    }

    int expected = THREADS * ITER_PER_THREAD;
    if (g_counter != expected) {
        fprintf(stderr, "Counter mismatch: got %d, expected %d\n", g_counter, expected);
        abort();
    }

    printf("  OK: counter = %d\n", g_counter);
}


void test_error_handling(void) {
    printf("test_error_handling...\n");

    int r;

    r = mutex_init(NULL);
    if (r == 0) {
        fprintf(stderr, "mutex_init(NULL) should fail\n");
        abort();
    }

    mymutex m;
    r = mutex_init(&m);
    assert(r == 0);

    r = mutex_unlock(&m);
    if (r == 0) {
        fprintf(stderr, "mutex_unlock on UNLOCKED should fail\n");
        abort();
    } else {
        printf("  OK: unlock on UNLOCKED returned error\n");
    }

    r = mutex_lock(&m);
    assert(r == 0);

    r = mutex_unlock(&m);
    assert(r == 0);

    r = mutex_unlock(&m);
    if (r == 0) {
        fprintf(stderr, "Second unlock should fail\n");
        abort();
    } else {
        printf("  OK: second unlock returned error\n");
    }

    r = mutex_lock(NULL);
    if (r == 0) {
        fprintf(stderr, "mutex_lock(NULL) should fail\n");
        abort();
    }
    r = mutex_unlock(NULL);
    if (r == 0) {
        fprintf(stderr, "mutex_unlock(NULL) should fail\n");
        abort();
    }

    printf("  OK: error handling\n");
}

int main(void) {
    test_basic_correctness();
    test_error_handling();

    printf("All tests passed.\n");
    return 0;
}


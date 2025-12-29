#define _GNU_SOURCE
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include "spinlock.h"

#define THREADS 8
#define ITER_PER_THREAD 100000

static spinlock g_spin;
static int g_counter = 0;

void *worker_increment(void *arg) {
    (void) arg;
    for (int i = 0; i < ITER_PER_THREAD; ++i) {
        int r = spinlock_lock(&g_spin);
        if (r != 0) {
            fprintf(stderr, "spin_lock_lock failed in worker_increment\n");
            abort();
        }
        g_counter++;
        r = spinlock_unlock(&g_spin);
        if (r != 0) {
            fprintf(stderr, "spin_lock_unlock failed in worker_increment\n");
            abort();
        }
    }
    return NULL;
}

void test_basic_correctness(void) {
    printf("test_basic_correctness...\n");

    int r = spinlock_init(&g_spin);
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
        fprintf(stderr, "Counter mismatch: got %d, expected %d\n",
                g_counter, expected);
        abort();
    }

    printf("  OK: counter = %d\n", g_counter);
}

void test_error_handling(void) {
    printf("test_error_handling...\n");

    int r;

    // 1) init(NULL) должен вернуть ошибку
    r = spinlock_init(NULL);
    if (r == 0) {
        fprintf(stderr, "spin_lock_init(NULL) should fail\n");
        abort();
    }

    spinlock l;
    r = spinlock_init(&l);
    assert(r == 0);

    // 2) unlock без lock — ожидаем ошибку
    r = spinlock_unlock(&l);
    if (r == 0) {
        fprintf(stderr, "spin_lock_unlock on UNLOCKED should fail\n");
        abort();
    } else {
        printf("  OK: unlock on UNLOCKED returned error\n");
    }

    // 3) двойной unlock
    r = spinlock_lock(&l);
    assert(r == 0);

    r = spinlock_unlock(&l);
    assert(r == 0);

    r = spinlock_unlock(&l);
    if (r == 0) {
        fprintf(stderr, "Second unlock should fail\n");
        abort();
    } else {
        printf("  OK: second unlock returned error\n");
    }

    // 4) lock(NULL) / unlock(NULL)
    r = spinlock_lock(NULL);
    if (r == 0) {
        fprintf(stderr, "spin_lock_lock(NULL) should fail\n");
        abort();
    }
    r = spinlock_unlock(NULL);
    if (r == 0) {
        fprintf(stderr, "spinlock_unlock(NULL) should fail\n");
        abort();
    }

    printf("  OK: error handling\n");
}


int main(void) {
    test_basic_correctness();
    test_error_handling();

    printf("All spin_lock tests passed.\n");
    return 0;
}

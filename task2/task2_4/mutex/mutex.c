#define _GNU_SOURCE
#include "mutex.h"
#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include <sys/syscall.h>
#include <linux/futex.h>
#include <errno.h>
#include <time.h>


static int futex_wait(int *uaddr, int expected, const struct timespec *timeout) {
    return syscall(SYS_futex, uaddr, FUTEX_WAIT, expected, timeout, NULL, 0);
}

static int futex_wake(int *uaddr, int nr_wake) {
    return syscall(SYS_futex, uaddr, FUTEX_WAKE, nr_wake, NULL, NULL, 0);
}

int mutex_init(mymutex *mutex) {
    if (!mutex) {
        return 1;
    }
    __atomic_store_n(&mutex->lock, UNLOCKED, __ATOMIC_RELAXED);
    __atomic_store_n(&mutex->owner, (pid_t)0, __ATOMIC_RELAXED);
    return 0;
}

#define NUM_TRY_LOCK 32

int mutex_lock(mymutex *mutex) {
    if (!mutex) {
        return 1;
    }
    for (int i = 0; i < NUM_TRY_LOCK; i++) {
        int expected = UNLOCKED;
        if (__atomic_compare_exchange_n(&mutex->lock, &expected, LOCKED, 0, __ATOMIC_ACQUIRE, __ATOMIC_RELAXED)) {
            __atomic_store_n(&mutex->owner, gettid(), __ATOMIC_RELAXED);
            return 0;
        }
    }

    while ((1)) {
        int expected = UNLOCKED;
        if (__atomic_compare_exchange_n(&mutex->lock, &expected, LOCKED, 0, __ATOMIC_ACQUIRE, __ATOMIC_RELAXED)) {
            __atomic_store_n(&mutex->owner, gettid(), __ATOMIC_RELAXED);
            return 0;
        }
        int ret_val = futex_wait(&mutex->lock, LOCKED, NULL);
        if (ret_val == -1 && errno != EAGAIN && errno != EINTR) {
            return 1;
        }

    }
    
}

int mutex_unlock(mymutex *mutex) {
    if (!mutex) {
        return 1;
    }
    if (__atomic_load_n(&mutex->owner, __ATOMIC_RELAXED) != gettid()) {
        return 1;
    }

    __atomic_store_n(&mutex->owner, (pid_t)0, __ATOMIC_RELAXED);
    __atomic_store_n(&mutex->lock, UNLOCKED, __ATOMIC_RELEASE);

    int ret_val = futex_wake(&mutex->lock, 1);
    if (ret_val == -1) return 1;

    return 0;
}
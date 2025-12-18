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
    atomic_store(&mutex->lock, UNLOCKED);
    atomic_store(&mutex->owner, (pid_t)0);
    return 0;
}

int mutex_lock(mymutex *mutex) {
    if (!mutex) {
        return 1;
    }
    int expected = UNLOCKED;
    if (atomic_compare_exchange_strong(&mutex->lock, &expected, LOCKED)) {
        atomic_store(&mutex->owner, gettid());
        return 0;
    }

    while ((1)) {
        int cur_lock = atomic_load(&mutex->lock);

        if (cur_lock == UNLOCKED) {
            int expected = UNLOCKED;
            if (atomic_compare_exchange_strong(&mutex->lock, &expected, LOCKED)) {
                atomic_store(&mutex->owner, gettid());
                return 0;
            }
            continue;
        }
        int ret_val = futex_wait((int*)&mutex->lock, LOCKED, NULL);
        if (ret_val == -1 && errno != EAGAIN && errno != EINTR) {
            return 1;
        }

    }
    
}

int mutex_unlock(mymutex *mutex) {
    if (!mutex) {
        return 1;
    }
    if (atomic_load(&mutex->owner) != gettid()) {
        return 1;
    }

    atomic_store(&mutex->owner, (pid_t)0);
    atomic_store(&mutex->lock, UNLOCKED);

    int ret_val = futex_wake((int*)&mutex->lock, 1);
    if (ret_val == -1) return 1;

    return 0;
}
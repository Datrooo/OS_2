#define _GNU_SOURCE
#include "spinlock.h"
#include <stdio.h>
#include <unistd.h>


int spinlock_init(spinlock *lock) {
    if (!lock) {
        return -1;
    }
    __atomic_store_n(&lock->lock, UNLOCKED, __ATOMIC_RELAXED);
    __atomic_store_n(&lock->owner, (pid_t)-1, __ATOMIC_RELAXED);
    return 0;
}

int spinlock_lock(spinlock *lock) {
    if (!lock) {
        return -1;
    }

    int expected = UNLOCKED;

    while (!__atomic_compare_exchange_n(&lock->lock, &expected, LOCKED, 0, __ATOMIC_ACQUIRE, __ATOMIC_RELAXED)) {
        expected = UNLOCKED;
    }

    __atomic_store_n(&lock->owner, gettid(), __ATOMIC_RELAXED);
    return 0;
}

int spinlock_unlock(spinlock *lock) {
    if (!lock) {
        return -1;
    }
    if (__atomic_load_n(&lock->owner, __ATOMIC_RELAXED) != gettid() ||
        __atomic_load_n(&lock->lock, __ATOMIC_RELAXED) != LOCKED){
        return -1;
    }
    __atomic_store_n(&lock->owner, (pid_t)-1, __ATOMIC_RELAXED);
    __atomic_store_n(&lock->lock, UNLOCKED, __ATOMIC_RELEASE);

    return 0;
}

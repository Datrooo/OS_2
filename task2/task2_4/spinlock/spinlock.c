
#define _GNU_SOURCE
#include "spinlock.h"
#include <stdio.h>
#include <unistd.h>


int spinlock_init(spinlock *lock) {
    if (!lock) {
        return -1;
    }
    atomic_store(&lock->lock, UNLOCKED);
    atomic_store(&lock->owner, (pid_t)-1);
    return 0;
}

int spinlock_lock(spinlock *lock) {
    if (!lock) {
        return -1;
    }

    int expected = UNLOCKED;

    while(!atomic_compare_exchange_strong(&lock->lock, &expected, LOCKED)) {
        expected = UNLOCKED;
    }

    atomic_store(&lock->owner, gettid());
    return 0;
}

int spinlock_unlock(spinlock *lock) {
    if (!lock) {
        return -1;
    }
    if (atomic_load(&lock->owner) != gettid() || atomic_load(&lock->lock) != LOCKED){
        return -1;
    }
    atomic_store(&lock->owner, (pid_t)-1);
    int expected = LOCKED;
    atomic_compare_exchange_strong(&lock->lock, &expected, UNLOCKED);

    return 0;
}

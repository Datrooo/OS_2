#ifndef SPINLOCK_H
#define SPINLOCK_H

#include <sys/types.h>

#define UNLOCKED 0
#define LOCKED 1

typedef struct spinlock {
    int lock;
    pid_t owner;
} spinlock;

int spinlock_init(spinlock *spin);
int spinlock_lock(spinlock *spin);
int spinlock_unlock(spinlock *spin);

#endif
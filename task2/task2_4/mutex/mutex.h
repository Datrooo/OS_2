#ifndef MUTEX_H
#define MUTEX_H

#include <stdatomic.h>
#include <sys/types.h>

#define UNLOCKED 0
#define LOCKED 1

typedef struct mymutex {
    atomic_int lock;
    _Atomic (pid_t) owner;
} mymutex;

int mutex_init(mymutex *mutex);
int mutex_lock(mymutex *mutex);
int mutex_unlock(mymutex *mutex);

#endif
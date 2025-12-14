#ifndef UTHREAD_MAP_H
#define UTHREAD_MAP_H

#include "uthread.h"
#include <pthread.h>


#define UTHREAD_WORKERS_MAX 64

int uthread_map_init(int worker_count);

int uthread_map_create(uthread_t *thread,
                       void *(*start_routine)(void *),
                       void *arg);

int uthread_map_join(uthread_t thread, void **retval);

void uthread_map_shutdown(void);

#endif


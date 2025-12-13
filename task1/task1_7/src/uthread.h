#ifndef UTHREAD_H
#define UTHREAD_H

#define _GNU_SOURCE
#include <stdlib.h>
#include <stdio.h>
#include <ucontext.h>
#include <errno.h>
#include <string.h>
#include <time.h>

#define MAX_THREADS 64
#define STACK_SIZE (64 * 1024)

typedef enum {
    THREAD_NEW,
    THREAD_READY,
    THREAD_RUNNING,
    THREAD_FINISHED,
    THREAD_SLEEPING
} thread_state_t;

typedef struct _uthread {
    int id;
    ucontext_t context;
    void *stack;
    void *(*start_routine)(void *);
    void *arg;
    void *retval;
    thread_state_t state;
    int joined;
    time_t wake_time;
} thread_t;

typedef int uthread_t;

int  uthread_create(uthread_t *thread, void *(*start_routine)(void *), void *arg);
void uthread_yield(void);
int  uthread_join(uthread_t thread, void **retval);
void uthread_run(void);
void uthread_sleep(int seconds);

#endif
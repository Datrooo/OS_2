#include "uthread.h"
#include <unistd.h>
#include <time.h>

static __thread thread_t threads[MAX_THREADS];
static __thread int thread_count = 0;
static __thread int current_thread = -1;
static __thread ucontext_t main_context;
static __thread int scheduler_running = 0;

static __thread int free_ids[MAX_THREADS];
static __thread int free_top = 0;

static void thread_wrapper(void) {
    thread_t *t = &threads[current_thread];
    
    t->retval = t->start_routine(t->arg);
    t->state = THREAD_FINISHED;
    
    uthread_yield();
}

int uthread_create(uthread_t *thread, void *(*start_routine)(void *), void *arg) {
    if (thread == NULL || start_routine == NULL) {
        errno = EINVAL;
        return -1;
    }
    
    int tid;
    if (free_top > 0) {
        tid = free_ids[--free_top];
    } else {
        if (thread_count >= MAX_THREADS) { errno = EAGAIN; return -1; }
        tid = thread_count++;
    }

    thread_t *t = &threads[tid];
    
    t->id = tid;
    t->start_routine = start_routine;
    t->arg = arg;
    t->retval = NULL;
    t->state = THREAD_NEW;
    t->joined = 0;
    
    t->stack = malloc(STACK_SIZE);
    if (t->stack == NULL) {
        thread_count--;
        return -1;
    }
    
    if (getcontext(&t->context) == -1) {
        free(t->stack);
        thread_count--; 
        return -1;
    }
    
    t->context.uc_stack.ss_sp = t->stack;
    t->context.uc_stack.ss_size = STACK_SIZE;
    t->context.uc_link = &main_context;
    
    makecontext(&t->context, thread_wrapper, 0);
    
    t->state = THREAD_READY;
    *thread = tid;
    
    return 0;
}

void uthread_sleep(int seconds) {
    if (current_thread == -1) return;
    
    thread_t *t = &threads[current_thread];
    t->state = THREAD_SLEEPING;
    t->wake_time = time(NULL) + seconds;
    
    uthread_yield();
}

#define UTHREAD_SLEEP_CHECK 10000

static void uthread_wait_for_wakeup(void) {
    while (1) {
        time_t now = time(NULL);
        for (int i = 0; i < thread_count; i++) {
            if (threads[i].state == THREAD_SLEEPING &&
                threads[i].wake_time <= now) {
                threads[i].state = THREAD_READY;
                return;
            }
        }
        usleep(UTHREAD_SLEEP_CHECK);
    }
}


void uthread_yield(void) {
    if (!scheduler_running || current_thread == -1) {
        return;
    }
    
    time_t now = time(NULL);
    for (int i = 0; i < thread_count; i++) {
        if (threads[i].state == THREAD_SLEEPING && threads[i].wake_time <= now) {
            threads[i].state = THREAD_READY;
        }
    }

    int old_thread = current_thread;
    thread_t *old = &threads[old_thread];
    
    int next = -1;
    while (next == -1) {
        for (int i = 0; i < thread_count; i++) {
            int idx = (old_thread + 1 + i) % thread_count;
            if (threads[idx].state == THREAD_READY || 
                (threads[idx].state == THREAD_RUNNING && idx != old_thread)) {
                next = idx;
                break;
            }
        }

        if (next != -1) break;

        int sleeping = 0;
        for (int i = 0; i < thread_count; i++) {
            if (threads[i].state == THREAD_SLEEPING) {
                sleeping = 1;
                break;
            }
        }

        if (!sleeping) break;

        uthread_wait_for_wakeup();
    }
    
    if (next == -1) {
        if (old->state == THREAD_FINISHED) {
            current_thread = -1;
            setcontext(&main_context);
        }
        return;
    }
    
    current_thread = next;
    threads[next].state = THREAD_RUNNING;
    
    if (old->state == THREAD_RUNNING) {
        old->state = THREAD_READY;
    }
    
    if (old->state == THREAD_FINISHED) {
        setcontext(&threads[next].context);
    } else {
        swapcontext(&old->context, &threads[next].context);
    }
}

int uthread_join(uthread_t thread, void **retval) {
    if (thread < 0 || thread >= thread_count) {
        errno = EINVAL;
        return -1;
    }
    
    thread_t *t = &threads[thread];
    
    if (t->joined) {
        errno = EINVAL;
        return -1;
    }
    
    t->joined = 1;
    
    while (t->state != THREAD_FINISHED) {
        uthread_yield();
    }
    
    if (retval) {
        *retval = t->retval;
    }
    
    free(t->stack);
    
    t->stack = NULL;
    free_ids[free_top++] = thread;
    
    return 0;
}

void uthread_run(void) {
    scheduler_running = 1;
    
    if (getcontext(&main_context) == -1) {
        perror("getcontext failed");
        return;
    }
    
    int has_ready = 0;
    for (int i = 0; i < thread_count; i++) {
        if (threads[i].state == THREAD_READY) {
            has_ready = 1;
            break;
        }
    }
    
    if (!has_ready) {
        scheduler_running = 0;
        return;
    }
    
    for (int i = 0; i < thread_count; i++) {
        if (threads[i].state == THREAD_READY) {
            current_thread = i;
            threads[i].state = THREAD_RUNNING;
            setcontext(&threads[i].context);
            break;
        }
    }
    
    scheduler_running = 0;
}
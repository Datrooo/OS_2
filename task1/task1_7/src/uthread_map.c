#define _GNU_SOURCE
#include "uthread_map.h"
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdatomic.h>

typedef struct mapped_thread {
    int used;
    int worker_id;
    int finished;
    int joined;
    void *retval;

    pthread_mutex_t mutex;
    pthread_cond_t  cond;
} mapped_thread_t;

typedef struct worker_task {
    int global_id;
    void *(*start_routine)(void *);
    void *arg;

    struct worker_task *next;
} worker_task_t;

typedef struct worker {
    pthread_t thread;
    pthread_mutex_t mutex;
    pthread_cond_t cond;

    worker_task_t *queue_head;
    worker_task_t *queue_tail;

    int stop;
} worker_t;

static worker_t workers[UTHREAD_WORKER_COUNT];
static mapped_thread_t mapped_threads[MAX_THREADS];

static pthread_mutex_t global_map_mutex = PTHREAD_MUTEX_INITIALIZER;
static atomic_int next_worker = 0;

static int map_initialized = 0;

static int init_mapped_entry(int idx, int worker_id) {
    mapped_thread_t *mt = &mapped_threads[idx];

    if (!mt->used) {
        mt->used = 1;
        mt->worker_id = worker_id;
        mt->finished = 0;
        mt->joined = 0;
        mt->retval = NULL;

        int err;
        err = pthread_mutex_init(&mt->mutex, NULL);
        if (err != 0) {
            errno = err;
            mt->used = 0;
            return -1;
        }
        err = pthread_cond_init(&mt->cond, NULL);
        if (err != 0) {
            errno = err;
            pthread_mutex_destroy(&mt->mutex);
            mt->used = 0;
            return -1;
        }
    } else {
        // Можно расширить логикой реюза при необходимости.
        errno = EBUSY;
        return -1;
    }

    return 0;
}


static void worker_enqueue_task(worker_t *w, worker_task_t *task) {
    task->next = NULL;
    if (w->queue_tail == NULL) {
        w->queue_head = w->queue_tail = task;
    } else {
        w->queue_tail->next = task;
        w->queue_tail = task;
    }
}

static worker_task_t *worker_dequeue_task(worker_t *w) {
    worker_task_t *t = w->queue_head;
    if (t) {
        w->queue_head = t->next;
        if (w->queue_head == NULL) {
            w->queue_tail = NULL;
        }
    }
    return t;
}

static void *worker_main(void *arg) {
    int worker_id = (int)(long)arg;
    worker_t *w = &workers[worker_id];

    while(1) {
        pthread_mutex_lock(&w->mutex);
        while (!w->stop && w->queue_head == NULL) {
            pthread_cond_wait(&w->cond, &w->mutex);
        }

        if (w->stop && w->queue_head == NULL) {
            pthread_mutex_unlock(&w->mutex);
            break;
        }

        worker_task_t *task = worker_dequeue_task(w);
        pthread_mutex_unlock(&w->mutex);

        if (!task) {
            continue;
        }

        int global_id = task->global_id;
        void *(*start_routine)(void *) = task->start_routine;
        void *arg_fn = task->arg;

        uthread_t local_id;
        int rc = uthread_create(&local_id, start_routine, arg_fn);
        if (rc != 0) {
            mapped_thread_t *mt = &mapped_threads[global_id];
            pthread_mutex_lock(&mt->mutex);
            mt->retval = NULL;
            mt->finished = 1;
            pthread_cond_broadcast(&mt->cond);
            pthread_mutex_unlock(&mt->mutex);

            free(task);
            continue;
        }

        uthread_run();

        void *res = NULL;
        rc = uthread_join(local_id, &res);
        if (rc != 0) {
            res = NULL;
        }

        mapped_thread_t *mt = &mapped_threads[global_id];
        pthread_mutex_lock(&mt->mutex);
        mt->retval = res;
        mt->finished = 1;
        pthread_cond_broadcast(&mt->cond);
        pthread_mutex_unlock(&mt->mutex);

        free(task);
    }

    return NULL;
}

int uthread_map_init(void) {
    if (map_initialized) {
        return 0;
    }

    memset(mapped_threads, 0, sizeof(mapped_threads));

    for (int i = 0; i < UTHREAD_WORKER_COUNT; ++i) {
        worker_t *w = &workers[i];
        w->queue_head = w->queue_tail = NULL;
        w->stop = 0;

        int err;
        err = pthread_mutex_init(&w->mutex, NULL);
        if (err != 0) {
            errno = err;
            return -1;
        }
        err = pthread_cond_init(&w->cond, NULL);
        if (err != 0) {
            errno = err;
            return -1;
        }

        err = pthread_create(&w->thread, NULL, worker_main, (void *)(long)i);
        if (err != 0) {
            errno = err;
            return -1;
        }
    }

    atomic_store(&next_worker, 0);
    map_initialized = 1;
    return 0;
}

int uthread_map_create(uthread_t *thread,
                       void *(*start_routine)(void *),
                       void *arg) {
    if (!map_initialized) {
        errno = EPERM;
        return -1;
    }

    if (thread == NULL || start_routine == NULL) {
        errno = EINVAL;
        return -1;
    }

    pthread_mutex_lock(&global_map_mutex);
    int idx = -1;
    for (int i = 0; i < MAX_THREADS; ++i) {
        if (!mapped_threads[i].used) {
            idx = i;
            break;
        }
    }
    if (idx == -1) {
        pthread_mutex_unlock(&global_map_mutex);
        errno = EAGAIN;
        return -1;
    }

    int worker_id = atomic_fetch_add(&next_worker, 1) % UTHREAD_WORKER_COUNT;

    if (init_mapped_entry(idx, worker_id) == -1) {
        pthread_mutex_unlock(&global_map_mutex);
        return -1;
    }

    *thread = idx;
    pthread_mutex_unlock(&global_map_mutex);

    worker_task_t *task = malloc(sizeof(worker_task_t));
    if (!task) {
        errno = ENOMEM;
        return -1;
    }
    task->global_id = idx;
    task->start_routine = start_routine;
    task->arg = arg;
    task->next = NULL;

    worker_t *w = &workers[worker_id];
    pthread_mutex_lock(&w->mutex);
    worker_enqueue_task(w, task);
    pthread_cond_signal(&w->cond);
    pthread_mutex_unlock(&w->mutex);

    return 0;
}

int uthread_map_join(uthread_t thread, void **retval) {
    if (!map_initialized) {
        errno = EPERM;
        return -1;
    }

    if (thread < 0 || thread >= MAX_THREADS) {
        errno = EINVAL;
        return -1;
    }

    mapped_thread_t *mt = &mapped_threads[thread];
    if (!mt->used) {
        errno = ESRCH;
        return -1;
    }

    pthread_mutex_lock(&mt->mutex);

    if (mt->joined) {
        pthread_mutex_unlock(&mt->mutex);
        errno = EINVAL;
        return -1;
    }

    mt->joined = 1;

    while (!mt->finished) {
        pthread_cond_wait(&mt->cond, &mt->mutex);
    }

    void *res = mt->retval;

    pthread_mutex_unlock(&mt->mutex);

    if (retval) {
        *retval = res;
    }

    return 0;
}

void uthread_map_shutdown(void) {
    if (!map_initialized) {
        return;
    }

    for (int i = 0; i < UTHREAD_WORKER_COUNT; ++i) {
        worker_t *w = &workers[i];
        pthread_mutex_lock(&w->mutex);
        w->stop = 1;
        pthread_cond_broadcast(&w->cond);
        pthread_mutex_unlock(&w->mutex);
    }

    for (int i = 0; i < UTHREAD_WORKER_COUNT; ++i) {
        pthread_join(workers[i].thread, NULL);
        pthread_mutex_destroy(&workers[i].mutex);
        pthread_cond_destroy(&workers[i].cond);
    }

    map_initialized = 0;
}


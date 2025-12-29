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

typedef struct {
    uthread_t local_id;
    int global_id;
} local_map_t;


static worker_t *workers = NULL;
static int workers_count = 0;

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

        worker_task_t *batch = w->queue_head;
        w->queue_head = NULL;
        w->queue_tail = NULL;
        pthread_mutex_unlock(&w->mutex);

        local_map_t locals[MAX_THREADS];
        int nlocals = 0;

        for (worker_task_t *t = batch; t != NULL; ) {
            worker_task_t *next = t->next;

            uthread_t local_id;
            int rc = uthread_create(&local_id, t->start_routine, t->arg);
            if (rc != 0) {
                mapped_thread_t *mt = &mapped_threads[t->global_id];
                pthread_mutex_lock(&mt->mutex);
                mt->retval = NULL;
                mt->finished = 1;
                pthread_cond_broadcast(&mt->cond);
                pthread_mutex_unlock(&mt->mutex);

                free(t);
                t = next;
                continue;
            }

            locals[nlocals].local_id = local_id;
            locals[nlocals].global_id = t->global_id;
            nlocals++;

            free(t);
            t = next;
        }

        if (nlocals > 0) {
            uthread_run();
        }

        for (int i = 0; i < nlocals; ++i) {
            void *res = NULL;
            int rc = uthread_join(locals[i].local_id, &res);
            if (rc != 0) {
                res = NULL;
            }

            mapped_thread_t *mt = &mapped_threads[locals[i].global_id];
            pthread_mutex_lock(&mt->mutex);
            mt->retval = res;
            mt->finished = 1;
            pthread_cond_broadcast(&mt->cond);
            pthread_mutex_unlock(&mt->mutex);
        }
    }

    return NULL;
}

int uthread_map_init(int worker_count) {
    if (map_initialized) {
        return 0;
    }

    if (worker_count <= 0 || worker_count > UTHREAD_WORKERS_MAX) {
        errno = EINVAL;
        return -1;
    }

    workers = calloc((size_t)worker_count, sizeof(*workers));
    if (workers == NULL) {
        errno = ENOMEM;
        return -1;
    }
    workers_count = worker_count;

    int created = 0;
    int failed = 0;
    int saved_errno = 0;

    for (int i = 0; i < workers_count; ++i) {
        int rc = 0;

        workers[i].queue_head = NULL;
        workers[i].queue_tail = NULL;
        workers[i].stop = 0;

        rc = pthread_mutex_init(&workers[i].mutex, NULL);
        if (rc != 0) {
            failed = 1;
            saved_errno = rc;
            break;
        }

        rc = pthread_cond_init(&workers[i].cond, NULL);
        if (rc != 0) {
            failed = 1;
            saved_errno = rc;
            pthread_mutex_destroy(&workers[i].mutex);
            break;
        }

        rc = pthread_create(&workers[i].thread, NULL, worker_main, (void *)(long)i);
        if (rc != 0) {
            failed = 1;
            saved_errno = rc;
            pthread_cond_destroy(&workers[i].cond);
            pthread_mutex_destroy(&workers[i].mutex);
            break;
        }

        created++;
    }

    if (!failed) {
        map_initialized = 1;
        return 0;
    }

    for (int j = 0; j < created; ++j) {
        pthread_mutex_lock(&workers[j].mutex);
        workers[j].stop = 1;
        pthread_cond_broadcast(&workers[j].cond);
        pthread_mutex_unlock(&workers[j].mutex);
    }

    for (int j = 0; j < created; ++j) {
        pthread_join(workers[j].thread, NULL);
        worker_task_t *t = workers[j].queue_head;
        while (t) {
            worker_task_t *n = t->next;
            free(t);
            t = n;
        }
        workers[j].queue_head = workers[j].queue_tail = NULL;
        pthread_cond_destroy(&workers[j].cond);
        pthread_mutex_destroy(&workers[j].mutex);
    }

    free(workers);
    workers = NULL;
    workers_count = 0;
    map_initialized = 0;

    errno = saved_errno ? saved_errno : EFAULT;
    return -1;
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

    int worker_id = atomic_fetch_add(&next_worker, 1) % workers_count;

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

    for (int i = 0; i < workers_count; ++i) {
        worker_t *w = &workers[i];
        pthread_mutex_lock(&w->mutex);
        w->stop = 1;
        pthread_cond_broadcast(&w->cond);
        pthread_mutex_unlock(&w->mutex);
    }

    for (int i = 0; i < workers_count; ++i) {
        pthread_join(workers[i].thread, NULL);
        pthread_mutex_destroy(&workers[i].mutex);
        pthread_cond_destroy(&workers[i].cond);

        worker_task_t *t = workers[i].queue_head;
        while (t) {
            worker_task_t *n = t->next;
            free(t);
            t = n;
        }
        workers[i].queue_head = workers[i].queue_tail = NULL;
    }

    map_initialized = 0;
    free(workers);
    workers = NULL;
    workers_count = 0;
}


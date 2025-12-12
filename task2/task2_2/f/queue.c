// #define _GNU_SOURCE
// #include <pthread.h>
// #include <assert.h>
// #include <stdio.h>
// #include <stdlib.h>
// #include <string.h>
// #include <errno.h>

// #include "queue.h"

// void *qmonitor(void *arg) {
//     queue_t *q = (queue_t *)arg;
//     int err;
//     printf("qmonitor: [%d %d %d]\n", getpid(), getppid(), gettid());

//     while (1) {
//         err = pthread_mutex_lock(&q->lock);
//         if (err != SUCCESS) {
//             printf("qmonitor: pthread_mutex_lock failed: %s\n", strerror(err));
//             continue;
//         }
//         queue_print_stats(q);
//         err = pthread_mutex_unlock(&q->lock);
//         if (err != SUCCESS)
//             printf("qmonitor: pthread_mutex_unlock failed: %s\n", strerror(err));
//         sleep(MONITOR_INTERVAL);
//     }

//     return NULL;
// }

// queue_t* queue_init(int max_count) {
//     int err;
//     queue_t *q = malloc(sizeof(queue_t));
//     if (q == NULL) {
//         printf("Cannot allocate memory for a queue\n");
//         abort();
//     }

//     q->first = NULL;
//     q->last = NULL;
//     q->max_count = max_count;
//     q->count = 0;

//     q->add_attempts = q->get_attempts = 0;
//     q->add_count = q->get_count = 0;

//     err = pthread_mutex_init(&q->lock, NULL);
//     if (err != SUCCESS) {
//         printf("queue_init: pthread_mutex_init failed: %s\n", strerror(err));
//         free(q);
//         abort();
//     }

//     err = pthread_cond_init(&q->cond, NULL);
//     if (err != SUCCESS) {
//         printf("queue_init: pthread_cond_init failed: %s\n", strerror(err));
//         err = pthread_mutex_destroy(&q->lock);
//         if (err != SUCCESS)
//             printf("queue_init: pthread_mutex_destroy failed: %s\n", strerror(err));
//         free(q);
//         abort();
//     }

//     err = pthread_create(&q->qmonitor_tid, NULL, qmonitor, q);
//     if (err != SUCCESS) {
//         printf("queue_init: pthread_create() failed: %s\n", strerror(err));
//         err = pthread_cond_destroy(&q->cond);
//         if (err != SUCCESS)
//             printf("queue_init: pthread_cond_destroy failed: %s\n", strerror(err));
//         err = pthread_mutex_destroy(&q->lock);
//         if (err != SUCCESS)
//             printf("queue_init: pthread_mutex_destroy failed: %s\n", strerror(err));
//         free(q);
//         abort();
//     }

//     return q;
// }

// void queue_destroy(queue_t *q) {
//     if (q == NULL) 
//         return;
//     int err;
//     err = pthread_cancel(q->qmonitor_tid);
//     if (err != SUCCESS)
//         printf("queue_destroy: pthread_cancel() failed: %s\n", strerror(err)); 
//     err = pthread_join(q->qmonitor_tid, NULL);
//     if (err != SUCCESS)
//         printf("queue_destroy: pthread_join() failed: %s\n", strerror(err));
    
//     err = pthread_cond_destroy(&q->cond);
//     if (err != SUCCESS)
//         printf("queue_destroy: pthread_cond_destroy() failed: %s\n", strerror(err));
    
//     err = pthread_mutex_destroy(&q->lock);
//     if (err != SUCCESS)
//         printf("queue_destroy: pthread_mutex_destroy() failed: %s\n", strerror(err));
//     qnode_t *current = q->first;
//     while (current != NULL) {
//         qnode_t *temp = current;
//         current = current->next;
//         free(temp);
//     }
//     free(q);
// }

// int queue_add(queue_t *q, int val) {
//     if (q == NULL)
//         return QUEUE_OP_FAILURE;

//     int err;

//     err = pthread_mutex_lock(&q->lock);
//     if (err != SUCCESS) {
//         printf("queue_add: pthread_mutex_lock failed: %s\n", strerror(err));
//         return QUEUE_OP_FAILURE;
//     }

//     q->add_attempts++;

//     err = pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, NULL);
//     if (err != SUCCESS) {
//         printf("queue_add: pthread_setcancelstate failed: %s\n", strerror(err));
//         err = pthread_mutex_unlock(&q->lock);
//         if (err != SUCCESS)
//             printf("queue_add: pthread_mutex_unlock failed: %s\n", strerror(err));
//         return QUEUE_OP_FAILURE;
//     }

//     while (q->count == q->max_count) {
//         err = pthread_cond_wait(&q->cond, &q->lock);
//         if (err != SUCCESS) {
//             printf("queue_add: pthread_cond_wait failed: %s\n", strerror(err));
//             pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);
//             err = pthread_mutex_unlock(&q->lock);
//             if (err != SUCCESS)
//                 printf("queue_add: pthread_mutex_unlock failed: %s\n", strerror(err));
//             return QUEUE_OP_FAILURE;
//         }
//     }

//     qnode_t *new = malloc(sizeof(qnode_t));
//     if (new == NULL) {
//         printf("Cannot allocate memory for new node\n");
//         pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);
//         err = pthread_mutex_unlock(&q->lock);
//         if (err != SUCCESS)
//             printf("queue_add: pthread_mutex_unlock failed: %s\n", strerror(err));
//         return QUEUE_OP_FAILURE;
//     }

//     new->val  = val;
//     new->next = NULL;

//     if (q->first == NULL)
//         q->first = q->last = new;
//     else {
//         q->last->next = new;
//         q->last = new;
//     }

//     q->count++;
//     q->add_count++;

//     pthread_cond_broadcast(&q->cond);

//     err = pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);
//     if (err != SUCCESS)
//         printf("queue_add: pthread_setcancelstate (ENABLE) failed: %s\n", strerror(err));

//     err = pthread_mutex_unlock(&q->lock);
//     if (err != SUCCESS)
//         printf("queue_add: pthread_mutex_unlock failed: %s\n", strerror(err));

//     return QUEUE_OP_SUCCESS;
// }


// int queue_get(queue_t *q, int *val) {
//     int err;
//     q->get_attempts++;
//     err = pthread_mutex_lock(&q->lock);
//     if (err != SUCCESS) {
//         printf("queue_get: pthread_mutex_lock failed: %s\n", strerror(err));
//         return QUEUE_OP_FAILURE;
//     }
//     err = pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, NULL);
//     if (err != SUCCESS) {
//         printf("queue_get: pthread_setcancelstate failed: %s\n", strerror(err));
//         err = pthread_mutex_unlock(&q->lock);
//         if (err != SUCCESS)
//             printf("queue_get: pthread_mutex_unlock failed: %s\n", strerror(err));
//         return QUEUE_OP_FAILURE;
//     }
//     while (q->count == 0) {
//         pthread_cond_wait(&q->cond, &q->lock);
//     }
//     err = pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);
//     if (err != SUCCESS) {
//         printf("queue_get: pthread_setcancelstate failed: %s\n", strerror(err));
//         err = pthread_mutex_unlock(&q->lock);
//         if (err != SUCCESS)
//             printf("queue_get: pthread_mutex_unlock failed: %s\n", strerror(err));
//         return QUEUE_OP_FAILURE;
//     }
//     qnode_t *tmp = q->first;
//     *val = tmp->val;
//     q->first = q->first->next;
//     if (q->first == NULL)
//         q->last = NULL;
//     free(tmp);
//     q->count--;
//     q->get_count++;
    
//     pthread_cond_broadcast(&q->cond);
//     err = pthread_mutex_unlock(&q->lock);
//     if (err != SUCCESS)
//         printf("queue_get: pthread_mutex_unlock failed: %s\n", strerror(err));
//     return QUEUE_OP_SUCCESS;
// }

// void queue_print_stats(queue_t *q) {
//     printf("queue stats: current size %d; attempts: (%ld %ld %ld); counts (%ld %ld %ld)\n",
//         q->count,
//         q->add_attempts, q->get_attempts, q->add_attempts - q->get_attempts,
//         q->add_count, q->get_count, q->add_count - q->get_count);
// }


/*
	version with 2 cond vars
*/

#define _GNU_SOURCE
#include <pthread.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include "queue.h"

void *qmonitor(void *arg) {
    queue_t *q = (queue_t *)arg;
    int err;
    printf("qmonitor: [%d %d %d]\n", getpid(), getppid(), gettid());

    err = pthread_mutex_lock(&q->lock);
    if (err != SUCCESS) {
        printf("qmonitor: pthread_mutex_lock failed: %s\n", strerror(err));
        return NULL;
    }

    time_t last_print = 0;

    while (1) {
        pthread_testcancel();
        struct timespec ts;
        clock_gettime(CLOCK_REALTIME, &ts);
        ts.tv_sec += MONITOR_INTERVAL;

        err = pthread_cond_timedwait(&q->cond_monitor, &q->lock, &ts);
        if (err != SUCCESS && err != ETIMEDOUT) {
            printf("qmonitor: pthread_mutex_lock failed: %s\n", strerror(err));
            pthread_mutex_unlock(&q->lock);
            return NULL;
        }
       
        time_t now = time(NULL);
        if (now - last_print >= MONITOR_INTERVAL) {
            queue_print_stats(q);
            last_print = now;
        }
    }
    
    err = pthread_mutex_unlock(&q->lock);
    if (err != SUCCESS)
        printf("qmonitor: pthread_mutex_unlock failed: %s\n", strerror(err));
    return NULL;
}

queue_t* queue_init(int max_count) {
    int err;
    queue_t *q = malloc(sizeof(queue_t));
    if (q == NULL) {
        printf("Cannot allocate memory for a queue\n");
        abort();
    }

    q->first = NULL;
    q->last = NULL;
    q->max_count = max_count;
    q->count = 0;

    q->add_attempts = q->get_attempts = 0;
    q->add_count = q->get_count = 0;

    err = pthread_mutex_init(&q->lock, NULL);
    if (err != SUCCESS) {
        printf("queue_init: pthread_mutex_init failed: %s\n", strerror(err));
        free(q);
        abort();
    }

    err = pthread_cond_init(&q->cond, NULL);
    if (err != SUCCESS) {
        printf("queue_init: pthread_cond_init failed: %s\n", strerror(err));
        err = pthread_mutex_destroy(&q->lock);
        if (err != SUCCESS)
            printf("queue_init: pthread_mutex_destroy failed: %s\n", strerror(err));
        free(q);
        abort();
    }

    err = pthread_cond_init(&q->cond_monitor, NULL);
    if (err != SUCCESS) {
        printf("queue_init: pthread_cond_init failed: %s\n", strerror(err));
        err = pthread_cond_destroy(&q->cond);
        if (err != SUCCESS)
            printf("queue_init: pthread_cond_destroy failed: %s\n", strerror(err));
        err = pthread_mutex_destroy(&q->lock);
        if (err != SUCCESS)
            printf("queue_init: pthread_mutex_destroy failed: %s\n", strerror(err));
        free(q);
        abort();
    }

    err = pthread_create(&q->qmonitor_tid, NULL, qmonitor, q);
    if (err != SUCCESS) {
        printf("queue_init: pthread_create() failed: %s\n", strerror(err));
        err = pthread_cond_destroy(&q->cond);
        if (err != SUCCESS)
            printf("queue_init: pthread_cond_destroy failed: %s\n", strerror(err));
        err = pthread_cond_destroy(&q->cond_monitor);
        if (err != SUCCESS)
            printf("queue_init: pthread_cond_destroy failed: %s\n", strerror(err));
        err = pthread_mutex_destroy(&q->lock);
        if (err != SUCCESS)
            printf("queue_init: pthread_mutex_destroy failed: %s\n", strerror(err));
        free(q);
        abort();
    }

    return q;
}

void queue_destroy(queue_t *q) {
    if (q == NULL) 
        return;
    int err;
    err = pthread_cancel(q->qmonitor_tid);
    if (err != SUCCESS)
        printf("queue_destroy: pthread_cancel() failed: %s\n", strerror(err)); 
    err = pthread_join(q->qmonitor_tid, NULL);
    if (err != SUCCESS)
        printf("queue_destroy: pthread_join() failed: %s\n", strerror(err));
    
    err = pthread_cond_destroy(&q->cond);
    if (err != SUCCESS)
        printf("queue_destroy: pthread_cond_destroy() failed: %s\n", strerror(err));
    err = pthread_cond_destroy(&q->cond_monitor);
    if (err != SUCCESS)
        printf("queue_init: pthread_cond_destroy failed: %s\n", strerror(err));
    
    err = pthread_mutex_destroy(&q->lock);
    if (err != SUCCESS)
        printf("queue_destroy: pthread_mutex_destroy() failed: %s\n", strerror(err));
    qnode_t *current = q->first;
    while (current != NULL) {
        qnode_t *temp = current;
        current = current->next;
        free(temp);
    }
    free(q);
}

int queue_add(queue_t *q, int val) {
    if (q == NULL)
        return QUEUE_OP_FAILURE;

    int err;

    err = pthread_mutex_lock(&q->lock);
    if (err != SUCCESS) {
        printf("queue_add: pthread_mutex_lock failed: %s\n", strerror(err));
        return QUEUE_OP_FAILURE;
    }

    q->add_attempts++;

    err = pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, NULL);
    if (err != SUCCESS) {
        printf("queue_add: pthread_setcancelstate failed: %s\n", strerror(err));
        err = pthread_mutex_unlock(&q->lock);
        if (err != SUCCESS)
            printf("queue_add: pthread_mutex_unlock failed: %s\n", strerror(err));
        return QUEUE_OP_FAILURE;
    }

    while (q->count == q->max_count) {
        err = pthread_cond_wait(&q->cond, &q->lock);
        if (err != SUCCESS) {
            printf("queue_add: pthread_cond_wait failed: %s\n", strerror(err));
            pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);
            err = pthread_mutex_unlock(&q->lock);
            if (err != SUCCESS)
                printf("queue_add: pthread_mutex_unlock failed: %s\n", strerror(err));
            return QUEUE_OP_FAILURE;
        }
    }

    qnode_t *new = malloc(sizeof(qnode_t));
    if (new == NULL) {
        printf("Cannot allocate memory for new node\n");
        pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);
        err = pthread_mutex_unlock(&q->lock);
        if (err != SUCCESS)
            printf("queue_add: pthread_mutex_unlock failed: %s\n", strerror(err));
        return QUEUE_OP_FAILURE;
    }

    new->val  = val;
    new->next = NULL;

    if (q->first == NULL)
        q->first = q->last = new;
    else {
        q->last->next = new;
        q->last = new;
    }

    q->count++;
    q->add_count++;

    pthread_cond_broadcast(&q->cond);
    pthread_cond_signal(&q->cond_monitor);

    err = pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);
    if (err != SUCCESS)
        printf("queue_add: pthread_setcancelstate (ENABLE) failed: %s\n", strerror(err));

    err = pthread_mutex_unlock(&q->lock);
    if (err != SUCCESS)
        printf("queue_add: pthread_mutex_unlock failed: %s\n", strerror(err));

    return QUEUE_OP_SUCCESS;
}


int queue_get(queue_t *q, int *val) {
    int err;
    q->get_attempts++;
    err = pthread_mutex_lock(&q->lock);
    if (err != SUCCESS) {
        printf("queue_get: pthread_mutex_lock failed: %s\n", strerror(err));
        return QUEUE_OP_FAILURE;
    }
    err = pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, NULL);
    if (err != SUCCESS) {
        printf("queue_get: pthread_setcancelstate failed: %s\n", strerror(err));
        err = pthread_mutex_unlock(&q->lock);
        if (err != SUCCESS)
            printf("queue_get: pthread_mutex_unlock failed: %s\n", strerror(err));
        return QUEUE_OP_FAILURE;
    }
    while (q->count == 0) {
        pthread_cond_wait(&q->cond, &q->lock);
    }
    err = pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);
    if (err != SUCCESS) {
        printf("queue_get: pthread_setcancelstate failed: %s\n", strerror(err));
        err = pthread_mutex_unlock(&q->lock);
        if (err != SUCCESS)
            printf("queue_get: pthread_mutex_unlock failed: %s\n", strerror(err));
        return QUEUE_OP_FAILURE;
    }
    qnode_t *tmp = q->first;
    *val = tmp->val;
    q->first = q->first->next;
    if (q->first == NULL)
        q->last = NULL;
    free(tmp);
    q->count--;
    q->get_count++;
    
    pthread_cond_broadcast(&q->cond);
    pthread_cond_signal(&q->cond_monitor);

    err = pthread_mutex_unlock(&q->lock);
    if (err != SUCCESS)
        printf("queue_get: pthread_mutex_unlock failed: %s\n", strerror(err));
    return QUEUE_OP_SUCCESS;
}

void queue_print_stats(queue_t *q) {
    printf("queue stats: current size %d; attempts: (%ld %ld %ld); counts (%ld %ld %ld)\n",
        q->count,
        q->add_attempts, q->get_attempts, q->add_attempts - q->get_attempts,
        q->add_count, q->get_count, q->add_count - q->get_count);
}
#define _GNU_SOURCE
#include "queue.h"
#include <pthread.h>
#include <sys/syscall.h>


void *qmonitor(void *arg)
{
    queue_t *q = (queue_t *)arg;
	int err;
	printf("qmonitor: [%d %d %d]\n", getpid(), getppid(), gettid());

	while (1) {
		err = pthread_mutex_lock(&q->lock);
		if (err != SUCCESS) {
			printf("qmonitor: pthread_mutex_lock failed: %s\n", strerror(err));
			continue;
		}
        queue_print_stats(q);
        err = pthread_mutex_unlock(&q->lock);
		if (err != SUCCESS)
			printf("qmonitor: pthread_mutex_unlock failed: %s\n", strerror(err));
		sleep(MONITOR_INTERVAL);
	}

	return NULL;
}

queue_t *queue_init(int max_count)
{
    queue_t *q = (queue_t *)malloc(sizeof(queue_t));
    if (!q) {
        perror("queue_init: malloc");
        return NULL;
    }

    q->first = NULL;
    q->last  = NULL;

    q->count     = 0;
    q->max_count = max_count;

    q->add_attempts = 0;
    q->get_attempts = 0;
    q->add_count    = 0;
    q->get_count    = 0;

    if (pthread_mutex_init(&q->lock, NULL) != 0) {
        perror("queue_init: pthread_mutex_init");
        free(q);
        return NULL;
    }

    if (pthread_cond_init(&q->cond, NULL) != 0) {
        perror("queue_init: pthread_cond_init");
        pthread_mutex_destroy(&q->lock);
        free(q);
        return NULL;
    }

    if (pthread_cond_init(&q->cond_monitor, NULL) != 0) {
        perror("queue_init: pthread_cond_init");
        pthread_cond_destroy(&q->cond);
        pthread_mutex_destroy(&q->lock);
        
        free(q);
        return NULL;
    }

    int err = pthread_create(&q->qmonitor_tid, NULL, qmonitor, q);
    if (err != SUCCESS) {
        fprintf(stderr, "queue_init: pthread_create(qmonitor) failed: %s\n",
                strerror(err));
        pthread_cond_destroy(&q->cond);
        pthread_cond_destroy(&q->cond_monitor);
        pthread_mutex_destroy(&q->lock);
        free(q);
        return NULL;
    }

    return q;
}

void queue_destroy(queue_t *q)
{
    if (!q) return;

    int err = pthread_cancel(q->qmonitor_tid);
    if (err != SUCCESS) {
        fprintf(stderr, "queue_destroy: pthread_cancel(qmonitor) failed: %s\n",
                strerror(err));
    }

    err = pthread_join(q->qmonitor_tid, NULL);
    if (err != SUCCESS) {
        fprintf(stderr, "queue_destroy: pthread_join(qmonitor) failed: %s\n",
                strerror(err));
    }

    pthread_mutex_lock(&q->lock);
    qnode_t *cur = q->first;
    while (cur) {
        qnode_t *tmp = cur;
        cur = cur->next;
        free(tmp);
    }
    q->first = q->last = NULL;
    q->count = 0;
    pthread_mutex_unlock(&q->lock);

    pthread_cond_destroy(&q->cond);
    pthread_cond_destroy(&q->cond_monitor);
    pthread_mutex_destroy(&q->lock);
    free(q);
}

int queue_add(queue_t *q, int val)
{
    if (!q) return QUEUE_OP_FAILURE;

    int old_state;
    pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, &old_state);

    pthread_mutex_lock(&q->lock);

    q->add_attempts++;

    if (q->count >= q->max_count) {
        pthread_mutex_unlock(&q->lock);
        pthread_setcancelstate(old_state, NULL);
        return QUEUE_OP_FAILURE;
    }

    qnode_t *node = (qnode_t *)malloc(sizeof(qnode_t));
    if (!node) {
        pthread_mutex_unlock(&q->lock);
        pthread_setcancelstate(old_state, NULL);
        return QUEUE_OP_FAILURE;
    }

    node->val  = val;
    node->next = NULL;

    if (q->last) {
        q->last->next = node;
    } else {
        q->first = node;
    }
    q->last = node;

    q->count++;
    q->add_count++;

    pthread_cond_signal(&q->cond);
    pthread_cond_signal(&q->cond_monitor);

    pthread_mutex_unlock(&q->lock);
    pthread_setcancelstate(old_state, NULL);

    return QUEUE_OP_SUCCESS;
}

int queue_get(queue_t *q, int *val)
{
    if (!q || !val) return QUEUE_OP_FAILURE;

    int old_state;
    pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, &old_state);

    pthread_mutex_lock(&q->lock);

    q->get_attempts++;

    if (q->count == 0) {
        pthread_mutex_unlock(&q->lock);
        pthread_setcancelstate(old_state, NULL);
        return QUEUE_OP_FAILURE;
    }

    qnode_t *node = q->first;
    *val = node->val;

    q->first = node->next;
    if (!q->first) {
        q->last = NULL;
    }

    q->count--;
    q->get_count++;

    free(node);


    pthread_cond_signal(&q->cond);
    pthread_cond_signal(&q->cond_monitor);

    pthread_mutex_unlock(&q->lock);
    pthread_setcancelstate(old_state, NULL);

    return QUEUE_OP_SUCCESS;
}

void queue_print_stats(queue_t *q) {
	printf("queue stats: current size %d; attempts: (%ld %ld %ld); counts (%ld %ld %ld)\n",
		q->count,
		q->add_attempts, q->get_attempts, q->add_attempts - q->get_attempts,
		q->add_count, q->get_count, q->add_count -q->get_count);
}

#define _GNU_SOURCE
#include <pthread.h>
#include <assert.h>

#include "queue.h"

void *qmonitor(void *arg) {
	queue_t *q = (queue_t *)arg;

	printf("qmonitor: [%d %d %d]\n", getpid(), getppid(), gettid());

	while (1) {
		int err;
		err = sem_wait(&q->queue_access);
		if (err != SUCCESS) {
			printf("qmonitor: sem_wait(queue_access) failed: %s\n", strerror(errno));
			break;
		}
		queue_print_stats(q);
		err = sem_post(&q->queue_access);
		if (err != SUCCESS) {
			printf("qmonitor: sem_post(queue_access) failed: %s\n", strerror(errno));
			break;
		}
		sleep(MONITOR_INTERVAL);
	}

	return NULL;
}

queue_t *queue_init(int max_count) {
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
	err = sem_init(&q->available_slots, PROCESS_PRIVATE, max_count);
	if (err != SUCCESS) {
		printf("queue_init: sem_init(available_slots) failed: %s\n", strerror(errno));
		free(q);
		abort();
	}

	err = sem_init(&q->available_items, PROCESS_PRIVATE, NO_ITEMS);
	if (err != SUCCESS) {
		printf("queue_init: sem_init(available_items) failed: %s\n", strerror(errno));
		err = sem_destroy(&q->available_slots);
		if (err != SUCCESS)
			printf("queue_init: sem_destroy(available_slots) failed: %s\n", strerror(errno));
		free(q);
		abort();
	}

	err = sem_init(&q->queue_access, PROCESS_PRIVATE, ACCESS_ALLOWED);
	if (err != SUCCESS) {
		printf("queue_init: sem_init(queue_access) failed: %s\n", strerror(errno));
		err = sem_destroy(&q->available_slots);
		if (err != SUCCESS)
			printf("queue_init: sem_destroy(available_slots) failed: %s\n", strerror(errno));
		err = sem_destroy(&q->available_items);
		if (err != SUCCESS)
			printf("queue_init: sem_destroy(available_items) failed: %s\n", strerror(errno));
		free(q);
		abort();
	}

	err = pthread_create(&q->qmonitor_tid, NULL, qmonitor, q);
	if (err != SUCCESS) {
		printf("queue_init: pthread_create() failed: %s\n", strerror(err));
		err = sem_destroy(&q->available_slots);
		if (err != SUCCESS)
			printf("queue_init: sem_destroy(available_slots) failed: %s\n", strerror(errno));
		err = sem_destroy(&q->available_items);
		if (err != SUCCESS)
			printf("queue_init: sem_destroy(available_items) failed: %s\n", strerror(errno));
		err = sem_destroy(&q->queue_access);
		if (err != SUCCESS)
			printf("queue_init: sem_destroy(queue_access) failed: %s\n", strerror(errno));
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
	err = sem_destroy(&q->available_slots);
	if (err != SUCCESS)
		printf("queue_destroy: sem_destroy(available_slots) failed: %s\n", strerror(errno));
	err = sem_destroy(&q->available_items);
	if (err != SUCCESS)
		printf("queue_destroy: sem_destroy(available_items) failed: %s\n", strerror(errno));
	err = sem_destroy(&q->queue_access);
	if (err != SUCCESS)
		printf("queue_destroy: sem_destroy(queue_access) failed: %s\n", strerror(errno));
	qnode_t *current = q->first;
	while (current != NULL) {
		qnode_t *temp = current;
		current = current->next;
		free(temp);
	}
	free(q);
}

int queue_add(queue_t *q, int val) {
    int err;
    int oldstate;

    q->add_attempts++;

    err = sem_wait(&q->available_slots);
    if (err != SUCCESS) {
        printf("queue_add: sem_wait(available_slots) failed: %s\n", strerror(errno));
        return QUEUE_OP_FAILURE;
    }

    err = pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, &oldstate);
    if (err != SUCCESS) {
        printf("queue_add: pthread_setcancelstate failed: %s\n", strerror(err));
        err = sem_post(&q->available_slots);
        if (err != SUCCESS)
            printf("queue_add: sem_post(available_slots) failed: %s\n", strerror(errno));
        return QUEUE_OP_FAILURE;
    }

    err = sem_wait(&q->queue_access);
    if (err != SUCCESS) {
        printf("queue_add: sem_wait(queue_access) failed: %s\n", strerror(errno));
        err = sem_post(&q->available_slots);
        if (err != SUCCESS)
            printf("queue_add: sem_post(available_slots) failed: %s\n", strerror(errno));
        err = pthread_setcancelstate(oldstate, NULL);
        if (err != SUCCESS)
            printf("queue_add: pthread_setcancelstate failed: %s\n", strerror(err));
        return QUEUE_OP_FAILURE;
    }

    qnode_t *new = malloc(sizeof(qnode_t));
    if (new == NULL) {
        printf("Cannot allocate memory for new node\n");
        err = sem_post(&q->queue_access);
        if (err != SUCCESS)
            printf("queue_add: sem_post(queue_access) failed: %s\n", strerror(errno));
        err = sem_post(&q->available_slots);
        if (err != SUCCESS)
            printf("queue_add: sem_post(available_slots) failed: %s\n", strerror(errno));
        err = pthread_setcancelstate(oldstate, NULL);
        if (err != SUCCESS)
            printf("queue_add: pthread_setcancelstate failed: %s\n", strerror(err));
        return QUEUE_OP_FAILURE;
    }

    new->val = val;
    new->next = NULL;

    if (q->first == NULL)
        q->first = q->last = new;
    else {
        q->last->next = new;
        q->last = new;
    }
    q->count++;
    q->add_count++;

    err = sem_post(&q->queue_access);
    if (err != SUCCESS)
        printf("queue_add: sem_post(queue_access) failed: %s\n", strerror(errno));

    err = sem_post(&q->available_items);
    if (err != SUCCESS)
        printf("queue_add: sem_post(available_items) failed: %s\n", strerror(errno));

    err = pthread_setcancelstate(oldstate, NULL);
    if (err != SUCCESS)
        printf("queue_add: pthread_setcancelstate failed: %s\n", strerror(err));

    return QUEUE_OP_SUCCESS;
}

int queue_get(queue_t *q, int *val) {
    q->get_attempts++;
    int err;
    int oldstate;

    err = sem_wait(&q->available_items);
    if (err != SUCCESS) {
        printf("queue_get: sem_wait(available_items) failed: %s\n", strerror(errno));
        return QUEUE_OP_FAILURE;
    }

    err = pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, &oldstate);
    if (err != SUCCESS) {
        printf("queue_get: pthread_setcancelstate failed: %s\n", strerror(err));
        err = sem_post(&q->available_items);
        if (err != SUCCESS)
            printf("queue_get: sem_post(available_items) failed: %s\n", strerror(errno));
        return QUEUE_OP_FAILURE;
    }

    err = sem_wait(&q->queue_access);
    if (err != SUCCESS) {
        printf("queue_get: sem_wait(queue_access) failed: %s\n", strerror(errno));
        err = sem_post(&q->available_items);
        if (err != SUCCESS)
            printf("queue_get: sem_post(available_items) failed: %s\n", strerror(errno));
        err = pthread_setcancelstate(oldstate, NULL);
        if (err != SUCCESS)
            printf("queue_get: pthread_setcancelstate failed: %s\n", strerror(err));
        return QUEUE_OP_FAILURE;
    }

    qnode_t *tmp = q->first;
    if (tmp == NULL) {
        printf("queue_get: invariants broken: available_items > 0, but queue is empty\n");
        err = sem_post(&q->queue_access);
        if (err != SUCCESS)
            printf("queue_get: sem_post(queue_access) failed: %s\n", strerror(errno));
        err = sem_post(&q->available_items);
        if (err != SUCCESS)
            printf("queue_get: sem_post(available_items) failed: %s\n", strerror(errno));
        err = pthread_setcancelstate(oldstate, NULL);
        if (err != SUCCESS)
            printf("queue_get: pthread_setcancelstate failed: %s\n", strerror(err));
        return QUEUE_OP_FAILURE;
    }

    *val = tmp->val;
    q->first = tmp->next;
    if (q->first == NULL)
        q->last = NULL;
    free(tmp);

    q->count--;
    q->get_count++;

    err = sem_post(&q->queue_access);
    if (err != SUCCESS)
        printf("queue_get: sem_post(queue_access) failed: %s\n", strerror(errno));

    err = sem_post(&q->available_slots);
    if (err != SUCCESS)
        printf("queue_get: sem_post(available_slots) failed: %s\n", strerror(errno));

    err = pthread_setcancelstate(oldstate, NULL);
    if (err != SUCCESS)
        printf("queue_get: pthread_setcancelstate failed: %s\n", strerror(err));

    return QUEUE_OP_SUCCESS;
}


void queue_print_stats(queue_t *q)
{
	printf("queue stats: current size %d; attempts: (%ld %ld %ld); counts (%ld %ld %ld)\n",
		   q->count,
		   q->add_attempts, q->get_attempts, q->add_attempts - q->get_attempts,
		   q->add_count, q->get_count, q->add_count - q->get_count);
}
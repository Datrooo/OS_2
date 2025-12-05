#ifndef __FITOS_QUEUE_H__
#define __FITOS_QUEUE_H__

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/types.h>
#include <unistd.h>
#include <semaphore.h>

#define SUCCESS 0
#define ERROR -1
#define QUEUE_OP_SUCCESS 1
#define QUEUE_OP_FAILURE 0
#define PROCESS_PRIVATE 0
#define NO_ITEMS 0 
#define ACCESS_ALLOWED 1
#define MONITOR_INTERVAL 1

typedef struct _QueueNode
{
	int val;
	struct _QueueNode *next;
} qnode_t;

typedef struct _Queue
{
	qnode_t *first;
	qnode_t *last;

	pthread_t qmonitor_tid;
	sem_t available_slots;
	sem_t available_items;
	sem_t queue_access;

	int count;
	int max_count;

	// queue statistics
	long add_attempts;
	long get_attempts;
	long add_count;
	long get_count;
} queue_t;

queue_t *queue_init(int max_count);
void queue_destroy(queue_t *q);
int queue_add(queue_t *q, int val);
int queue_get(queue_t *q, int *val);
void queue_print_stats(queue_t *q);

#endif // __FITOS_QUEUE_H__
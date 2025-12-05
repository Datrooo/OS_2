#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <string.h>
#include <errno.h>
#include <sys/types.h>
#include <unistd.h>
#include <sched.h>

#include "queue.h"

#define RED "\033[41m"
#define NOCOLOR "\033[0m"
#define QUEUE_SIZE 1000
#define SLEEP_TIME 10

#define CANCEL_AND_JOIN(thread, name) do { \
    int cancel_err = pthread_cancel(thread); \
    if (cancel_err != SUCCESS) \
        printf("main: pthread_cancel(%s) failed: %s\n", name, strerror(cancel_err)); \
    int join_err = pthread_join(thread, NULL); \
    if (join_err != SUCCESS) \
        printf("main: pthread_join(%s) failed: %s\n", name, strerror(join_err)); \
} while(0)

void set_cpu(int n) {
	int err;
	cpu_set_t cpuset;
	pthread_t tid = pthread_self();

	CPU_ZERO(&cpuset);
	CPU_SET(n, &cpuset);

	err = pthread_setaffinity_np(tid, sizeof(cpu_set_t), &cpuset);
	if (err != SUCCESS) {
		printf("set_cpu: pthread_setaffinity failed: %s\n", strerror(err));
		return;
	}

	printf("set_cpu: set cpu %d\n", n);
}

void *reader(void *arg) {
	int expected = 0;
	queue_t *q = (queue_t *)arg;
	printf("reader [%d %d %d]\n", getpid(), getppid(), gettid());

	set_cpu(0);

	while (1) {
		pthread_testcancel();
		int val = -1;
		int ok = queue_get(q, &val);
		if (ok != QUEUE_OP_SUCCESS)
			continue;

		if (expected != val)
			printf(RED"ERROR: get value is %d but expected - %d" NOCOLOR "\n", val, expected);

		expected = val + 1;
	}

	return NULL;
}

void *writer(void *arg) {
	int i = 0;
	queue_t *q = (queue_t *)arg;
	printf("writer [%d %d %d]\n", getpid(), getppid(), gettid());

	set_cpu(0);

	while (1) {
		pthread_testcancel();
		int ok = queue_add(q, i);
		if (ok != QUEUE_OP_SUCCESS) {
			//usleep(1);
			continue;
		}
		i++;
		//usleep(1);
	}

	return NULL;
}

int main() {
	pthread_t reader_tid, writer_tid;
	queue_t *q;
	int err;
	printf("main [%d %d %d]\n", getpid(), getppid(), gettid());
	q = queue_init(QUEUE_SIZE);

    err = pthread_create(&writer_tid, NULL, writer, q);
	if (err != SUCCESS) {
		CANCEL_AND_JOIN(reader_tid, "reader");
		queue_destroy(q);
		return ERROR;
	}

	err = pthread_create(&reader_tid, NULL, reader, q);
	if (err != SUCCESS) {
		printf("main: pthread_create() failed: %s\n", strerror(err));
		queue_destroy(q);
		return ERROR;
	}

	sched_yield();


	sleep(SLEEP_TIME);
	CANCEL_AND_JOIN(reader_tid, "reader");
	CANCEL_AND_JOIN(writer_tid, "writer");
	printf("main: reader and writer threads were joined\n");
	queue_destroy(q);
	printf("main: queue was destroyed, exiting...\n");
	return SUCCESS;
}
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <unistd.h>
#include <string.h>

void* mythread(void* arg) {
	(void)arg;
	printf("Thread ID: %d\n", gettid());
	sleep(10);
	//pthread_detach(pthread_self());
	return NULL;
}

int main() {
    pthread_t tid;
	int err;
	// void* thread_result;

    // pthread_attr_t attr;
	// pthread_attr_init(&attr);
	// pthread_attr_setstacksize(&attr, 16<<20);
	// pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
	while (1) {
		err = pthread_create(&tid, NULL, mythread, NULL);
		if (err) {
			printf("main: pthread_create() failed: %s\n", strerror(err));
			// pthread_attr_destroy(&attr);
			return -1;
		}
	}
	// pthread_attr_destroy(&attr);

    return 0;
}

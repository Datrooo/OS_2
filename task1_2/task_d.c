#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <unistd.h>
#include <string.h>

void* mythread(void* arg) {
	printf("Thread ID: %d\n", gettid());
	//pthread_detach(pthread_self());
	return NULL;
}

int main() {
    pthread_t tid;
	int err;
	void* thread_result;
	char flag;

    pthread_attr_t attr;
		pthread_attr_init(&attr);
		pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
		while (1) {
			err = pthread_create(&tid, NULL, mythread, NULL);
			err = pthread_create(&tid, &attr, mythread, NULL);
			if (err) {
				printf("main: pthread_create() failed: %s\n", strerror(err));
				pthread_attr_destroy(&attr);
				return -1;
			}
		}
		pthread_attr_destroy(&attr);

    return 0;
}

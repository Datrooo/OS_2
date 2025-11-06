#define _GNU_SOURCE
#include <stdio.h>
#include <pthread.h>
#include <string.h>
#include <errno.h>
#include <sys/types.h>
#include <unistd.h>

#define MAX_THREADS 5

void *mythread(void *arg) {
	//pthread_detach(pthread_self());
	sleep(10);
	printf("mythread [%d %d %d]: Hello from mythread!\n", getpid(), getppid(), gettid());
	return NULL;
}

int main() {
	pthread_t tid[MAX_THREADS];
	int err;

	printf("main [%d %d %d]: Hello from main!\n", getpid(), getppid(), gettid());

	for (size_t i = 0; i < MAX_THREADS; i++) {
		err = pthread_create(tid + i, NULL, mythread, NULL);
		if (err) {
			printf("main: pthread_create() failed: %s\n", strerror(err));
			return -1;
		}
	}

	// for (size_t i = 0; i < MAX_THREADS; i++) {
	// 	err = pthread_join(tid[i], NULL);
		
	// 	if (err) {
	// 		printf("main: pthread_join() failed: %s\n", strerror(err));
	// 		return -1;
	// 	}
	// }
	pthread_exit(NULL);
	//return 0;
}


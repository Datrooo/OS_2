#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <unistd.h>
#include <string.h>

void* thread_func(void* arg) {
    while (1) {
        printf("Thread is running...\n");
        sleep(1);
    }
    return NULL;
}

int main() {
    pthread_t thread;

    int err = pthread_create(&thread, NULL, thread_func, NULL);
	if (err) {
	    printf("main: pthread_create() failed: %s\n", strerror(err));
		return 1;
	}

    sleep(5);

    err = pthread_cancel(thread);
    if (err) {
        printf("main: pthread_cancel() failed: %s\n", strerror(err));
        return 1;
    }

    err = pthread_join(thread, NULL);
    if (err) {
        printf("main: pthread_join() failed: %s\n", strerror(err));
        return 1;
    }

    printf("Thread has been cancelled.\n");

    return 0;
}

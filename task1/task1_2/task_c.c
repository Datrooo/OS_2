#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <unistd.h>
#include <string.h>

void* mythread_string(void* arg) {
    (void)arg;
	char* string = "Hello world!\n";
	sleep(2);
	return (void*)string;
}

int main() {
    pthread_t thread;
    char* thread_result;

    int err = pthread_create(&thread, NULL, mythread_string, NULL);
    if (err) {
        printf("main: pthread_create() failed: %s\n", strerror(err));
        return 1;
    }

    err = pthread_join(thread, (void**)&thread_result);
    if (err) {
        printf("main: pthread_join() failed: %s\n", strerror(err));
        return 1;
    }

    printf("main: received string = %s\n", thread_result);

    return 0;
}

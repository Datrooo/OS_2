#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <unistd.h>
#include <string.h>

void* thread_function(void* arg) {
    int err = pthread_detach(pthread_self());
    if (err) {
        printf("thread: pthread_detach() failed: %s\n", strerror(err));
        return NULL;
    }

    printf("Thread started, id = %lu\n", pthread_self());

    pthread_exit(NULL);
}

int main() {
    pthread_t thread;

    while (1) {
        int err = pthread_create(&thread, NULL, thread_function, NULL);
        if (err) {
            printf("main: pthread_create() failed: %s\n", strerror(err));
            return 1;
        }
    }

    return 0;
}

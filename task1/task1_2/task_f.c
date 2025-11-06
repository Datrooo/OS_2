#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <unistd.h>
#include <string.h>

void* thread_function(void* arg) {
    printf("Thread started, id = %lu\n", pthread_self());
    pthread_exit(NULL);
}

int main() {
    pthread_t thread;
    pthread_attr_t attr;

    int err;
    err = pthread_attr_init(&attr);
    if (err != 0) {
        printf("pthread_attr_init failed: %d\n", err);
        return 1;
    }
    err = pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
    if (err != 0) {
        printf("pthread_attr_setdetachstate failed: %d\n", err);
        pthread_attr_destroy(&attr);
        return 1;
    }

    while (1) {
        int err = pthread_create(&thread, &attr, thread_function, NULL);
        if (err) {
            printf("main: pthread_create() failed: %s\n", strerror(err));
            return 1;
        }
    }

    pthread_attr_destroy(&attr);

    return 0;
}

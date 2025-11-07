#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <unistd.h>
#include <string.h>

void* thread_function(void* arg) {
    (void)arg;
    static int result = 42;
    sleep(2);
    return &result;
}


int main() {
    pthread_t thread;
    int* thread_result;

    int err = pthread_create(&thread, NULL, thread_function, NULL);
    if (err) {
        printf("main: pthread_create() failed: %s\n", strerror(err));
        return 1;
    }

    err = pthread_join(thread, (void**)&thread_result);
    if (err) {
        printf("main: pthread_join() failed: %s\n", strerror(err));
        return 1;
    }

    printf("main: result = %d\n", *thread_result);

    return 0;
}

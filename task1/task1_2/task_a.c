#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <unistd.h>
#include <string.h>

void* thread_function(void* arg) {
    (void)arg;
    sleep(2);
    printf("abacaba");
    return NULL;
}

int main() {
    pthread_t thread;

    int err = pthread_create(&thread, NULL, thread_function, NULL);
    if (err) {
        printf("main: pthread_create() failed: %s\n", strerror(err));
        return 1;
    }

    err = pthread_join(thread, NULL);
    if (err) {
        printf("main: pthread_join() failed: %s\n", strerror(err));
        return 1;
    }

    return 0;
}

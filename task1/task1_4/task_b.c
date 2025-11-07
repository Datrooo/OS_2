#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <unistd.h>
#include <string.h>

void* thread_func(void* arg) {
    (void)arg;
    // int err;
    // err = pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, NULL);
    // if (err) {
    //     printf("failed to set cancel type");
    //     pthread_exit(NULL);
    // }
    long long counter = 0;

    while (1) {
        counter++;
        // spthread_testcancel();
    }

    return NULL;
}

int main() {
    pthread_t thread;
    void *res;

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

    err = pthread_join(thread, &res);
    if (err) {
        printf("main: pthread_join() failed: %s\n", strerror(err));
        return 1;
    }

    if (res == PTHREAD_CANCELED) {
        printf("thread was canceled\n");
    }

    return 0;
}

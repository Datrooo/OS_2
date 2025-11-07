#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <unistd.h>
#include <string.h>

#define LEN_OF_STR 12

void cleanup(void* arg) {
    char** str = (char**)arg;
    if(!*str) {
        printf("memory alloca...\n");
        return;
    }
    printf("Cleaning up: freeing memory...\n");
    free(*str);
}

void* thread_func(void* arg) {
    (void)arg;
    char* str = NULL;
    pthread_cleanup_push(cleanup, &str);
    str = malloc(LEN_OF_STR * sizeof(char));
    if (!str) {
        perror("malloc error");
        pthread_exit(NULL);
    }
    strcpy(str, "hello world");


    while (1) {
        printf("%s\n", str);
        sleep(1);
    }

    pthread_cleanup_pop(1);
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

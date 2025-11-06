#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>

struct myStruct {
    int integer;
    char *string;
};

void* thread_func(void *arg) {
    struct myStruct *ms = (struct myStruct*) arg;

    for (int i = 0; i < 10; i++) {
        printf("[Thread] iteration %d: integer=%d, string=%s, addr=%p\n",
               i, ms->integer, ms->string, (void*)ms);
        usleep(300000);
    }

    return NULL;
}

void* puts_wrapper(void *arg) {
    puts((const char *)arg);
    return NULL;
}

int main() {
    pthread_t tid;
    pthread_attr_t attr;
    int err;

    err = pthread_attr_init(&attr);
    if (err != 0) {
        fprintf(stderr, "Error: pthread_attr_init() failed: %s\n", strerror(err));
        exit(EXIT_FAILURE);
    }

    err = pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
    if (err != 0) {
        fprintf(stderr, "Error: pthread_attr_setdetachstate() failed: %s\n", strerror(err));
        pthread_attr_destroy(&attr);
        exit(EXIT_FAILURE);
    }

    {
        struct myStruct ms = { 123, "StackData" };

        err = pthread_create(&tid, &attr, thread_func, &ms);
        if (err != 0) {
            fprintf(stderr, "Error: pthread_create() failed: %s\n", strerror(err));
            pthread_attr_destroy(&attr);
            exit(EXIT_FAILURE);
        }

        printf("[Main] ms at %p, integer=%d\n", (void*)&ms, ms.integer);
    }

    printf("[Main] creating new thread to overwrite stack\n");
    pthread_t tid2;
    err = pthread_create(&tid2, NULL, puts_wrapper, "Overwriting stack");
    if (err != 0) {
            fprintf(stderr, "Error: pthread_create() failed: %s\n", strerror(err));
            pthread_attr_destroy(&attr);
            exit(EXIT_FAILURE);
        }

    pthread_attr_destroy(&attr);

    printf("[Main] leaving main()...\n");

    sleep(4);
    printf("[Main] exits completely.\n");
    return 0;
}

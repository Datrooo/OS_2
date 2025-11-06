#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <pthread.h>
#include <sys/types.h>

#define FLAG -1

struct myStruct {
    int integer;
    char* string;
};

void* mythread_B(void* arg) {
    struct myStruct* ms = (struct myStruct*)arg;
    printf("Integer: %d, String: %s\n", ms->integer, ms->string);
    pthread_detach(pthread_self());
    if (ms->integer == FLAG) {
        free(ms);
    }
    return NULL;
}

struct myStruct ms_global = {
    .integer = 10,
    .string = "Global"
};

int main() {
    pthread_t tids[3];
    int err;
    pthread_attr_t attr;

    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);

    err = pthread_create(&tids[0], &attr, mythread_B, &ms_global);
    if (err) {
        printf("Main: pthread_create() failed: %s\n", strerror(err));
        pthread_attr_destroy(&attr);
        return -1;
    }

    static struct myStruct ms_static = {
        .integer = 25,
        .string = "Static"
    };

    err = pthread_create(&tids[1], &attr, mythread_B, &ms_static);
    if (err) {
        printf("Main: pthread_create() failed: %s\n", strerror(err));
        pthread_attr_destroy(&attr);
        return -1;
    }

    struct myStruct* ms_heap = malloc(sizeof(struct myStruct));
    ms_heap->integer = FLAG;
    ms_heap->string = "Heap";

    err = pthread_create(&tids[2], &attr, mythread_B, ms_heap);
    if (err) {
        printf("Main: pthread_create() failed: %s\n", strerror(err));
        pthread_attr_destroy(&attr);
        return -1;
    }

    pthread_attr_destroy(&attr);

    sleep(1);

    pthread_exit(NULL);
}

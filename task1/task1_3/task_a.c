#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <pthread.h>
#include <sys/types.h>

struct myStruct {
    int integer;
    char* string;
};

void* mythread_A(void* arg) {
    struct myStruct* ms = (struct myStruct*)arg;
    printf("Integer: %d\n", ms->integer);
    printf("String: %s\n", ms->string);
    return NULL;
}

int main() {
    pthread_t tid;
    int err;

    struct myStruct ms_local = {
        .integer = 15,
        .string = "Local"
    };

    err = pthread_create(&tid, NULL, mythread_A, &ms_local);
    if (err) {
        printf("Main: pthread_create() failed: %s\n", strerror(err));
        return -1;
    }

    pthread_join(tid, NULL);
// todo check error
    return 0;
}

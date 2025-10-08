#define _GNU_SOURCE
#include <stdio.h>
#include <pthread.h>
#include <string.h>
#include <errno.h>
#include <sys/types.h>
#include <unistd.h>

#define MAX_THREADS 5

int global_var = 100;

typedef struct {
    int thread_num;
    pthread_t *tid_ptr;
} thread_arg_t;

void *mythread(void *arg) {
    thread_arg_t *targ = (thread_arg_t*)arg;

    int thread_num = targ->thread_num;
    pthread_t created_tid = *(targ->tid_ptr);

    int local = 200;
    static int static_local = 300;
    const int const_local = 400;

    pthread_t self_tid = pthread_self();

    printf("\n\n");
    printf("[%d] PIDs: pid=%d ppid=%d tid=%d\n", thread_num, getpid(), getppid(), gettid());

    printf("[%d] pthread_self(): %lu\n", thread_num, (unsigned long)self_tid);
    printf("[%d] pthread_create() returned: %lu\n", thread_num, (unsigned long)created_tid);

    printf("[%d] pthread_equal(self, created): %d\n", thread_num,
           pthread_equal(self_tid, created_tid));

    printf("[%d] Local variable: %p\n", thread_num, (void*)&local);
    printf("[%d] Static local variable: %p\n", thread_num, (void*)&static_local);
    printf("[%d] Const local variable: %p\n", thread_num, (void*)&const_local);
    printf("[%d] Global variable: %p\n", thread_num, (void*)&global_var);

    return NULL;
}

int main() {
    pthread_t tid[MAX_THREADS];
    thread_arg_t args[MAX_THREADS];
    int err;

    printf("main [pid=%d ppid=%d tid=%d]: Hello from main!\n", getpid(), getppid(), gettid());

    for (int i = 0; i < MAX_THREADS; i++) {
        args[i].thread_num = i + 1;
        args[i].tid_ptr = tid + i;

        err = pthread_create(tid + i, NULL, mythread, args + i);
        if (err) {
            printf("main: pthread_create() failed: %s\n", strerror(err));
            return -1;
        }
    }

    for (int i = 0; i < MAX_THREADS; i++) {
        err = pthread_join(tid[i], NULL);
        if (err) {
            printf("main: pthread_join() failed: %s\n", strerror(err));
            return -1;
        }
        printf("main: joined thread %d (tid=%lu)\n", i + 1, (unsigned long)tid[i]);
    }

    return 0;
}

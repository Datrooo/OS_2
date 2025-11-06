// a_print_cancel.c
#define _XOPEN_SOURCE 700
#include <pthread.h>
#include <stdio.h>
#include <unistd.h>   // sleep
#include <stdlib.h>

void* worker(void* arg) {
	char* string = "Stroka";
	int counter = 0;
	while (1) {
		counter++;
		printf("%s num. %d\n", string, counter);
	}
	return NULL;
}

int main(void) {
    pthread_t tid;
    int err;
    if (pthread_create(&tid, NULL, worker, NULL) != 0) {
        perror("pthread_create");
        return 1;
    }

    sleep(3); // даём потоку поработать ~3 секунды
    puts("[main] requesting cancellation...");
    pthread_cancel(tid);

    void* ret = NULL;
    if (pthread_join(tid, &ret) != 0) {
        perror("pthread_join");
        return 1;
    }

    if (ret == PTHREAD_CANCELED) {
        puts("[main] thread was canceled (PTHREAD_CANCELED).");
    } else {
        puts("[main] thread exited normally.");
    }
    return 0;
}

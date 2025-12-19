#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <pthread.h>
#include <time.h>
#include "rwlock_queue.h"
#include "counters.h"

void stop_threads(pthread_t search_threads[3],
                         pthread_t swap_threads[3],
                         int created_count) {

    for (int i = 0; i < created_count; i++) {
        pthread_t tid = (i < 3) ? search_threads[i] : swap_threads[i - 3];
        pthread_cancel(tid);
    }

    for (int i = 0; i < created_count; i++) {
        pthread_t tid = (i < 3) ? search_threads[i] : swap_threads[i - 3];
        pthread_join(tid, NULL);
    }
}


int main(int argc, char** argv) {
    if (argc < 2) {
        printf("Please run the program as follows: %s [size]\n", argv[0]);
        return 1;
    }
    
    srand(time(NULL));

    int size = atoi(argv[1]);
    printf("TESTING A LIST OF SIZE %d ELEMENTS\n", size);
    Storage* storage = create_storage(size);
    if (!storage) {
        printf("Error create of a list!\n");
        return 1;
    }

    reset_atomic_counters();

    pthread_t search_threads[3];
    pthread_t swap_threads[3];

    printf("Launch...\n");
    if (pthread_create(&search_threads[0], NULL, find_rising_pairs, storage)!= 0) {
        free_storage(storage);
        return 1;
    }
    if (pthread_create(&search_threads[1], NULL, find_falling_pairs, storage) != 0) {
        stop_threads(search_threads, swap_threads, 1);
        free_storage(storage);
        return 1;
    }
    if (pthread_create(&search_threads[2], NULL, find_equal_pairs, storage) != 0) {
        stop_threads(search_threads, swap_threads, 2);
        free_storage(storage);
        return 1;
    }
    if (pthread_create(&swap_threads[0], NULL, swap_thread_1, storage) != 0) {
        stop_threads(search_threads, swap_threads, 3);
        free_storage(storage);
        return 1;
    }
    if (pthread_create(&swap_threads[1], NULL, swap_thread_2, storage) != 0) {
        stop_threads(search_threads, swap_threads, 4);
        free_storage(storage);
        return 1;
    }
    if (pthread_create(&swap_threads[2], NULL, swap_thread_3, storage) != 0) {
        stop_threads(search_threads, swap_threads, 5);
        free_storage(storage);
        return 1;
    }

    for (int t = 0; t < 5; t++) {
        sleep(2);
        print_stats();
    }

    stop_threads(search_threads, swap_threads, 6);
    free_storage(storage);


    return 0;
}
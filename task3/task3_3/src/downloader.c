#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <unistd.h>

#include "downloader.h"

static pthread_t *downloader_threads = NULL;
static int downloader_count = 0;
static volatile int should_exit = 0;

static void *downloader_thread_func(void *arg) {
    (void)arg;
    
    fprintf(stdout, "[DOWNLOADER] Thread started\n");
    
    while (!should_exit) {
        /* TODO: получить задачу из очереди и выполнить */
    }
    
    fprintf(stdout, "[DOWNLOADER] Thread exiting\n");
    
    return NULL;
}

int downloader_init(int pool_size) {
    downloader_count = pool_size;
    should_exit = 0;
    
    downloader_threads = (pthread_t *)malloc(sizeof(pthread_t) * pool_size);
    if (!downloader_threads) return -1;
    
    for (int i = 0; i < pool_size; i++) {
        if (pthread_create(&downloader_threads[i], NULL, downloader_thread_func, NULL) != 0) {
            fprintf(stderr, "[DOWNLOADER] Failed to create thread %d\n", i);
            return -1;
        }
        fprintf(stdout, "[DOWNLOADER] Thread %d created\n", i);
    }
    
    return 0;
}

int downloader_enqueue(CacheEntry *entry) {
    if (!entry) return -1;
    
    /* TODO: добавить в очередь */
    fprintf(stdout, "[DOWNLOADER] Enqueue: %s\n", entry->key.s);
    
    return 0;
}

void downloader_shutdown(void) {
    fprintf(stdout, "[DOWNLOADER] Shutting down...\n");
    
    should_exit = 1;
    
    for (int i = 0; i < downloader_count; i++) {
        pthread_join(downloader_threads[i], NULL);
    }
    
    free(downloader_threads);
    downloader_threads = NULL;
    
    fprintf(stdout, "[DOWNLOADER] Shutdown complete\n");
}
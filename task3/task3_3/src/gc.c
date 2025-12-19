#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <unistd.h>

#include "gc.h"

static pthread_t gc_thread;
static volatile int should_exit = 0;

static void *gc_thread_func(void *arg) {
    (void)arg;
    
    fprintf(stdout, "[GC] Thread started\n");
    
    while (!should_exit) {
        // TODO: проверить размер кэша и выполнить LRU eviction
    }
    
    fprintf(stdout, "[GC] Thread exiting\n");
    
    return NULL;
}

int gc_init(size_t max_cache_size) {
    (void)max_cache_size;
    
    should_exit = 0;
    
    if (pthread_create(&gc_thread, NULL, gc_thread_func, NULL) != 0) {
        fprintf(stderr, "[GC] Failed to create GC thread\n");
        return -1;
    }
    
    fprintf(stdout, "[GC] GC thread created\n");
    
    return 0;
}

void gc_shutdown(void) {
    fprintf(stdout, "[GC] Shutting down...\n");
    
    should_exit = 1;
    pthread_join(gc_thread, NULL);
    
    fprintf(stdout, "[GC] Shutdown complete\n");
}
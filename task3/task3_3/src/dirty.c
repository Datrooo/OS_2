#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>

#include "dirty.h"
#include "loop.h"

#define DIRTY_QUEUE_MAX 2048

typedef struct DirtyQueue{
    CacheEntry **entries;
    int count;
    int capacity;
    pthread_mutex_t m;
} DirtyQueue;

static DirtyQueue g_dirty_queue = {
    .entries = NULL,
    .count = 0,
    .capacity = 0
};

static int g_dirty_initialized = 0;

int dirty_init(void) {
    if (g_dirty_initialized) {
        return 0;
    }
    
    g_dirty_queue.capacity = DIRTY_QUEUE_MAX;
    g_dirty_queue.entries = (CacheEntry **)malloc(DIRTY_QUEUE_MAX * sizeof(CacheEntry *));
    if (!g_dirty_queue.entries) {
        fprintf(stderr, "[DIRTY] Failed to allocate queue\n");
        return -1;
    }
    
    g_dirty_queue.count = 0;
    pthread_mutex_init(&g_dirty_queue.m, NULL);
    
    g_dirty_initialized = 1;
    
    fprintf(stdout, "[DIRTY] Dirty queue initialized (capacity: %d)\n", DIRTY_QUEUE_MAX);
    
    return 0;
}

int dirty_enqueue(CacheEntry *entry) {
    if (!entry) {
        return -1;
    }

    if (!g_dirty_initialized) {
        fprintf(stderr, "[DIRTY] dirty_enqueue() called before dirty_init()\n");
        return -1;
    }
    
    pthread_mutex_lock(&g_dirty_queue.m);
    
    for (int i = 0; i < g_dirty_queue.count; i++) {
        if (g_dirty_queue.entries[i] == entry) {
            pthread_mutex_unlock(&g_dirty_queue.m);
            loop_notify_dirty();
            return 0;
        }
    }
    
    if (g_dirty_queue.count >= g_dirty_queue.capacity) {
        fprintf(stderr, "[DIRTY] Queue full (size: %d)\n", g_dirty_queue.count);
        pthread_mutex_unlock(&g_dirty_queue.m);
        
        loop_notify_dirty();
        return -1;
    }
    
    g_dirty_queue.entries[g_dirty_queue.count++] = entry;
    
    pthread_mutex_unlock(&g_dirty_queue.m);

    loop_notify_dirty();
    
    return 0;
}

int dirty_process_all(void) {
    pthread_mutex_lock(&g_dirty_queue.m);
    
    int count = g_dirty_queue.count;
    
    if (count == 0) {
        pthread_mutex_unlock(&g_dirty_queue.m);
        return 0;
    }
    
    fprintf(stdout, "[DIRTY] Processing %d dirty entries\n", count);
    
    CacheEntry **to_process = (CacheEntry **)malloc(count * sizeof(CacheEntry *));
    if (!to_process) {
        pthread_mutex_unlock(&g_dirty_queue.m);
        return -1;
    }
    
    memcpy(to_process, g_dirty_queue.entries, count * sizeof(CacheEntry *));
    g_dirty_queue.count = 0;
    
    pthread_mutex_unlock(&g_dirty_queue.m);
    
    for (int i = 0; i < count; i++) {
        CacheEntry *entry = to_process[i];
        if (!entry) continue;
        
        pthread_mutex_lock(&entry->m);
        
        if (entry->is_dirty) {
            entry->is_dirty = 0;
            
            fprintf(stdout, "[DIRTY] Entry %lu: %zu bytes, complete=%d\n",
                    entry->id, entry->produced, entry->is_completed);
        }
        
        pthread_mutex_unlock(&entry->m);
    }
    
    free(to_process);
    
    return count;
}

int dirty_get_queue_size(void) {
    pthread_mutex_lock(&g_dirty_queue.m);
    int size = g_dirty_queue.count;
    pthread_mutex_unlock(&g_dirty_queue.m);
    return size;
}

void dirty_cleanup(void) {
    if (!g_dirty_initialized) {
        return;
    }
    
    fprintf(stdout, "[DIRTY] Cleaning up dirty queue\n");
    
    pthread_mutex_lock(&g_dirty_queue.m);
    
    if (g_dirty_queue.entries) {
        free(g_dirty_queue.entries);
        g_dirty_queue.entries = NULL;
    }
    
    g_dirty_queue.count = 0;
    g_dirty_queue.capacity = 0;
    
    pthread_mutex_unlock(&g_dirty_queue.m);
    pthread_mutex_destroy(&g_dirty_queue.m);
    
    g_dirty_initialized = 0;
    
    fprintf(stdout, "[DIRTY] Dirty queue cleaned up\n");
}
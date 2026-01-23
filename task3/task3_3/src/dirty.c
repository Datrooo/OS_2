#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <errno.h>

#include "dirty.h"
#include "loop.h"
#include "cache.h"

#define DIRTY_QUEUE_MAX 2048

typedef struct DirtyQueue{
    CacheEntry **entries;
    int count;
    int capacity;
    pthread_mutex_t m;
} DirtyQueue;

typedef struct DirtyState {
    DirtyQueue q;
    int initialized;
} DirtyState;

static DirtyState g_dirty = {
    .q = {
        .entries = NULL,
        .count = 0,
        .capacity = 0,
    },
    .initialized = 0,
};

static int dirty_mutex_lock(pthread_mutex_t *m, const char *ctx) {
    int rc = pthread_mutex_lock(m);
    if (rc != 0) {
        fprintf(stderr, "[DIRTY] %s: pthread_mutex_lock failed: %s\n", ctx, strerror(rc));
        return -1;
    }
    return 0;
}

static int dirty_mutex_unlock(pthread_mutex_t *m, const char *ctx) {
    int rc = pthread_mutex_unlock(m);
    if (rc != 0) {
        fprintf(stderr, "[DIRTY] %s: pthread_mutex_unlock failed: %s\n", ctx, strerror(rc));
        return -1;
    }
    return 0;
}

int dirty_create(void) {
    if (g_dirty.initialized) {
        return 0;
    }
    
    g_dirty.q.capacity = DIRTY_QUEUE_MAX;
    g_dirty.q.entries = (CacheEntry **)malloc(DIRTY_QUEUE_MAX * sizeof(CacheEntry *));
    if (!g_dirty.q.entries) {
        fprintf(stderr, "[DIRTY] Failed to allocate queue\n");
        return -1;
    }
    
    g_dirty.q.count = 0;
    int rc = pthread_mutex_init(&g_dirty.q.m, NULL);
    if (rc != 0) {
        fprintf(stderr, "[DIRTY] pthread_mutex_init failed: %s\n", strerror(rc));
        free(g_dirty.q.entries);
        g_dirty.q.entries = NULL;
        g_dirty.q.capacity = 0;
        return -1;
    }
    
    g_dirty.initialized = 1;
    
    fprintf(stdout, "[DIRTY] Dirty queue initialized (capacity: %d)\n", DIRTY_QUEUE_MAX);
    
    return 0;
}

int dirty_enqueue(CacheEntry *entry) {
    if (!entry) {
        return -1;
    }

    if (!g_dirty.initialized) {
        fprintf(stderr, "[DIRTY] dirty_enqueue() called before dirty_create()\n");
        return -1;
    }
    
    if (dirty_mutex_lock(&g_dirty.q.m, "enqueue") != 0) {
        return -1;
    }
    
    for (int i = 0; i < g_dirty.q.count; i++) {
        if (g_dirty.q.entries[i] == entry) {
            (void)dirty_mutex_unlock(&g_dirty.q.m, "enqueue(already)");
            loop_notify_dirty();
            return 0;
        }
    }
    
    if (g_dirty.q.count >= g_dirty.q.capacity) {
        fprintf(stderr, "[DIRTY] Queue full (size: %d)\n", g_dirty.q.count);
        (void)dirty_mutex_unlock(&g_dirty.q.m, "enqueue(full)");
        
        loop_notify_dirty();
        return -1;
    }
    
    g_dirty.q.entries[g_dirty.q.count++] = entry;
    cache_entry_acquire(entry);
    
    (void)dirty_mutex_unlock(&g_dirty.q.m, "enqueue(done)");

    loop_notify_dirty();
    
    return 0;
}

int dirty_process_all(CacheEntry ***out_entries) {
    if (out_entries) {
        *out_entries = NULL;
    }
    if (dirty_mutex_lock(&g_dirty.q.m, "process_all") != 0) {
        return -1;
    }
    
    int count = g_dirty.q.count;
    
    if (count == 0) {
        (void)dirty_mutex_unlock(&g_dirty.q.m, "process_all(empty)");
        return 0;
    }
    
    fprintf(stdout, "[DIRTY] Processing %d dirty entries\n", count);
    
    CacheEntry **to_process = (CacheEntry **)malloc(count * sizeof(CacheEntry *));
    if (!to_process) {
        (void)dirty_mutex_unlock(&g_dirty.q.m, "process_all(alloc_fail)");
        return -1;
    }
    
    memcpy(to_process, g_dirty.q.entries, count * sizeof(CacheEntry *));
    g_dirty.q.count = 0;
    
    (void)dirty_mutex_unlock(&g_dirty.q.m, "process_all(copy_done)");
    
    for (int i = 0; i < count; i++) {
        CacheEntry *entry = to_process[i];
        if (!entry) continue;
        
        int lrc = pthread_mutex_lock(&entry->m);
        if (lrc != 0) {
            fprintf(stderr, "[DIRTY] process_all: pthread_mutex_lock(entry) failed: %s\n", strerror(lrc));
            continue;
        }
        
        if (entry->is_dirty) {
            entry->is_dirty = 0;
            
            fprintf(stdout, "[DIRTY] Entry %lu: %zu bytes, complete=%d\n",
                    entry->id, entry->produced, entry->is_completed);
        }
        
        int urc = pthread_mutex_unlock(&entry->m);
        if (urc != 0) {
            fprintf(stderr, "[DIRTY] process_all: pthread_mutex_unlock(entry) failed: %s\n", strerror(urc));
        }
    }

    if (!out_entries) {
        for (int i = 0; i < count; i++) {
            if (to_process[i]) {
                cache_entry_release(to_process[i]);
            }
        }
        free(to_process);
        return count;
    }

    *out_entries = to_process;
    return count;
}

void dirty_destroy(void) {
    if (!g_dirty.initialized) {
        return;
    }
    
    fprintf(stdout, "[DIRTY] Cleaning up dirty queue\n");
    
    if (dirty_mutex_lock(&g_dirty.q.m, "cleanup") != 0) {
        return;
    }
    
    if (g_dirty.q.entries) {
        free(g_dirty.q.entries);
        g_dirty.q.entries = NULL;
    }
    
    g_dirty.q.count = 0;
    g_dirty.q.capacity = 0;
    
    (void)dirty_mutex_unlock(&g_dirty.q.m, "cleanup(unlock)");
    int drc = pthread_mutex_destroy(&g_dirty.q.m);
    if (drc != 0) {
        fprintf(stderr, "[DIRTY] pthread_mutex_destroy failed: %s\n", strerror(drc));
    }
    
    g_dirty.initialized = 0;
    
    fprintf(stdout, "[DIRTY] Dirty queue cleaned up\n");
}
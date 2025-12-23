#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <time.h>
#include <string.h>

#include "gc.h"
#include "cache.h"

static pthread_t gc_thread;
static volatile int should_exit = 0;

static pthread_mutex_t g_gc_m = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t g_gc_cond = PTHREAD_COND_INITIALIZER;
static int g_gc_inited = 0;
static int g_gc_requested = 0;

static int gc_mutex_lock(pthread_mutex_t *m, const char *ctx) {
    int rc = pthread_mutex_lock(m);
    if (rc != 0) {
        fprintf(stderr, "[GC] %s: pthread_mutex_lock failed: %s\n", ctx, strerror(rc));
        return -1;
    }
    return 0;
}

static int gc_mutex_unlock(pthread_mutex_t *m, const char *ctx) {
    int rc = pthread_mutex_unlock(m);
    if (rc != 0) {
        fprintf(stderr, "[GC] %s: pthread_mutex_unlock failed: %s\n", ctx, strerror(rc));
        return -1;
    }
    return 0;
}

void gc_notify_pressure(void) {
    if (gc_mutex_lock(&g_gc_m, "notify") != 0) {
        return;
    }
    if (g_gc_inited) {
        g_gc_requested = 1;
        int rc = pthread_cond_signal(&g_gc_cond);
        if (rc != 0) {
            fprintf(stderr, "[GC] notify: pthread_cond_signal failed: %s\n", strerror(rc));
        }
    }
    (void)gc_mutex_unlock(&g_gc_m, "notify(unlock)");
}

static void *gc_thread_func(void *arg) {
    (void)arg;
    
    fprintf(stdout, "[GC] Thread started\n");
    
    while (!should_exit) {
        if (gc_mutex_lock(&g_gc_m, "thread") != 0) {
            break;
        }
        while (!should_exit && !g_gc_requested) {
            int wrc = pthread_cond_wait(&g_gc_cond, &g_gc_m);
            if (wrc != 0) {
                fprintf(stderr, "[GC] thread: pthread_cond_wait failed: %s\n", strerror(wrc));
                break;
            }
        }
        g_gc_requested = 0;
        (void)gc_mutex_unlock(&g_gc_m, "thread(unlock)");

        if (should_exit) {
            break;
        }

        int delete = cache_trim_to_max();
        if (delete > 0) {
            fprintf(stdout, "[GC] Deleted %d entries\n", delete);
        }
    }
    
    fprintf(stdout, "[GC] Thread exiting\n");
    
    return NULL;
}

int gc_init(size_t max_cache_size) {
    (void)max_cache_size;
    
    should_exit = 0;
    if (gc_mutex_lock(&g_gc_m, "init") != 0) {
        return -1;
    }
    g_gc_inited = 1;
    g_gc_requested = 0;
    (void)gc_mutex_unlock(&g_gc_m, "init(unlock)");
    
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

    if (gc_mutex_lock(&g_gc_m, "shutdown") != 0) {
        return;
    }
    g_gc_requested = 1;
    int brc = pthread_cond_broadcast(&g_gc_cond);
    if (brc != 0) {
        fprintf(stderr, "[GC] shutdown: pthread_cond_broadcast failed: %s\n", strerror(brc));
    }
    (void)gc_mutex_unlock(&g_gc_m, "shutdown(unlock)");

    int jrc = pthread_join(gc_thread, NULL);
    if (jrc != 0) {
        fprintf(stderr, "[GC] pthread_join failed: %s\n", strerror(jrc));
    }

    if (gc_mutex_lock(&g_gc_m, "shutdown(final)") != 0) {
        return;
    }
    g_gc_inited = 0;
    (void)gc_mutex_unlock(&g_gc_m, "shutdown(final_unlock)");
    
    fprintf(stdout, "[GC] Shutdown complete\n");
}
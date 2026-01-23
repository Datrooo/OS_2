#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <time.h>
#include <string.h>

#include "gc.h"
#include "cache.h"

typedef struct GcState {
    pthread_t thread;
    volatile int should_exit;
    pthread_mutex_t m;
    pthread_cond_t cond;
    int inited;
    int requested;
} GcState;

static GcState g_gc = {
    .thread = (pthread_t)0,
    .should_exit = 0,
    .m = PTHREAD_MUTEX_INITIALIZER,
    .cond = PTHREAD_COND_INITIALIZER,
    .inited = 0,
    .requested = 0,
};

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
    if (gc_mutex_lock(&g_gc.m, "notify") != 0) {
        return;
    }
    if (g_gc.inited) {
        g_gc.requested = 1;
        int rc = pthread_cond_signal(&g_gc.cond);
        if (rc != 0) {
            fprintf(stderr, "[GC] notify: pthread_cond_signal failed: %s\n", strerror(rc));
        }
    }
    (void)gc_mutex_unlock(&g_gc.m, "notify(unlock)");
}

static void *gc_thread_func(void *arg) {
    (void)arg;
    
    fprintf(stdout, "[GC] Thread started\n");
    
    while (!g_gc.should_exit) {
        if (gc_mutex_lock(&g_gc.m, "thread") != 0) {
            break;
        }
        while (!g_gc.should_exit && !g_gc.requested) {
            int wrc = pthread_cond_wait(&g_gc.cond, &g_gc.m);
            if (wrc != 0) {
                fprintf(stderr, "[GC] thread: pthread_cond_wait failed: %s\n", strerror(wrc));
                break;
            }
        }
        g_gc.requested = 0;
        (void)gc_mutex_unlock(&g_gc.m, "thread(unlock)");

        if (g_gc.should_exit) {
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

int gc_create(size_t max_cache_size) {
    (void)max_cache_size;

    if (gc_mutex_lock(&g_gc.m, "init(check)") != 0) {
        return -1;
    }
    if (g_gc.inited) {
        (void)gc_mutex_unlock(&g_gc.m, "init(check_unlock)");
        return 0;
    }
    (void)gc_mutex_unlock(&g_gc.m, "init(check_unlock)");
    
    g_gc.should_exit = 0;
    if (gc_mutex_lock(&g_gc.m, "init") != 0) {
        return -1;
    }
    g_gc.inited = 1;
    g_gc.requested = 0;
    (void)gc_mutex_unlock(&g_gc.m, "init(unlock)");
    
    if (pthread_create(&g_gc.thread, NULL, gc_thread_func, NULL) != 0) {
        fprintf(stderr, "[GC] Failed to create GC thread\n");
        return -1;
    }
    
    fprintf(stdout, "[GC] GC thread created\n");
    
    return 0;
}

void gc_destroy(void) {
    fprintf(stdout, "[GC] Shutting down...\n");
    
    g_gc.should_exit = 1;

    if (gc_mutex_lock(&g_gc.m, "shutdown") != 0) {
        return;
    }
    g_gc.requested = 1;
    int brc = pthread_cond_broadcast(&g_gc.cond);
    if (brc != 0) {
        fprintf(stderr, "[GC] shutdown: pthread_cond_broadcast failed: %s\n", strerror(brc));
    }
    (void)gc_mutex_unlock(&g_gc.m, "shutdown(unlock)");

    int jrc = pthread_join(g_gc.thread, NULL);
    if (jrc != 0) {
        fprintf(stderr, "[GC] pthread_join failed: %s\n", strerror(jrc));
    }

    if (gc_mutex_lock(&g_gc.m, "shutdown(final)") != 0) {
        return;
    }
    g_gc.inited = 0;
    (void)gc_mutex_unlock(&g_gc.m, "shutdown(final_unlock)");
    
    fprintf(stdout, "[GC] Shutdown complete\n");
}
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <errno.h>
#include "subscribers.h"

#include "types.h"

static int subs_mutex_lock(pthread_mutex_t *m, const char *ctx) {
    int rc = pthread_mutex_lock(m);
    if (rc != 0) {
        fprintf(stderr, "[SUBS] %s: pthread_mutex_lock failed: %s\n", ctx, strerror(rc));
        return -1;
    }
    return 0;
}

static int subs_mutex_unlock(pthread_mutex_t *m, const char *ctx) {
    int rc = pthread_mutex_unlock(m);
    if (rc != 0) {
        fprintf(stderr, "[SUBS] %s: pthread_mutex_unlock failed: %s\n", ctx, strerror(rc));
        return -1;
    }
    return 0;
}


int subscriber_add(CacheEntry *entry, Session *session) {
    if (!entry || !session) {
        return -1;
    }
    
    if (subs_mutex_lock(&entry->m, "add") != 0) {
        return -1;
    }
    
    SubNode *existing = entry->subs;
    while (existing) {
        if (existing->s == session) {
            (void)subs_mutex_unlock(&entry->m, "add(already)");
            fprintf(stdout, "[SUBS] Session %llu already subscribed\n",
                    (unsigned long long)session->id);
            return 0;
        }
        existing = existing->hh_next;
    }
    
    SubNode *new_sub = malloc(sizeof(SubNode));
    if (!new_sub) {
        (void)subs_mutex_unlock(&entry->m, "add(alloc_fail)");
        fprintf(stderr, "[SUBS] Failed to allocate SubNode\n");
        return -1;
    }
    
    new_sub->s = session;
    new_sub->hh_next = entry->subs;
    entry->subs = new_sub;
    entry->subs_count++;
    
    fprintf(stdout, "[SUBS] Added subscriber (session %llu), total: %d\n",
            (unsigned long long)session->id, entry->subs_count);
    
    (void)subs_mutex_unlock(&entry->m, "add(done)");
    
    return 0;
}


int subscriber_remove(CacheEntry *entry, Session *session) {
    if (!entry || !session) {
        return -1;
    }
    
    if (subs_mutex_lock(&entry->m, "remove") != 0) {
        return -1;
    }
    
    SubNode *current = entry->subs;
    SubNode *prev = NULL;
    
    while (current) {
        if (current->s == session) {
            if (prev) {
                prev->hh_next = current->hh_next;
            } else {
                entry->subs = current->hh_next;
            }
            
            free(current);
            entry->subs_count--;
            
            fprintf(stdout, "[SUBS] Removed subscriber (session %llu), total: %d\n",
                    (unsigned long long)session->id, entry->subs_count);
            
                (void)subs_mutex_unlock(&entry->m, "remove(done)");
            return 0;
        }
        
        prev = current;
        current = current->hh_next;
    }
    
    fprintf(stderr, "[SUBS] Session %llu not found in subscribers\n",
            (unsigned long long)session->id);
    
    (void)subs_mutex_unlock(&entry->m, "remove(not_found)");
    return -1;
}


Session **subscriber_get_all(CacheEntry *entry, int *out_count) {
    if (!entry || !out_count) {
        return NULL;
    }
    
    if (subs_mutex_lock(&entry->m, "get_all") != 0) {
        return NULL;
    }
    
    int count = entry->subs_count;
    if (count == 0) {
        (void)subs_mutex_unlock(&entry->m, "get_all(empty)");
        *out_count = 0;
        return NULL;
    }
    
    Session **sessions = malloc((count + 1) * sizeof(Session *));
    if (!sessions) {
        (void)subs_mutex_unlock(&entry->m, "get_all(alloc_fail)");
        return NULL;
    }
    
    int i = 0;
    SubNode *current = entry->subs;
    while (current && i < count) {
        sessions[i] = current->s;
        current = current->hh_next;
        i++;
    }
    sessions[count] = NULL;
    
    *out_count = count;
    
    fprintf(stdout, "[SUBS] Retrieved %d subscribers for entry\n", count);
    
    (void)subs_mutex_unlock(&entry->m, "get_all(done)");
    
    return sessions;
}


int subscriber_clear_all(CacheEntry *entry) {
    if (!entry) {
        return -1;
    }
    
    if (subs_mutex_lock(&entry->m, "clear_all") != 0) {
        return -1;
    }
    
    SubNode *current = entry->subs;
    while (current) {
        SubNode *next = current->hh_next;
        free(current);
        current = next;
    }
    
    entry->subs = NULL;
    entry->subs_count = 0;
    
    fprintf(stdout, "[SUBS] Cleared all subscribers\n");
    
    (void)subs_mutex_unlock(&entry->m, "clear_all(done)");
    
    return 0;
}

int is_entry_downloading(CacheEntry *entry) {
    if (!entry) {
        return 0;
    }
    
    if (subs_mutex_lock(&entry->m, "is_downloading") != 0) {
        return 0;
    }
    int downloading = entry->is_downloader_running;
    (void)subs_mutex_unlock(&entry->m, "is_downloading(done)");
    
    return downloading;
}

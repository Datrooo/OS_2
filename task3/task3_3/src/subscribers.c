#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include "subscribers.h"

#include "types.h"


int subscriber_add(CacheEntry *entry, Session *session) {
    if (!entry || !session) {
        return -1;
    }
    
    pthread_mutex_lock(&entry->m);
    
    SubNode *existing = entry->subs;
    while (existing) {
        if (existing->s == session) {
            pthread_mutex_unlock(&entry->m);
            fprintf(stdout, "[SUBS] Session %llu already subscribed\n",
                    (unsigned long long)session->id);
            return 0;
        }
        existing = existing->hh_next;
    }
    
    SubNode *new_sub = malloc(sizeof(SubNode));
    if (!new_sub) {
        pthread_mutex_unlock(&entry->m);
        fprintf(stderr, "[SUBS] Failed to allocate SubNode\n");
        return -1;
    }
    
    new_sub->s = session;
    new_sub->hh_next = entry->subs;
    entry->subs = new_sub;
    entry->subs_count++;
    
    fprintf(stdout, "[SUBS] Added subscriber (session %llu), total: %d\n",
            (unsigned long long)session->id, entry->subs_count);
    
    pthread_mutex_unlock(&entry->m);
    
    return 0;
}


int subscriber_remove(CacheEntry *entry, Session *session) {
    if (!entry || !session) {
        return -1;
    }
    
    pthread_mutex_lock(&entry->m);
    
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
            
            pthread_mutex_unlock(&entry->m);
            return 0;
        }
        
        prev = current;
        current = current->hh_next;
    }
    
    fprintf(stderr, "[SUBS] Session %llu not found in subscribers\n",
            (unsigned long long)session->id);
    
    pthread_mutex_unlock(&entry->m);
    return -1;
}


Session **subscriber_get_all(CacheEntry *entry, int *out_count) {
    if (!entry || !out_count) {
        return NULL;
    }
    
    pthread_mutex_lock(&entry->m);
    
    int count = entry->subs_count;
    if (count == 0) {
        pthread_mutex_unlock(&entry->m);
        *out_count = 0;
        return NULL;
    }
    
    Session **sessions = malloc((count + 1) * sizeof(Session *));
    if (!sessions) {
        pthread_mutex_unlock(&entry->m);
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
    
    pthread_mutex_unlock(&entry->m);
    
    return sessions;
}


int subscriber_clear_all(CacheEntry *entry) {
    if (!entry) {
        return -1;
    }
    
    pthread_mutex_lock(&entry->m);
    
    SubNode *current = entry->subs;
    while (current) {
        SubNode *next = current->hh_next;
        free(current);
        current = next;
    }
    
    entry->subs = NULL;
    entry->subs_count = 0;
    
    fprintf(stdout, "[SUBS] Cleared all subscribers\n");
    
    pthread_mutex_unlock(&entry->m);
    
    return 0;
}

int is_entry_downloading(CacheEntry *entry) {
    if (!entry) {
        return 0;
    }
    
    pthread_mutex_lock(&entry->m);
    int downloading = entry->is_downloader_running;
    pthread_mutex_unlock(&entry->m);
    
    return downloading;
}

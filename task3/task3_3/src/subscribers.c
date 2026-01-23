#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>

#include "subscribers.h"

typedef struct SubSessionNode {
    Session *s;
    struct SubSessionNode *next;
} SubSessionNode;

typedef struct EntrySubsNode {
    CacheEntry *entry;
    SubSessionNode *head;
    int count;
    struct EntrySubsNode *next;
} EntrySubsNode;

static pthread_mutex_t g_subs_m = PTHREAD_MUTEX_INITIALIZER;
static EntrySubsNode *g_entries = NULL;

static EntrySubsNode **find_entry_pp(CacheEntry *entry) {
    EntrySubsNode **pp = &g_entries;
    while (*pp) {
        if ((*pp)->entry == entry) {
            return pp;
        }
        pp = &((*pp)->next);
    }
    return pp;
}

static void free_session_list(SubSessionNode *n) {
    while (n) {
        SubSessionNode *next = n->next;
        free(n);
        n = next;
    }
}

int subscriber_add(CacheEntry *entry, Session *session) {
    if (!entry || !session) {
        return -1;
    }

    int rc = pthread_mutex_lock(&g_subs_m);
    if (rc != 0) {
        fprintf(stderr, "[SUBS] add: pthread_mutex_lock failed: %s\n", strerror(rc));
        return -1;
    }

    EntrySubsNode **pp = find_entry_pp(entry);
    if (!*pp) {
        *pp = (EntrySubsNode *)calloc(1, sizeof(EntrySubsNode));
        if (!*pp) {
            (void)pthread_mutex_unlock(&g_subs_m);
            return -1;
        }
        (*pp)->entry = entry;
        (*pp)->head = NULL;
        (*pp)->count = 0;
        (*pp)->next = NULL;
    }

    EntrySubsNode *en = *pp;
    for (SubSessionNode *cur = en->head; cur; cur = cur->next) {
        if (cur->s == session) {
            (void)pthread_mutex_unlock(&g_subs_m);
            return 0;
        }
    }

    SubSessionNode *node = (SubSessionNode *)malloc(sizeof(SubSessionNode));
    if (!node) {
        (void)pthread_mutex_unlock(&g_subs_m);
        return -1;
    }
    node->s = session;
    node->next = en->head;
    en->head = node;
    en->count++;

    (void)pthread_mutex_unlock(&g_subs_m);
    return 0;
}

int subscriber_remove(CacheEntry *entry, Session *session) {
    if (!entry || !session) {
        return -1;
    }

    int rc = pthread_mutex_lock(&g_subs_m);
    if (rc != 0) {
        fprintf(stderr, "[SUBS] remove: pthread_mutex_lock failed: %s\n", strerror(rc));
        return -1;
    }

    EntrySubsNode **pp = find_entry_pp(entry);
    if (!*pp) {
        (void)pthread_mutex_unlock(&g_subs_m);
        return 0;
    }

    EntrySubsNode *en = *pp;
    SubSessionNode **sp = &en->head;
    while (*sp) {
        if ((*sp)->s == session) {
            SubSessionNode *victim = *sp;
            *sp = victim->next;
            free(victim);
            en->count--;
            break;
        }
        sp = &((*sp)->next);
    }

    if (en->count <= 0) {
        *pp = en->next;
        free_session_list(en->head);
        free(en);
    }

    (void)pthread_mutex_unlock(&g_subs_m);
    return 0;
}

int subscriber_count(CacheEntry *entry) {
    if (!entry) return 0;

    int rc = pthread_mutex_lock(&g_subs_m);
    if (rc != 0) {
        fprintf(stderr, "[SUBS] count: pthread_mutex_lock failed: %s\n", strerror(rc));
        return 0;
    }

    EntrySubsNode **pp = find_entry_pp(entry);
    int out = (*pp) ? (*pp)->count : 0;

    (void)pthread_mutex_unlock(&g_subs_m);
    return out;
}

int subscriber_snapshot(CacheEntry *entry, Session ***out_sessions) {
    if (out_sessions) {
        *out_sessions = NULL;
    }
    if (!entry || !out_sessions) {
        return -1;
    }

    int rc = pthread_mutex_lock(&g_subs_m);
    if (rc != 0) {
        fprintf(stderr, "[SUBS] snapshot: pthread_mutex_lock failed: %s\n", strerror(rc));
        return -1;
    }

    EntrySubsNode **pp = find_entry_pp(entry);
    EntrySubsNode *en = (*pp);
    int n = en ? en->count : 0;
    if (n <= 0) {
        (void)pthread_mutex_unlock(&g_subs_m);
        return 0;
    }

    Session **arr = (Session **)malloc((size_t)n * sizeof(Session *));
    if (!arr) {
        (void)pthread_mutex_unlock(&g_subs_m);
        return -1;
    }

    int i = 0;
    for (SubSessionNode *cur = en->head; cur && i < n; cur = cur->next) {
        arr[i++] = cur->s;
    }

    (void)pthread_mutex_unlock(&g_subs_m);

    *out_sessions = arr;
    return i;
}

void subscriber_forget_entry(CacheEntry *entry) {
    if (!entry) return;

    int rc = pthread_mutex_lock(&g_subs_m);
    if (rc != 0) {
        fprintf(stderr, "[SUBS] forget_entry: pthread_mutex_lock failed: %s\n", strerror(rc));
        return;
    }

    EntrySubsNode **pp = find_entry_pp(entry);
    if (*pp) {
        EntrySubsNode *en = *pp;
        *pp = en->next;
        free_session_list(en->head);
        free(en);
    }

    (void)pthread_mutex_unlock(&g_subs_m);
}


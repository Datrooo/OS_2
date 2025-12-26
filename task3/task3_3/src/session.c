#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <errno.h> 

#include "session.h"
#include "cache.h"
#include "subscribers.h"

#define SESSION_HEADER_BUF_SIZE 2048

Session *session_new(int fd) {
    Session *s = calloc(1, sizeof(Session));
    if (!s) return NULL;
    
    s->fd = fd;
    s->state = SESSION_REQ_RECV;
    s->req_len = 0;
    s->cursor = 0;
    s->entry = NULL;
    s->read_active = 0;
    s->write_active = 0;
    s->closed = 0;
    s->id = 0;
    
    s->method = NULL;
    s->target = NULL;
    s->http_version = NULL;
    s->host = NULL;

    s->header_sent = 0;
    s->header_len = 0;
    s->header_sent_bytes = 0;
    s->header_buf = (char *)malloc(SESSION_HEADER_BUF_SIZE);
    s->header_buf = calloc(1, SESSION_HEADER_BUF_SIZE);
    if (!s->header_buf) {
        free(s);
        return NULL;
    }
    
    return s;
}

void session_free(Session *s) {
    if (!s) return;
    
    if (s->fd >= 0) {
        close(s->fd);
        s->fd = -1;
    }
    
    if (s->method) free(s->method);
    if (s->target) free(s->target);
    if (s->http_version) free(s->http_version);
    if (s->host) free(s->host);

    if (s->header_buf) {
        free(s->header_buf);
        s->header_buf = NULL;
    }
    
    if (s->entry) {
        session_detach_entry(s);
    }
    
    free(s);
}

int session_attach_entry(Session *s, CacheEntry *entry) {
    if (!s || !entry) return -1;
    
    s->entry = entry;
    s->cursor = 0;

    if (subscriber_add(entry, s) != 0) {
        s->entry = NULL;
        s->cursor = 0;
        return -1;
    }
    return 0;
}

void session_detach_entry(Session *s) {
    if (!s || !s->entry) return;
    
    CacheEntry *entry = s->entry;

    if (subscriber_remove(entry, s) != 0) {
        fprintf(stderr, "[Session %lu] subscriber_remove failed\n", s->id);
    }

    s->entry = NULL;
    s->cursor = 0;

    pthread_mutex_lock(&entry->m);
    int should_remove = (entry->no_cache && entry->is_completed && entry->subs_count == 0);
    pthread_mutex_unlock(&entry->m);

    if (should_remove) {
        cache_remove_entry(entry);
    }
}
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <errno.h> 

#include "session.h"
#include "cache.h"

#define SESSION_HEADER_BUF_SIZE 2048

Session *session_new(int fd) {
    Session *s = (Session *)malloc(sizeof(Session));
    if (!s) return NULL;
    
    memset(s, 0, sizeof(Session));
    
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
    s->header_buf = (char *)malloc(SESSION_HEADER_BUF_SIZE);
    if (s->header_buf) {
        memset(s->header_buf, 0, SESSION_HEADER_BUF_SIZE);
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
    
    SubNode *sub = (SubNode *)malloc(sizeof(SubNode));
    if (!sub) return -1;
    
    sub->s = s;
    sub->hh_next = entry->subs;
    entry->subs = sub;
    entry->subs_count++;
    
    fprintf(stdout, "[Session %lu] Attached to cache entry (ID: %lu, subscribers: %d)\n",
            s->id, entry->id, entry->subs_count);
    
    return 0;
}

void session_detach_entry(Session *s) {
    if (!s || !s->entry) return;
    
    CacheEntry *entry = s->entry;
    
    SubNode **node = &entry->subs;
    while (*node) {
        if ((*node)->s == s) {
            SubNode *tmp = *node;
            *node = (*node)->hh_next;
            free(tmp);
            entry->subs_count--;
            
            fprintf(stdout, "[Session %lu] Detached from cache entry (ID: %lu, remaining: %d)\n",
                    s->id, entry->id, entry->subs_count);
            break;
        }
        node = &((*node)->hh_next);
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

int session_send_response_header(Session *s, CacheEntry *entry) {
    if (!s || !entry) return -1;
    
    int status = cache_entry_get_status(entry);
    const char *content_type = cache_entry_get_content_type(entry);
    
    char response[1024];
    int len = snprintf(response, sizeof(response),
        "HTTP/1.0 %d OK\r\n"
        "Content-Type: %s\r\n"
        "Connection: close\r\n"
        "\r\n",
        status > 0 ? status : 200,
        content_type ? content_type : "text/html");
    
    if (len < 0 || len >= (int)sizeof(response)) {
        return -1;
    }
    
    ssize_t sent = send(s->fd, response, len, MSG_NOSIGNAL);
    if (sent < 0) {
        fprintf(stderr, "[Session %lu] Failed to send response header\n", s->id);
        return -1;
    }
    
    fprintf(stdout, "[Session %lu] Sent response header (%zd bytes)\n", s->id, sent);
    
    return 0;
}

int session_send_cached_data(Session *s) {
    if (!s || !s->entry) return -1;
    
    CacheEntry *entry = s->entry;
    
    uint8_t *data = NULL;
    size_t size = 0;
    
    int result = cache_get_chunk(entry, s->cursor, &data, &size);
    
    if (result != 0) {
        if (entry->is_completed) {
            return 1;
        } else {
            return 0;
        }
    }
    
    if (size == 0) {
        return 0;
    }
    
    ssize_t sent = send(s->fd, data, size, MSG_NOSIGNAL);
    if (sent < 0) {
        fprintf(stderr, "[Session %lu] Failed to send data\n", s->id);
        return -1;
    }
    
    s->cursor += sent;
    
    fprintf(stdout, "[Session %lu] Sent %zd bytes (cursor: %zu / %zu)\n",
            s->id, sent, s->cursor, entry->produced);
    
    if (sent < (ssize_t)size) {
        return 0;
    }
    
    return 0;
}

uint64_t session_get_id(Session *s) {
    if (!s) return 0;
    return s->id;
}
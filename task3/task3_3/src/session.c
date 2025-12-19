#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "session.h"

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
    
    if (s->entry) {
        session_detach_entry(s);
    }
    
    free(s);
}

int session_attach_entry(Session *s, CacheEntry *entry) {
    if (!s || !entry) return -1;
    
    s->entry = entry;
    s->cursor = 0;
    
    /* TODO: добавить в entry->subs */
    
    return 0;
}

void session_detach_entry(Session *s) {
    if (!s || !s->entry) return;
    
    /* TODO: удалить из entry->subs */
    
    s->entry = NULL;
    s->cursor = 0;
}

int session_send_response_header(Session *s, CacheEntry *entry) {
    if (!s || !entry) return -1;
    
    /* TODO: отправить HTTP заголовок */
    
    return 0;
}

int session_send_cached_data(Session *s) {
    if (!s || !s->entry) return -1;
    
    /* TODO: отправить данные из кэша */
    
    return 0;
}

uint64_t session_get_id(Session *s) {
    if (!s) return 0;
    return s->id;
}
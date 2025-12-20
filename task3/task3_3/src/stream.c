#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <errno.h>
#include <sys/socket.h>


#include "stream.h"
#include "cache.h"


static void format_http_date(char *buf, size_t buf_len) {
    time_t now = time(NULL);
    struct tm *tm_info = gmtime(&now);
    strftime(buf, buf_len, "%a, %d %b %Y %H:%M:%S GMT", tm_info);
}

int stream_format_response_header(CacheEntry *entry, 
                                  char *buf, size_t buf_len,
                                  size_t *out_len) {
    if (!entry || !buf || buf_len < 256) {
        return -1;
    }
    
    char date_buf[64];
    format_http_date(date_buf, sizeof(date_buf));
    
    size_t content_length = 0;
    
    if (entry->is_completed) {
        content_length = entry->produced;
    } else {
        content_length = entry->content_length;
    }
    
    int len = snprintf(buf, buf_len,
        "HTTP/1.0 %d %s\r\n"
        "Content-Type: %s\r\n"
        "Content-Length: %zu\r\n"
        "Connection: close\r\n"
        "Date: %s\r\n"
        "Server: CachingProxy/1.0\r\n"
        "\r\n",
        entry->http_status,
        entry->http_status == 200 ? "OK" : 
        entry->http_status == 404 ? "Not Found" :
        entry->http_status == 500 ? "Internal Server Error" :
        "Unknown",
        strlen(entry->content_type) > 0 ? entry->content_type : "application/octet-stream",
        content_length,
        date_buf);
    
    if (len < 0 || len >= (int)buf_len) {
        fprintf(stderr, "[STREAM] Header formatting failed\n");
        return -1;
    }
    
    *out_len = len;
    
    fprintf(stdout, "[STREAM] Formatted header (%zu bytes):\n", *out_len);
    fprintf(stdout, "[STREAM] Status: %d, Content-Type: %s, Length: %zu\n",
            entry->http_status, entry->content_type, content_length);
    
    return 0;
}


int stream_send_header(Session *session) {
    if (!session || !session->entry || session->header_sent) {
        return -1;
    }
    
    CacheEntry *entry = session->entry;
    
    if (session->header_len == 0) {
        if (stream_format_response_header(entry, 
                                          session->header_buf, 
                                          sizeof(session->header_buf),
                                          &session->header_len) < 0) {
            return -1;
        }
    }
    
    ssize_t sent = send(session->fd, session->header_buf, session->header_len, MSG_NOSIGNAL);
    
    if (sent < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            return 0;
        }
        fprintf(stderr, "[Session %lu] Failed to send header: %s\n", 
                session->id, strerror(errno));
        return -1;
    }
    
    if (sent > 0) {
        fprintf(stdout, "[Session %lu] Sent response header (%zd bytes)\n", 
                session->id, sent);
        session->header_sent = 1;
    }
    
    return 0;
}

int stream_send_body(Session *session) {
    if (!session || !session->entry || !session->header_sent) {
        return -1;
    }
    
    CacheEntry *entry = session->entry;
    
    uint8_t *data = NULL;
    size_t size = 0;
    
    int result = cache_get_chunk(entry, session->cursor, &data, &size);
    
    if (result < 0) {
        fprintf(stderr, "[Session %lu] Failed to get chunk at cursor %zu\n",
                session->id, session->cursor);
        return -1;
    }
    
    if (result == 0) {
        if (entry->is_completed) {
            return 1;
        } else {
            return 0;
        }
    }
    
    if (size == 0) {
        return 0;
    }
    
    ssize_t sent = send(session->fd, data, size, MSG_NOSIGNAL);
    
    if (sent < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            return 0;
        }
        fprintf(stderr, "[Session %lu] Failed to send data: %s\n",
                session->id, strerror(errno));
        return -1;
    }
    
    if (sent > 0) {
        session->cursor += sent;
        fprintf(stdout, "[Session %lu] Sent %zd bytes (total: %zu / %zu)\n",
                session->id, sent, session->cursor, entry->produced);
    }
    
    if ((size_t)sent < size) {
        return 0;
    }
    
    return 0;
}



size_t stream_get_content_length(CacheEntry *entry) {
    if (!entry) {
        return 0;
    }
    
    pthread_mutex_lock(&entry->m);
    
    size_t len = 0;
    
    if (entry->is_completed) {
        len = entry->produced;
    } else if (entry->content_length > 0) {
        len = entry->content_length;
    }
    
    pthread_mutex_unlock(&entry->m);
    
    return len;
}

int stream_can_send_more(Session *session) {
    if (!session || !session->entry) {
        return 0;
    }
    
    CacheEntry *entry = session->entry;
    
    pthread_mutex_lock(&entry->m);
    
    int can_send = (session->cursor < entry->produced) || 
                   (!entry->is_completed && entry->produced > session->cursor);
    
    pthread_mutex_unlock(&entry->m);
    
    return can_send;
}
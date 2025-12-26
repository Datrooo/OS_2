#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <errno.h>
#include <sys/socket.h>


#include "stream.h"
#include "cache.h"

#define STREAM_HEADER_BUF_SIZE 2048


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
    
    int http_status = 200;
    const char *content_type = "application/octet-stream";
    size_t produced = 0;
    int is_completed = 0;
    size_t content_length = 0;

    pthread_mutex_lock(&entry->m);
    http_status = entry->http_status > 0 ? entry->http_status : 200;
    content_type = (entry->content_type && entry->content_type[0]) ? entry->content_type : "application/octet-stream";
    produced = entry->produced;
    is_completed = entry->is_completed;
    content_length = entry->content_length;
    pthread_mutex_unlock(&entry->m);

    if (is_completed) {
        content_length = produced;
    }
    
    int len;
    const char *reason =
        http_status == 200 ? "OK" :
        http_status == 404 ? "Not Found" :
        http_status == 500 ? "Internal Server Error" :
        "Unknown";

    if (is_completed && content_length > 0) {
        len = snprintf(buf, buf_len,
            "HTTP/1.0 %d %s\r\n"
            "Content-Type: %s\r\n"
            "Content-Length: %zu\r\n"
            "Connection: close\r\n"
            "Date: %s\r\n"
            "Server: CachingProxy/1.0\r\n"
            "\r\n",
            http_status, reason, content_type, content_length, date_buf);
    } else {
        // Streaming: omit Content-Length (unknown until complete)
        len = snprintf(buf, buf_len,
            "HTTP/1.0 %d %s\r\n"
            "Content-Type: %s\r\n"
            "Connection: close\r\n"
            "Date: %s\r\n"
            "Server: CachingProxy/1.0\r\n"
            "\r\n",
            http_status, reason, content_type, date_buf);
    }
    
    if (len < 0 || len >= (int)buf_len) {
        fprintf(stderr, "[STREAM] Header formatting failed\n");
        return -1;
    }
    
    *out_len = len;
    
    fprintf(stdout, "[STREAM] Formatted header (%zu bytes):\n", *out_len);
    fprintf(stdout, "[STREAM] Status: %d, Content-Type: %s, Length: %zu\n",
            http_status, content_type, content_length);
    
    return 0;
}


int stream_send_header(Session *session) {
    if (!session || !session->entry) {
        return -1;
    }
    if (session->header_sent) {
        return 0;
    }
    
    CacheEntry *entry = session->entry;

    pthread_mutex_lock(&entry->m);
    int header_ready = entry->header_ready;
    size_t origin_hdr_len = entry->resp_header_len;
    const char *origin_hdr = entry->resp_header;
    pthread_mutex_unlock(&entry->m);

    if (!header_ready) {
        // Wait until downloader parses origin response header.
        return 0;
    }
    
    if (session->header_len == 0) {
        if (origin_hdr && origin_hdr_len > 0) {
            free(session->header_buf);
            session->header_buf = (char *)malloc(origin_hdr_len);
            if (!session->header_buf) {
                return -1;
            }
            memcpy(session->header_buf, origin_hdr, origin_hdr_len);
            session->header_len = origin_hdr_len;
            session->header_sent_bytes = 0;
        } else {
            if (!session->header_buf) {
                session->header_buf = (char *)malloc(STREAM_HEADER_BUF_SIZE);
                if (!session->header_buf) {
                    return -1;
                }
            }
            if (stream_format_response_header(entry,
                                              session->header_buf,
                                              STREAM_HEADER_BUF_SIZE,
                                              &session->header_len) < 0) {
                return -1;
            }
            session->header_sent_bytes = 0;
        }
    }

    if (session->header_sent_bytes >= session->header_len) {
        session->header_sent = 1;
        return 0;
    }

    const char *p = session->header_buf + session->header_sent_bytes;
    size_t remaining = session->header_len - session->header_sent_bytes;
    ssize_t sent = send(session->fd, p, remaining, MSG_NOSIGNAL);
    
    if (sent < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            return 0;
        }
        fprintf(stderr, "[Session %lu] Failed to send header: %s\n", 
                session->id, strerror(errno));
        return -1;
    }
    
    if (sent == 0) {
        return 0;
    }

    session->header_sent_bytes += (size_t)sent;
    fprintf(stdout, "[Session %lu] Sent response header (%zu/%zu bytes)\n",
            session->id, session->header_sent_bytes, session->header_len);

    if (session->header_sent_bytes >= session->header_len) {
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
    
    if (result == 1) {
        // No data at current cursor
        pthread_mutex_lock(&entry->m);
        int completed = entry->is_completed;
        size_t produced = entry->produced;
        pthread_mutex_unlock(&entry->m);

        if (completed && session->cursor >= produced) {
            return 1;
        }
        return 0;
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
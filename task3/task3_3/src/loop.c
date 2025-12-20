#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <ev.h>

#include "loop.h"
#include "session.h"
#include "cache.h"
#include "parse.h"
#include "key_builder.h"
#include "net.h"
#include "downloader.h"


static struct ev_loop *g_loop = NULL;
static int g_listen_fd = -1;
static ev_io g_listen_watcher;
static ev_async g_async_watcher;

static uint64_t g_session_counter = 0;

typedef struct SessionNode {
    Session *sess;
    struct SessionNode *next;
} SessionNode;

static SessionNode *g_sessions = NULL;

static void session_list_add(Session *s) {
    SessionNode *node = (SessionNode *)malloc(sizeof(SessionNode));
    if (!node) return;
    node->sess = s;
    node->next = g_sessions;
    g_sessions = node;
}

static void session_list_remove(Session *s) {
    SessionNode **pp = &g_sessions;
    while (*pp) {
        if ((*pp)->sess == s) {
            SessionNode *tmp = *pp;
            *pp = (*pp)->next;
            free(tmp);
            return;
        }
        pp = &((*pp)->next);
    }
}

static void async_callback(struct ev_loop *loop, ev_async *w, int revents) {
    (void)w;
    (void)revents;
    
    SessionNode *node = g_sessions;
    while (node) {
        Session *s = node->sess;
        if (s->state == SESSION_STREAMING && s->entry && !s->write_active) {
            ev_io_start(loop, &s->write_w);
            s->write_active = 1;
        }
        node = node->next;
    }
}

static void client_read_callback(struct ev_loop *loop, ev_io *w, int revents) {
    (void)revents;
    
    Session *s = (Session *)w->data;
    int fd = s->fd;
    
    if (s->state != SESSION_REQ_RECV) {
        return;
    }
    
    size_t available = sizeof(s->reqbuf) - s->req_len;
    if (available == 0) {
        fprintf(stderr, "[Session %lu] Request buffer overflow\n", s->id);
        s->state = SESSION_ERROR;
        ev_io_stop(loop, &s->read_w);
        if (s->write_active) ev_io_stop(loop, &s->write_w);
        return;
    }
    
    ssize_t n = read(fd, s->reqbuf + s->req_len, available);
    if (n < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            return;  // пока нет данных
        }
        fprintf(stderr, "[Session %lu] Read error: %s\n", s->id, strerror(errno));
        s->state = SESSION_ERROR;
        ev_io_stop(loop, &s->read_w);
        return;
    }
    
    if (n == 0) {
        fprintf(stderr, "[Session %lu] Client closed connection\n", s->id);
        s->state = SESSION_DONE;
        ev_io_stop(loop, &s->read_w);
        return;
    }
    
    s->req_len += n;
    
    const char *header_end = strstr(s->reqbuf, "\r\n\r\n");
    if (!header_end) {
        // не все данные получили
        return;
    }
    
    size_t headers_len = (header_end - s->reqbuf) + 4;
    
    char *method = NULL;
    char *target = NULL;
    char *http_version = NULL;
    size_t headers_end = 0;
    
    int parse_result = parse_http_request(s->reqbuf, s->req_len,
                                          &method, &target, &http_version,
                                          &headers_end);
    
    if (parse_result <= 0) {
        fprintf(stderr, "[Session %lu] Failed to parse HTTP request\n", s->id);
        s->state = SESSION_ERROR;
        ev_io_stop(loop, &s->read_w);
        return;
    }
    
    fprintf(stdout, "[Session %lu] Request: %s %s %s\n", s->id, method, target, http_version);
    
    s->method = method;
    s->target = target;
    s->http_version = http_version;
    
    s->host = parse_host_header(s->reqbuf, headers_len);
    if (!s->host) {
        fprintf(stderr, "[Session %lu] No Host header found\n", s->id);
        s->state = SESSION_ERROR;
        ev_io_stop(loop, &s->read_w);
        return;
    }
    
    fprintf(stdout, "[Session %lu] Host: %s\n", s->id, s->host);
    
    int port = 80;
    char *host_only = s->host;
    
    char *port_pos = strchr(s->host, ':');
    if (port_pos) {
        *port_pos = '\0';
        host_only = s->host;
        port = atoi(port_pos + 1);
        *port_pos = ':';
    }
    
    CacheKey *cache_key = build_cache_key(host_only, target, port);
    if (!cache_key) {
        fprintf(stderr, "[Session %lu] Failed to build cache key\n", s->id);
        s->state = SESSION_ERROR;
        ev_io_stop(loop, &s->read_w);
        return;
    }
    
    fprintf(stdout, "[Session %lu] Cache key: %s\n", s->id, cache_key->s);
    
    CacheEntry *entry = cache_lookup_or_create(cache_key);
    free(cache_key->s);
    free(cache_key);
    
    if (!entry) {
        fprintf(stderr, "[Session %lu] Failed to create cache entry\n", s->id);
        s->state = SESSION_ERROR;
        ev_io_stop(loop, &s->read_w);
        return;
    }
    
    if (session_attach_entry(s, entry) != 0) {
        fprintf(stderr, "[Session %lu] Failed to attach to cache entry\n", s->id);
        s->state = SESSION_ERROR;
        ev_io_stop(loop, &s->read_w);
        return;
    }
        
    if (!entry->is_downloader_running && !entry->is_completed) {
        fprintf(stdout, "[Session %lu] Enqueueing download task for entry %lu\n",
                s->id, entry->id);
        
        if (downloader_enqueue(entry, 1) != 0) {
            fprintf(stderr, "[Session %lu] Failed to enqueue download\n", s->id);
        }
        
        pthread_mutex_lock(&entry->m);
        entry->is_downloader_running = 1;
        pthread_mutex_unlock(&entry->m);
    }
    
    fprintf(stdout, "[Session %lu] Parsed successfully, transitioning to STREAMING\n", s->id);
    
    s->state = SESSION_STREAMING;
    ev_io_stop(loop, &s->read_w);
    s->read_active = 0;
    
    ev_io_init(&s->write_w, client_write_callback, s->fd, EV_WRITE);
    s->write_w.data = (void *)s;
    ev_io_start(loop, &s->write_w);
    s->write_active = 1;
}

static void client_write_callback(struct ev_loop *loop, ev_io *w, int revents) {
    (void)revents;
    
    Session *s = (Session *)w->data;
    
    if (s->state == SESSION_STREAMING && s->entry) {
        CacheEntry *entry = s->entry;
        
        if (s->cursor == 0 && entry->http_status > 0) {
            if (session_send_response_header(s, entry) < 0) {
                s->state = SESSION_ERROR;
                ev_io_stop(loop, &s->write_w);
                s->write_active = 0;
                return;
            }
        }
        
        int send_result = session_send_cached_data(s);
        
        if (send_result < 0) {
            fprintf(stderr, "[Session %lu] Error sending data\n", s->id);
            s->state = SESSION_ERROR;
        } else if (send_result == 1 && entry->is_completed) {
            fprintf(stdout, "[Session %lu] Streaming complete (sent %zu bytes)\n", 
                    s->id, s->cursor);
            s->state = SESSION_DONE;
        } else if (send_result == 0 && !entry->is_completed && s->cursor >= entry->produced) {
            // ждем еще данных
            ev_io_stop(loop, &s->write_w);
            s->write_active = 0;
            return;
        }
    }
    
    if (s->state == SESSION_DONE || s->state == SESSION_ERROR) {
        ev_io_stop(loop, &s->write_w);
        s->write_active = 0;
        close(s->fd);
        s->fd = -1;
        s->closed = 1;
        session_list_remove(s);
        session_free(s);
    }
}

static void accept_callback(struct ev_loop *loop, ev_io *w, int revents) {
    (void)revents;
    
    struct sockaddr_in client_addr;
    socklen_t client_addr_len = sizeof(client_addr);
    
    int client_fd = accept(g_listen_fd, (struct sockaddr *)&client_addr, &client_addr_len);
    if (client_fd < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            return;
        }
        fprintf(stderr, "Accept error: %s\n", strerror(errno));
        return;
    }
    
    Session *s = session_new(client_fd);
    if (!s) {
        fprintf(stderr, "Failed to create session\n");
        close(client_fd);
        return;
    }
    
    s->id = ++g_session_counter;
    
    char client_ip[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &client_addr.sin_addr, client_ip, sizeof(client_ip));
    fprintf(stdout, "[Session %lu] New connection from %s:%d\n", 
            s->id, client_ip, ntohs(client_addr.sin_port));
    
    int flags = fcntl(client_fd, F_GETFL, 0);
    fcntl(client_fd, F_SETFL, flags | O_NONBLOCK);
    
    ev_io_init(&s->read_w, client_read_callback, client_fd, EV_READ);
    s->read_w.data = (void *)s;
    ev_io_start(loop, &s->read_w);
    s->read_active = 1;
    
    session_list_add(s);
}

int loop_init(int listen_port) {
    g_listen_fd = net_listen(listen_port);
    if (g_listen_fd < 0) {
        fprintf(stderr, "Failed to create listen socket\n");
        return -1;
    }
    
    fprintf(stdout, "Listen socket created: fd=%d, port=%d\n", g_listen_fd, listen_port);
    
    int flags = fcntl(g_listen_fd, F_GETFL, 0);
    fcntl(g_listen_fd, F_SETFL, flags | O_NONBLOCK);
    
    g_loop = ev_loop_new(0);
    if (!g_loop) {
        fprintf(stderr, "Failed to create event loop\n");
        close(g_listen_fd);
        return -1;
    }
    
    ev_io_init(&g_listen_watcher, accept_callback, g_listen_fd, EV_READ);
    ev_io_start(g_loop, &g_listen_watcher);
    
    ev_async_init(&g_async_watcher, async_callback);
    ev_async_start(g_loop, &g_async_watcher);
    
    return 0;
}

int loop_run(void) {
    if (!g_loop) return -1;
    
    ev_run(g_loop, 0);
    
    return 0;
}

void loop_stop(void) {
    if (!g_loop) return;
    
    ev_break(g_loop, EVBREAK_ALL);
}

struct ev_loop *loop_get(void) {
    return g_loop;
}

void loop_notify_dirty(void) {
    if (!g_loop) return;
    ev_async_send(g_loop, &g_async_watcher);
}
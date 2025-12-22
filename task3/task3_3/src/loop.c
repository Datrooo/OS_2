#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <signal.h>
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
#include "dirty.h"
#include "stream.h"

static char *xstrdup(const char *s) {
    if (!s) return NULL;
    size_t n = strlen(s) + 1;
    char *out = (char *)malloc(n);
    if (!out) return NULL;
    memcpy(out, s, n);
    return out;
}

static char *normalize_target_origin_form(const char *target) {
    if (!target || !target[0]) {
        return xstrdup("/");
    }

    if (target[0] == '/') {
        return xstrdup(target);
    }

    const char *p = NULL;
    if (strncmp(target, "http://", 7) == 0) {
        p = target + 7;
    } else if (strncmp(target, "https://", 8) == 0) {
        p = target + 8;
    }

    if (!p) {
        return xstrdup(target);
    }

    const char *path = strchr(p, '/');
    if (!path) {
        return xstrdup("/");
    }

    return xstrdup(path);
}

static struct ev_loop *g_loop = NULL;
static int g_listen_fd = -1;
static ev_io g_listen_watcher;
static ev_async g_async_watcher;
static ev_signal g_sigint_watcher;
static ev_signal g_sigterm_watcher;

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
    int processed = dirty_process_all();
    if (processed <= 0) {
        return;
    }

    SessionNode *node = g_sessions;
    while (node) {
        Session *s = node->sess;
        if (s->state == SESSION_STREAMING && s->entry && !s->write_active) {
            int should_wake = 0;

            pthread_mutex_lock(&s->entry->m);
            int header_ready = s->entry->header_ready;
            int completed = s->entry->is_completed;
            pthread_mutex_unlock(&s->entry->m);

            if (!s->header_sent && header_ready) {
                should_wake = 1;
            }

            if (!should_wake && stream_can_send_more(s)) {
                should_wake = 1;
            } else if (!should_wake) {
                should_wake = completed;
            }

            if (should_wake) {
                ev_io_start(loop, &s->write_w);
                s->write_active = 1;
            }
        }
        node = node->next;
    }
}

static void signal_callback(struct ev_loop *loop, ev_signal *w, int revents) {
    (void)w;
    (void)revents;
    fprintf(stderr, "\nSignal received, shutting down...\n");
    ev_break(loop, EVBREAK_ALL);
}

static void client_write_callback(struct ev_loop *loop, ev_io *w, int revents);

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
    char *normalized_target = normalize_target_origin_form(target);
    free(target);
    s->target = normalized_target;
    s->http_version = http_version;

    if (!s->method || strcmp(s->method, "GET") != 0) {
        static const char resp[] =
            "HTTP/1.0 501 Not Implemented\r\n"
            "Connection: close\r\n"
            "\r\n";
        (void)send(fd, resp, sizeof(resp) - 1, MSG_NOSIGNAL);
        fprintf(stderr, "[Session %lu] Unsupported method: %s\n", s->id, s->method ? s->method : "(null)");
        s->state = SESSION_ERROR;
        ev_io_stop(loop, &s->read_w);
        return;
    }
    
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
    
    CacheKey *cache_key = build_cache_key(host_only, s->target, port);
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

        if (!s->header_sent) {
            pthread_mutex_lock(&entry->m);
            int header_ready = entry->header_ready;
            pthread_mutex_unlock(&entry->m);
            if (!header_ready) {
                ev_io_stop(loop, &s->write_w);
                s->write_active = 0;
                return;
            }
        }
        
        if (!s->header_sent) {
            if (stream_send_header(s) < 0) {
                fprintf(stderr, "[Session %lu] Failed to send header\n", s->id);
                s->state = SESSION_ERROR;
                ev_io_stop(loop, &s->write_w);
                s->write_active = 0;
                return;
            }
            
            if (!s->header_sent) {
                // wait headder
                return;
            }
        }
        
        int send_result = stream_send_body(s);
        
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
    (void)w;
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

int loop_init(const char *bind_ip, int listen_port) {
    g_listen_fd = net_listen_on(bind_ip, listen_port);
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
        g_listen_fd = -1;
        return -1;
    }
    
    ev_io_init(&g_listen_watcher, accept_callback, g_listen_fd, EV_READ);
    ev_io_start(g_loop, &g_listen_watcher);
    
    ev_async_init(&g_async_watcher, async_callback);
    ev_async_start(g_loop, &g_async_watcher);

    ev_signal_init(&g_sigint_watcher, signal_callback, SIGINT);
    ev_signal_start(g_loop, &g_sigint_watcher);
    ev_signal_init(&g_sigterm_watcher, signal_callback, SIGTERM);
    ev_signal_start(g_loop, &g_sigterm_watcher);
    
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

void loop_shutdown(void) {
    if (g_loop) {
        ev_signal_stop(g_loop, &g_sigterm_watcher);
        ev_signal_stop(g_loop, &g_sigint_watcher);
        ev_async_stop(g_loop, &g_async_watcher);
        ev_io_stop(g_loop, &g_listen_watcher);
        ev_break(g_loop, EVBREAK_ALL);
        ev_loop_destroy(g_loop);
        g_loop = NULL;
    }

    if (g_listen_fd >= 0) {
        close(g_listen_fd);
        g_listen_fd = -1;
    }
}

struct ev_loop *loop_get(void) {
    return g_loop;
}

void loop_notify_dirty(void) {
    if (!g_loop) return;
    ev_async_send(g_loop, &g_async_watcher);
}
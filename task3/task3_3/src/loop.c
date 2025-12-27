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
#include <pthread.h>
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
#include "subscribers.h"

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

typedef struct SessionNode {
    Session *sess;
    struct SessionNode *next;
} SessionNode;

typedef struct ConnectWaitQueue {
    pthread_mutex_t m;
    struct ConnectWaitReq *head;
    struct ConnectWaitReq *tail;
    struct ConnectWaitReq *active;
} ConnectWaitQueue;

typedef struct LoopState {
    struct ev_loop *loop;
    int listen_fd;
    ev_io listen_watcher;
    ev_async async_watcher;
    ev_signal sigint_watcher;
    ev_signal sigterm_watcher;
    uint64_t session_counter;
    SessionNode *sessions;
    ConnectWaitQueue connect_q;
    int shutting_down;
} LoopState;

static LoopState g_state = {
    .loop = NULL,
    .listen_fd = -1,
    .session_counter = 0,
    .sessions = NULL,
    .connect_q = {
        .m = PTHREAD_MUTEX_INITIALIZER,
        .head = NULL,
        .tail = NULL,
        .active = NULL,
    },
    .shutting_down = 0,
};

int loop_is_shutting_down(void) {
    return g_state.shutting_down;
}

static void session_list_add(Session *s) {
    SessionNode *node = (SessionNode *)malloc(sizeof(SessionNode));
    if (!node) return;
    node->sess = s;
    node->next = g_state.sessions;
    g_state.sessions = node;
}

static void session_list_remove(Session *s) {
    SessionNode **pp = &g_state.sessions;
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

typedef struct ConnectWaitReq {
    int fd;
    double timeout_sec;
    int done;
    int soerr;
    int timed_out;
    pthread_mutex_t m;
    pthread_cond_t cv;
    ev_io io;
    ev_timer timer;
    struct ConnectWaitReq *next;
} ConnectWaitReq;

static void connect_wait_finish(ConnectWaitReq *req, int timed_out, int soerr);

static void connect_wait_active_add(ConnectWaitReq *req) {
    if (!req) return;
    pthread_mutex_lock(&g_state.connect_q.m);
    req->next = (ConnectWaitReq *)g_state.connect_q.active;
    g_state.connect_q.active = req;
    pthread_mutex_unlock(&g_state.connect_q.m);
}

static void connect_wait_active_remove(ConnectWaitReq *req) {
    if (!req) return;
    pthread_mutex_lock(&g_state.connect_q.m);
    ConnectWaitReq **pp = (ConnectWaitReq **)&g_state.connect_q.active;
    while (*pp) {
        if (*pp == req) {
            *pp = (*pp)->next;
            break;
        }
        pp = &((*pp)->next);
    }
    pthread_mutex_unlock(&g_state.connect_q.m);
}

static void connect_wait_cancel_list(struct ev_loop *loop, ConnectWaitReq *list, int stop_watchers) {
    while (list) {
        ConnectWaitReq *req = list;
        list = list->next;
        req->next = NULL;
        if (stop_watchers && loop) {
            ev_io_stop(loop, &req->io);
            ev_timer_stop(loop, &req->timer);
        }
        connect_wait_finish(req, 1, ECANCELED);
    }
}

static void connect_wait_finish(ConnectWaitReq *req, int timed_out, int soerr) {
    if (!req) return;
    pthread_mutex_lock(&req->m);
    req->done = 1;
    req->timed_out = timed_out;
    req->soerr = soerr;
    pthread_cond_signal(&req->cv);
    pthread_mutex_unlock(&req->m);
}

static void connect_wait_io_cb(EV_P_ ev_io *w, int revents) {
    (void)revents;
    ConnectWaitReq *req = (ConnectWaitReq *)w->data;
    if (!req) return;

    int soerr = 0;
    socklen_t slen = sizeof(soerr);
    if (getsockopt(req->fd, SOL_SOCKET, SO_ERROR, &soerr, &slen) != 0) {
        soerr = errno;
    }

    ev_io_stop(EV_A_ &req->io);
    ev_timer_stop(EV_A_ &req->timer);
    connect_wait_active_remove(req);
    connect_wait_finish(req, 0, soerr);
}

static void connect_wait_timer_cb(EV_P_ ev_timer *w, int revents) {
    (void)revents;
    ConnectWaitReq *req = (ConnectWaitReq *)w->data;
    if (!req) return;
    ev_io_stop(EV_A_ &req->io);
    ev_timer_stop(EV_A_ &req->timer);
    connect_wait_active_remove(req);
    connect_wait_finish(req, 1, ETIMEDOUT);
}

static void async_callback(struct ev_loop *loop, ev_async *w, int revents) {
    (void)w;
    (void)revents;
    CacheEntry **dirty_entries = NULL;
    int processed = dirty_process_all(&dirty_entries);

    ConnectWaitReq *local_head = NULL;
    pthread_mutex_lock(&g_state.connect_q.m);
    local_head = (ConnectWaitReq *)g_state.connect_q.head;
    g_state.connect_q.head = NULL;
    g_state.connect_q.tail = NULL;
    pthread_mutex_unlock(&g_state.connect_q.m);

    while (local_head) {
        ConnectWaitReq *req = local_head;
        local_head = local_head->next;
        req->next = NULL;

        ev_io_init(&req->io, connect_wait_io_cb, req->fd, EV_WRITE);
        req->io.data = req;
        ev_timer_init(&req->timer, connect_wait_timer_cb, req->timeout_sec, 0.0);
        req->timer.data = req;

        ev_io_start(loop, &req->io);
        ev_timer_start(loop, &req->timer);

        connect_wait_active_add(req);
    }

    if (processed <= 0) {
        if (dirty_entries) {
            free(dirty_entries);
        }
        return;
    }

    for (int i = 0; i < processed; i++) {
        CacheEntry *entry = dirty_entries[i];
        if (!entry) continue;

        Session **sessions = NULL;
        int n = subscriber_snapshot(entry, &sessions);
        if (n <= 0) {
            free(sessions);
            continue;
        }

        for (int j = 0; j < n; j++) {
            Session *s = sessions[j];
            if (!s) continue;

            if (s->state != SESSION_STREAMING || s->entry != entry || s->write_active) {
                continue;
            }

            int should_wake = 0;
            pthread_mutex_lock(&entry->m);
            int header_ready = entry->header_ready;
            int completed = entry->is_completed;
            pthread_mutex_unlock(&entry->m);

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

        free(sessions);

        cache_entry_release(entry);
    }

    free(dirty_entries);
}

static void signal_callback(struct ev_loop *loop, ev_signal *w, int revents) {
    (void)w;
    (void)revents;
    fprintf(stderr, "\nSignal received, shutting down...\n");
    g_state.shutting_down = 1;

    // Cancel connect-waits to avoid deadlocks (downloader threads may be blocked in loop_wait_connect()).
    ConnectWaitReq *queued = NULL;
    ConnectWaitReq *active = NULL;
    pthread_mutex_lock(&g_state.connect_q.m);
    queued = (ConnectWaitReq *)g_state.connect_q.head;
    active = (ConnectWaitReq *)g_state.connect_q.active;
    g_state.connect_q.head = NULL;
    g_state.connect_q.tail = NULL;
    g_state.connect_q.active = NULL;
    pthread_mutex_unlock(&g_state.connect_q.m);

    connect_wait_cancel_list(loop, active, 1);
    connect_wait_cancel_list(NULL, queued, 0);

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
    
    char *method = NULL;
    char *target = NULL;
    char *http_version = NULL;
    size_t headers_end = 0;
    
    int parse_result = parse_http_request(s->reqbuf, s->req_len,
                                          &method, &target, &http_version,
                                          &headers_end);

    if (parse_result == 0) {
        // need more data
        return;
    }
    if (parse_result < 0) {
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
        ssize_t wr = send(fd, resp, sizeof(resp) - 1, MSG_NOSIGNAL);
        if (wr < 0 && errno != EPIPE && errno != ECONNRESET) {
            fprintf(stderr, "[Session %lu] send(501) failed: %s\n", s->id, strerror(errno));
        }
        fprintf(stderr, "[Session %lu] Unsupported method: %s\n", s->id, s->method ? s->method : "(null)");
        s->state = SESSION_ERROR;
        ev_io_stop(loop, &s->read_w);
        return;
    }
    
    s->host = parse_host_header(s->reqbuf, headers_end);
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
    
    int client_fd = accept(g_state.listen_fd, (struct sockaddr *)&client_addr, &client_addr_len);
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
    
    s->id = ++g_state.session_counter;
    
    char client_ip[INET_ADDRSTRLEN];
    if (!inet_ntop(AF_INET, &client_addr.sin_addr, client_ip, sizeof(client_ip))) {
        snprintf(client_ip, sizeof(client_ip), "?");
    }
    fprintf(stdout, "[Session %lu] New connection from %s:%d\n", 
            s->id, client_ip, ntohs(client_addr.sin_port));

    if (net_set_nonblocking(client_fd, "ACCEPT") != 0) {
        close(client_fd);
        s->fd = -1;
        session_free(s);
        return;
    }
    
    ev_io_init(&s->read_w, client_read_callback, client_fd, EV_READ);
    s->read_w.data = (void *)s;
    ev_io_start(loop, &s->read_w);
    s->read_active = 1;
    
    session_list_add(s);
}

int loop_create(const char *bind_ip, int listen_port) {
    g_state.shutting_down = 0;
    g_state.listen_fd = net_listen_on(bind_ip, listen_port);
    if (g_state.listen_fd < 0) {
        fprintf(stderr, "Failed to create listen socket\n");
        return -1;
    }
    
    fprintf(stdout, "Listen socket created: fd=%d, port=%d\n", g_state.listen_fd, listen_port);

    if (net_set_nonblocking(g_state.listen_fd, "LISTEN") != 0) {
        close(g_state.listen_fd);
        g_state.listen_fd = -1;
        return -1;
    }
    
    g_state.loop = ev_loop_new(0);
    if (!g_state.loop) {
        fprintf(stderr, "Failed to create event loop\n");
        close(g_state.listen_fd);
        g_state.listen_fd = -1;
        return -1;
    }
    
    ev_io_init(&g_state.listen_watcher, accept_callback, g_state.listen_fd, EV_READ);
    ev_io_start(g_state.loop, &g_state.listen_watcher);
    
    ev_async_init(&g_state.async_watcher, async_callback);
    ev_async_start(g_state.loop, &g_state.async_watcher);

    ev_signal_init(&g_state.sigint_watcher, signal_callback, SIGINT);
    ev_signal_start(g_state.loop, &g_state.sigint_watcher);
    ev_signal_init(&g_state.sigterm_watcher, signal_callback, SIGTERM);
    ev_signal_start(g_state.loop, &g_state.sigterm_watcher);
    
    return 0;
}

int loop_run(void) {
    if (!g_state.loop) return -1;
    
    ev_run(g_state.loop, 0);
    
    return 0;
}

void loop_destroy(void) {
    g_state.shutting_down = 1;

    // Close all active client sessions (best-effort).
    if (g_state.loop) {
        SessionNode *node = g_state.sessions;
        while (node) {
            Session *s = node->sess;
            if (s) {
                if (s->read_active) {
                    ev_io_stop(g_state.loop, &s->read_w);
                    s->read_active = 0;
                }
                if (s->write_active) {
                    ev_io_stop(g_state.loop, &s->write_w);
                    s->write_active = 0;
                }
                session_free(s);
            }
            node = node->next;
        }

        // Free the list nodes.
        node = g_state.sessions;
        while (node) {
            SessionNode *next = node->next;
            free(node);
            node = next;
        }
        g_state.sessions = NULL;
    }

    // Cancel any outstanding connect waits.
    ConnectWaitReq *queued = NULL;
    ConnectWaitReq *active = NULL;
    pthread_mutex_lock(&g_state.connect_q.m);
    queued = (ConnectWaitReq *)g_state.connect_q.head;
    active = (ConnectWaitReq *)g_state.connect_q.active;
    g_state.connect_q.head = NULL;
    g_state.connect_q.tail = NULL;
    g_state.connect_q.active = NULL;
    pthread_mutex_unlock(&g_state.connect_q.m);
    if (g_state.loop) {
        connect_wait_cancel_list(g_state.loop, active, 1);
    } else {
        connect_wait_cancel_list(NULL, active, 0);
    }
    connect_wait_cancel_list(NULL, queued, 0);

    if (g_state.loop) {
        ev_signal_stop(g_state.loop, &g_state.sigterm_watcher);
        ev_signal_stop(g_state.loop, &g_state.sigint_watcher);
        ev_async_stop(g_state.loop, &g_state.async_watcher);
        ev_io_stop(g_state.loop, &g_state.listen_watcher);
        ev_break(g_state.loop, EVBREAK_ALL);
        ev_loop_destroy(g_state.loop);
        g_state.loop = NULL;
    }

    if (g_state.listen_fd >= 0) {
        close(g_state.listen_fd);
        g_state.listen_fd = -1;
    }
}

void loop_notify_dirty(void) {
    if (!g_state.loop) return;
    ev_async_send(g_state.loop, &g_state.async_watcher);
}

int loop_wait_connect(int fd, double timeout_sec, int *out_soerr) {
    if (out_soerr) *out_soerr = 0;
    if (!g_state.loop || fd < 0) {
        return -1;
    }

    ConnectWaitReq *req = (ConnectWaitReq *)calloc(1, sizeof(*req));
    if (!req) return -1;
    req->fd = fd;
    req->timeout_sec = timeout_sec;
    req->done = 0;
    req->soerr = 0;
    req->timed_out = 0;
    pthread_mutex_init(&req->m, NULL);
    pthread_cond_init(&req->cv, NULL);
    req->next = NULL;

    pthread_mutex_lock(&g_state.connect_q.m);
    if (!g_state.connect_q.tail) {
        g_state.connect_q.head = req;
        g_state.connect_q.tail = req;
    } else {
        ((ConnectWaitReq *)g_state.connect_q.tail)->next = req;
        g_state.connect_q.tail = req;
    }
    pthread_mutex_unlock(&g_state.connect_q.m);

    ev_async_send(g_state.loop, &g_state.async_watcher);

    pthread_mutex_lock(&req->m);
    while (!req->done) {
        pthread_cond_wait(&req->cv, &req->m);
    }
    int timed_out = req->timed_out;
    int soerr = req->soerr;
    pthread_mutex_unlock(&req->m);

    pthread_cond_destroy(&req->cv);
    pthread_mutex_destroy(&req->m);
    free(req);

    if (out_soerr) *out_soerr = soerr;
    return timed_out ? 1 : 0;
}
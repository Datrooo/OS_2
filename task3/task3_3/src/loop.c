#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <arpa/inet.h>
#include <ev.h>

#include "loop.h"
#include "session.h"
#include "cache.h"
#include "parse.h"
#include "key_builder.h"
#include "net.h"

static struct ev_loop *loop = NULL;
static int listen_fd = -1;
static ev_io listen_watcher;
static ev_async async_watcher;

static uint64_t session_counter = 0;

typedef struct SessionNode {
    Session *sess;
    struct SessionNode *next;
} SessionNode;

static SessionNode *sessions = NULL;

static void session_list_add(Session *s) {
    SessionNode *node = (SessionNode *)malloc(sizeof(SessionNode));
    if (!node) return;
    node->sess = s;
    node->next = sessions;
    sessions = node;
}

static void session_list_remove(Session *s) {
    SessionNode **node = &sessions;
    while (*node) {
        if ((*node)->sess == s) {
            SessionNode *tmp = *node;
            *node = (*node)->next;
            free(tmp);
            return;
        }
        node = &((*node)->next);
    }
}

// пробуждение от скачивателей
static void async_callback(struct ev_loop *loop, ev_async *w, int revents) {
    (void)loop;
    (void)w;
    (void)revents;
    
    /* TODO: обработка dirty entries и уведомление subscribers */
}

// получение http запроса
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
    
    // TODO: lookup/create cache entry и переход в STREAMING
    
    fprintf(stdout, "[Session %lu] Parsed successfully, transitioning to STREAMING\n", s->id);
    
    s->state = SESSION_STREAMING;
    ev_io_stop(loop, &s->read_w);
    
    ev_io_init(&s->write_w, client_write_callback, fd, EV_WRITE);
    s->write_w.data = (void *)s;
    ev_io_start(loop, &s->write_w);
    s->write_active = 1;
}

static void client_write_callback(struct ev_loop *loop, ev_io *w, int revents) {
    (void)revents;
    
    Session *s = (Session *)w->data;
    int fd = s->fd;
    
    if (s->state == SESSION_STREAMING) {
        /* TODO: отправлять данные из кэша */
        fprintf(stdout, "[Session %lu] STREAMING: cursor=%zu\n", s->id, s->cursor);
        
        const char *response = "HTTP/1.0 200 OK\r\nContent-Type: text/plain\r\nConnection: close\r\n\r\ni love pg_lab\n";
        size_t response_len = strlen(response);
        
        ssize_t sent = send(fd, response, response_len, MSG_NOSIGNAL);
        if (sent < 0) {
            fprintf(stderr, "[Session %lu] Send error: %s\n", s->id, strerror(errno));
            s->state = SESSION_ERROR;
        } else {
            fprintf(stdout, "[Session %lu] Sent %zd bytes\n", s->id, sent);
            s->state = SESSION_DONE;
        }
    }
    
    if (s->state == SESSION_DONE || s->state == SESSION_ERROR) {
        ev_io_stop(loop, &s->write_w);
        s->write_active = 0;
        close(fd);
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
    
    int client_fd = accept(listen_fd, (struct sockaddr *)&client_addr, &client_addr_len);
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
    
    s->id = ++session_counter;
    
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
    listen_fd = net_listen(listen_port);
    if (listen_fd < 0) {
        fprintf(stderr, "Failed to create listen socket\n");
        return -1;
    }
    
    fprintf(stdout, "Listen socket created: fd=%d, port=%d\n", listen_fd, listen_port);
    
    int flags = fcntl(listen_fd, F_GETFL, 0);
    fcntl(listen_fd, F_SETFL, flags | O_NONBLOCK);
    
    loop = ev_loop_new(0);
    if (!loop) {
        fprintf(stderr, "Failed to create event loop\n");
        close(listen_fd);
        return -1;
    }
    
    ev_io_init(&listen_watcher, accept_callback, listen_fd, EV_READ);
    ev_io_start(loop, &listen_watcher);
    
    ev_async_init(&async_watcher, async_callback);
    ev_async_start(loop, &async_watcher);
    
    return 0;
}

int loop_run(void) {
    if (!loop) return -1;
    
    ev_run(loop, 0);  // 0 = run until no event
    
    return 0;
}

void loop_stop(void) {
    if (!loop) return;
    
    ev_break(loop, EVBREAK_ALL);
}

struct ev_loop *loop_get(void) {
    return loop;
}

void loop_notify_dirty(void) {
    if (!loop) return;
    ev_async_send(loop, &async_watcher);
}
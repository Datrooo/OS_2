#define _POSIX_C_SOURCE 200112L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <errno.h>
#include <stdint.h>
#include <fcntl.h>
#include <ev.h>

#include "downloader.h"
#include "cache.h"
#include "dirty.h"
#include "net.h"

#define DOWNLOADER_THREAD_COUNT 4
#define DOWNLOADER_QUEUE_SIZE 1024
#define DOWNLOAD_BUFFER_SIZE 4096
#define ORIGIN_HEADER_MAX 16384
#define CONNECT_TIMEOUT_SEC 5.0

typedef struct {
    struct ev_loop *loop;
    ev_io io;
    ev_timer timer;
    int fd;
    int fired;
    int timed_out;
    int soerr;
} ConnectWaitCtx;

static void connect_wait_stop(ConnectWaitCtx *ctx) {
    if (!ctx || !ctx->loop) return;
    ev_io_stop(ctx->loop, &ctx->io);
    ev_timer_stop(ctx->loop, &ctx->timer);
}

static void connect_wait_io_cb(EV_P_ ev_io *w, int revents) {
    (void)revents;
    ConnectWaitCtx *ctx = (ConnectWaitCtx *)w->data;
    if (!ctx) {
        ev_break(EV_A_ EVBREAK_ONE);
        return;
    }

    int soerr = 0;
    socklen_t slen = sizeof(soerr);
    if (getsockopt(ctx->fd, SOL_SOCKET, SO_ERROR, &soerr, &slen) != 0) {
        ctx->soerr = errno;
    } else {
        ctx->soerr = soerr;
    }

    ctx->fired = 1;
    ctx->timed_out = 0;
    connect_wait_stop(ctx);
    ev_break(EV_A_ EVBREAK_ONE);
}

static void connect_wait_timer_cb(EV_P_ ev_timer *w, int revents) {
    (void)revents;
    ConnectWaitCtx *ctx = (ConnectWaitCtx *)w->data;
    if (!ctx) {
        ev_break(EV_A_ EVBREAK_ONE);
        return;
    }
    ctx->fired = 1;
    ctx->timed_out = 1;
    ctx->soerr = ETIMEDOUT;
    connect_wait_stop(ctx);
    ev_break(EV_A_ EVBREAK_ONE);
}

static int wait_connect_writable_libev(int fd, double timeout_sec, int *out_soerr) {
    if (out_soerr) *out_soerr = 0;

    struct ev_loop *loop = ev_loop_new(0);
    if (!loop) {
        return -1;
    }

    ConnectWaitCtx ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.loop = loop;
    ctx.fd = fd;
    ctx.soerr = 0;

    ev_io_init(&ctx.io, connect_wait_io_cb, fd, EV_WRITE);
    ctx.io.data = &ctx;
    ev_timer_init(&ctx.timer, connect_wait_timer_cb, timeout_sec, 0.0);
    ctx.timer.data = &ctx;

    ev_io_start(loop, &ctx.io);
    ev_timer_start(loop, &ctx.timer);
    ev_run(loop, 0);

    ev_loop_destroy(loop);

    if (!ctx.fired) {
        return -1;
    }

    if (out_soerr) {
        *out_soerr = ctx.soerr;
    }

    return ctx.timed_out ? 1 : 0;
}

static int find_header_end_len(const char *buf, size_t len, size_t *out_header_len) {
    if (!buf || len < 4) return 0;
    for (size_t i = 0; i + 3 < len; i++) {
        if (buf[i] == '\r' && buf[i + 1] == '\n' && buf[i + 2] == '\r' && buf[i + 3] == '\n') {
            *out_header_len = i + 4;
            return 1;
        }
    }
    return 0;
}

typedef struct {
    DownloadTask tasks[DOWNLOADER_QUEUE_SIZE];
    int head;
    int tail;
    int count;
    pthread_mutex_t m;
    pthread_cond_t cond;
    int shutdown_flag;
} TaskQueue;

static TaskQueue g_task_queue = {
    .head = 0,
    .tail = 0,
    .count = 0,
    .shutdown_flag = 0
};

static pthread_t g_downloader_threads[DOWNLOADER_THREAD_COUNT];
static int g_downloader_initialized = 0;


static int task_queue_enqueue(DownloadTask *task) {
    pthread_mutex_lock(&g_task_queue.m);
    
    if (g_task_queue.count >= DOWNLOADER_QUEUE_SIZE) {
        pthread_mutex_unlock(&g_task_queue.m);
        fprintf(stderr, "[DL] Task queue full\n");
        return -1;
    }
    
    g_task_queue.tasks[g_task_queue.tail] = *task;
    g_task_queue.tail = (g_task_queue.tail + 1) % DOWNLOADER_QUEUE_SIZE;
    g_task_queue.count++;
    
    pthread_cond_signal(&g_task_queue.cond);
    pthread_mutex_unlock(&g_task_queue.m);
    
    return 0;
}

static int task_queue_dequeue(DownloadTask *task) {
    pthread_mutex_lock(&g_task_queue.m);
    
    while (g_task_queue.count == 0 && !g_task_queue.shutdown_flag) {
        pthread_cond_wait(&g_task_queue.cond, &g_task_queue.m);
    }
    
    if (g_task_queue.count == 0) {
        pthread_mutex_unlock(&g_task_queue.m);
        return -1;
    }
    
    *task = g_task_queue.tasks[g_task_queue.head];
    g_task_queue.head = (g_task_queue.head + 1) % DOWNLOADER_QUEUE_SIZE;
    g_task_queue.count--;
    
    pthread_mutex_unlock(&g_task_queue.m);
    
    return 0;
}

static int task_queue_size(void) {
    pthread_mutex_lock(&g_task_queue.m);
    int size = g_task_queue.count;
    pthread_mutex_unlock(&g_task_queue.m);
    return size;
}

static int origin_connect(const char *host, int port) {
    fprintf(stdout, "[DL] Connecting to %s:%d\n", host, port);

    char port_str[16];
    snprintf(port_str, sizeof(port_str), "%d", port);

    struct addrinfo hints;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    struct addrinfo *res = NULL;
    int rc = getaddrinfo(host, port_str, &hints, &res);
    if (rc != 0) {
        fprintf(stderr, "[DL] Failed to resolve %s:%d: %s\n", host, port, gai_strerror(rc));
        return -1;
    }

    int fd = -1;
    for (struct addrinfo *ai = res; ai; ai = ai->ai_next) {
        char addr_str[INET6_ADDRSTRLEN] = {0};
        void *addr_ptr = NULL;
        if (ai->ai_family == AF_INET) {
            addr_ptr = &((struct sockaddr_in *)ai->ai_addr)->sin_addr;
        } else if (ai->ai_family == AF_INET6) {
            addr_ptr = &((struct sockaddr_in6 *)ai->ai_addr)->sin6_addr;
        }
        if (addr_ptr) {
            if (!inet_ntop(ai->ai_family, addr_ptr, addr_str, sizeof(addr_str))) {
                snprintf(addr_str, sizeof(addr_str), "(inet_ntop failed)");
            }
            fprintf(stdout, "[DL] Trying %s (%s)\n", addr_str, (ai->ai_family == AF_INET6) ? "IPv6" : "IPv4");
        }

        fd = socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
        if (fd < 0) {
            continue;
        }

        int flags = fcntl(fd, F_GETFL, 0);
        if (flags < 0) {
            fprintf(stderr, "[DL] fcntl(F_GETFL) failed: %s\n", strerror(errno));
            close(fd);
            fd = -1;
            continue;
        }
        if (net_set_nonblocking(fd, "DL") != 0) {
            close(fd);
            fd = -1;
            continue;
        }

        int rc_conn = connect(fd, ai->ai_addr, ai->ai_addrlen);
        if (rc_conn == 0) {
            if (fcntl(fd, F_SETFL, flags) != 0) {
                fprintf(stderr, "[DL] fcntl(restore blocking) failed: %s\n", strerror(errno));
                close(fd);
                fd = -1;
                continue;
            }
            break;
        }

        if (rc_conn < 0 && errno == EINPROGRESS) {
            int soerr = 0;
            int wrc = wait_connect_writable_libev(fd, CONNECT_TIMEOUT_SEC, &soerr);
            if (wrc < 0) {
                fprintf(stderr, "[DL] libev connect-wait failed\n");
            } else if (wrc > 0) {
                fprintf(stderr, "[DL] connect timeout\n");
            } else if (soerr == 0) {
                if (fcntl(fd, F_SETFL, flags) != 0) {
                    fprintf(stderr, "[DL] fcntl(restore blocking) failed: %s\n", strerror(errno));
                    close(fd);
                    fd = -1;
                    continue;
                }
                break; // success
            } else {
                fprintf(stderr, "[DL] connect failed (SO_ERROR=%d: %s)\n", soerr, strerror(soerr));
            }
        }

        close(fd);
        fd = -1;
    }

    freeaddrinfo(res);

    if (fd < 0) {
        fprintf(stderr, "[DL] Connect failed to %s:%d: %s\n", host, port, strerror(errno));
        return -1;
    }

    fprintf(stdout, "[DL] Connected to %s:%d (fd=%d)\n", host, port, fd);
    return fd;
}

static int origin_send_request(int fd, const char *target, const char *host) {
    char request[1024];
    int len = snprintf(request, sizeof(request),
        "GET %s HTTP/1.0\r\n"
        "Host: %s\r\n"
        "Connection: close\r\n"
        "\r\n",
        target, host);
    
    if (len < 0 || len >= (int)sizeof(request)) {
        return -1;
    }
    
    fprintf(stdout, "[DL] Sending request (%d bytes)\n", len);
    
    size_t off = 0;
    while (off < (size_t)len) {
        ssize_t sent = send(fd, request + off, (size_t)len - off, MSG_NOSIGNAL);
        if (sent < 0) {
            if (errno == EINTR) {
                continue;
            }
            fprintf(stderr, "[DL] Send failed: %s\n", strerror(errno));
            return -1;
        }
        if (sent == 0) {
            fprintf(stderr, "[DL] Send returned 0\n");
            return -1;
        }
        off += (size_t)sent;
    }
    
    return 0;
}

static void parse_status_and_content_type(const char *hdr, size_t hdr_len,
                                        int *out_status, char **out_content_type) {
    if (!hdr || hdr_len == 0) return;

    char *tmp = (char *)malloc(hdr_len + 1);
    if (!tmp) return;
    memcpy(tmp, hdr, hdr_len);
    tmp[hdr_len] = '\0';

    int status = 0;
    sscanf(tmp, "HTTP/1.%*d %d", &status);
    if (status > 0) {
        *out_status = status;
    }

    const char *ct_start = strstr(tmp, "Content-Type:");
    if (!ct_start) ct_start = strstr(tmp, "content-type:");
    if (ct_start) {
        ct_start += 13;
        while (*ct_start == ' ') ct_start++;
        const char *ct_end = strstr(ct_start, "\r\n");
        if (ct_end && ct_end > ct_start) {
            size_t ct_len = (size_t)(ct_end - ct_start);
            char *ct = (char *)malloc(ct_len + 1);
            if (ct) {
                memcpy(ct, ct_start, ct_len);
                ct[ct_len] = '\0';
                *out_content_type = ct;
            }
        }
    }

    free(tmp);
}

static void *downloader_worker(void *arg) {
    int thread_id = (intptr_t)arg;
    
    fprintf(stdout, "[DL] Worker thread %d started\n", thread_id);
    
    while (1) {
        DownloadTask task;
        
        if (task_queue_dequeue(&task) < 0) {
            break;
        }
        
        if (!task.entry) {
            continue;
        }
        
        CacheEntry *entry = task.entry;
        
        fprintf(stdout, "[DL] Worker %d: Processing entry %lu (%s)\n",
                thread_id, entry->id, entry->key.s);
        
        pthread_mutex_lock(&entry->m);
        entry->is_downloader_running = 1;
        pthread_mutex_unlock(&entry->m);
        
        // Format: "host:port/path?query"
        char host[256] = {0};
        int port = 80;
        char target[512] = "/";
        
        const char *key_str = entry->key.s;
        
        const char *slash = strchr(key_str, '/');
        if (slash) {
            size_t host_len = slash - key_str;
            if (host_len < sizeof(host) - 1) {
                memcpy(host, key_str, host_len);
                host[host_len] = '\0';
            }
            
            if (strlen(slash) < sizeof(target)) {
                strcpy(target, slash);
            }
        } else {
            strcpy(host, key_str);
        }
        
        char *colon = strchr(host, ':');
        if (colon) {
            port = atoi(colon + 1);
            *colon = '\0';
        }
        
        fprintf(stdout, "[DL] Worker %d: host=%s, port=%d, target=%s\n",
                thread_id, host, port, target);
        
        int origin_fd = origin_connect(host, port);
        if (origin_fd < 0) {
            fprintf(stderr, "[DL] Worker %d: Failed to connect to origin\n", thread_id);
            cache_entry_failed(entry);
            pthread_mutex_lock(&entry->m);
            entry->is_downloader_running = 0;
            pthread_mutex_unlock(&entry->m);
            dirty_enqueue(entry);
            continue;
        }
        
        if (origin_send_request(origin_fd, target, host) < 0) {
            fprintf(stderr, "[DL] Worker %d: Failed to send request\n", thread_id);
            cache_entry_failed(entry);
            close(origin_fd);
            pthread_mutex_lock(&entry->m);
            entry->is_downloader_running = 0;
            pthread_mutex_unlock(&entry->m);
            dirty_enqueue(entry);
            continue;
        }
        
        uint8_t buffer[DOWNLOAD_BUFFER_SIZE];
        size_t total_received = 0;
        int header_parsed = 0;
        int response_status = 200;
        char *content_type = NULL;

        char header_buf[ORIGIN_HEADER_MAX];
        size_t header_buf_len = 0;
        
        while (1) {
            ssize_t n = recv(origin_fd, buffer, sizeof(buffer), 0);
            
            if (n < 0) {
                if (errno == EINTR) {
                    continue;
                }
                fprintf(stderr, "[DL] Worker %d: Recv error: %s\n", thread_id, strerror(errno));
                break;
            }
            
            if (n == 0) {
                fprintf(stdout, "[DL] Worker %d: Origin closed connection (total: %zu bytes)\n",
                        thread_id, total_received);
                break;
            }
            
            total_received += n;

            if (!header_parsed) {
                size_t can_copy = (size_t)n;
                if (header_buf_len + can_copy > sizeof(header_buf)) {
                    can_copy = sizeof(header_buf) - header_buf_len;
                }
                if (can_copy > 0) {
                    memcpy(header_buf + header_buf_len, buffer, can_copy);
                    header_buf_len += can_copy;
                }

                size_t header_len = 0;
                int found = find_header_end_len(header_buf, header_buf_len, &header_len);
                if (!found) {
                    if (header_buf_len == sizeof(header_buf)) {
                        // give up header parsing, treat as no-header
                        header_parsed = 1;
                        pthread_mutex_lock(&entry->m);
                        entry->http_status = 200;
                        entry->header_ready = 1;
                        pthread_mutex_unlock(&entry->m);
                    }
                    continue;
                }
                header_parsed = 1;

                parse_status_and_content_type(header_buf, header_len, &response_status, &content_type);
                fprintf(stdout, "[DL] Response status: %d\n", response_status);
                if (content_type) {
                    fprintf(stdout, "[DL] Content-Type: %s\n", content_type);
                }

                // store exact origin header for clients
                pthread_mutex_lock(&entry->m);
                if (entry->resp_header) {
                    free(entry->resp_header);
                    entry->resp_header = NULL;
                }
                entry->resp_header = (char *)malloc(header_len);
                if (entry->resp_header) {
                    memcpy(entry->resp_header, header_buf, header_len);
                    entry->resp_header_len = header_len;
                } else {
                    entry->resp_header_len = 0;
                }
                entry->http_status = response_status;
                entry->header_ready = 1;
                pthread_mutex_unlock(&entry->m);

                size_t spill = header_buf_len - header_len;
                if (spill > 0) {
                    int rc = cache_append_chunk(entry, (const uint8_t *)(header_buf + header_len), spill);
                    if (rc == -2) {
                        fprintf(stderr, "[DL] Entry %lu exceeds cache capacity; aborting\n", entry->id);
                        cache_entry_failed(entry);
                        break;
                    } else if (rc != 0) {
                        fprintf(stderr, "[DL] Failed to append spill chunk\n");
                        cache_entry_failed(entry);
                        break;
                    }
                }

                if ((size_t)n > can_copy) {
                    size_t rem = (size_t)n - can_copy;
                    int rc = cache_append_chunk(entry, buffer + can_copy, rem);
                    if (rc == -2) {
                        fprintf(stderr, "[DL] Entry %lu exceeds cache capacity; aborting\n", entry->id);
                        cache_entry_failed(entry);
                        break;
                    } else if (rc != 0) {
                        fprintf(stderr, "[DL] Failed to append data chunk\n");
                        cache_entry_failed(entry);
                        break;
                    }
                }
            } else {
                int rc = cache_append_chunk(entry, buffer, n);
                if (rc == -2) {
                    fprintf(stderr, "[DL] Entry %lu exceeds cache capacity; aborting\n", entry->id);
                    cache_entry_failed(entry);
                    break;
                } else if (rc != 0) {
                    fprintf(stderr, "[DL] Failed to append data chunk\n");
                    cache_entry_failed(entry);
                    break;
                }
            }
            
            pthread_mutex_lock(&entry->m);
            entry->is_dirty = 1;
            pthread_mutex_unlock(&entry->m);
            dirty_enqueue(entry);
        }
        
        if (header_parsed) {
            cache_entry_complete(entry, response_status, content_type);
        } else {
            cache_entry_failed(entry);
        }
        
        if (content_type) {
            free(content_type);
        }
        
        close(origin_fd);
        
        pthread_mutex_lock(&entry->m);
        entry->is_downloader_running = 0;
        pthread_mutex_unlock(&entry->m);
        
        dirty_enqueue(entry);
        
        fprintf(stdout, "[DL] Worker %d: Entry %lu completed (status: %d, %zu bytes)\n",
                thread_id, entry->id, response_status, total_received);
    }
    
    fprintf(stdout, "[DL] Worker thread %d shutting down\n", thread_id);
    
    return NULL;
}

int downloader_init(void) {
    if (g_downloader_initialized) {
        return 0;
    }
    
    if (pthread_mutex_init(&g_task_queue.m, NULL) != 0) {
        fprintf(stderr, "[DL] pthread_mutex_init failed\n");
        return -1;
    }
    if (pthread_cond_init(&g_task_queue.cond, NULL) != 0) {
        fprintf(stderr, "[DL] pthread_cond_init failed\n");
        pthread_mutex_destroy(&g_task_queue.m);
        return -1;
    }
    
    g_task_queue.shutdown_flag = 0;
    
    for (int i = 0; i < DOWNLOADER_THREAD_COUNT; i++) {
        if (pthread_create(&g_downloader_threads[i], NULL, downloader_worker, (void *)(intptr_t)i) != 0) {
            fprintf(stderr, "[DL] Failed to create worker thread %d\n", i);
            return -1;
        }
    }
    
    g_downloader_initialized = 1;
    
    fprintf(stdout, "[DL] Downloader pool initialized (%d threads)\n", DOWNLOADER_THREAD_COUNT);
    
    return 0;
}

int downloader_enqueue(CacheEntry *entry, int urgency) {
    if (!entry) {
        return -1;
    }
    
    DownloadTask task = {
        .entry = entry,
        .urgency = urgency
    };
    
    fprintf(stdout, "[DL] Enqueued download task for %s (urgency: %d)\n",
            entry->key.s, urgency);
    
    return task_queue_enqueue(&task);
}

void downloader_shutdown(void) {
    if (!g_downloader_initialized) {
        return;
    }
    
    fprintf(stdout, "[DL] Shutting down downloader pool\n");
    
    pthread_mutex_lock(&g_task_queue.m);
    g_task_queue.shutdown_flag = 1;
    pthread_cond_broadcast(&g_task_queue.cond);
    pthread_mutex_unlock(&g_task_queue.m);
    
    for (int i = 0; i < DOWNLOADER_THREAD_COUNT; i++) {
        pthread_join(g_downloader_threads[i], NULL);
    }
    
    fprintf(stdout, "[DL] Downloader pool shut down\n");
    
    g_downloader_initialized = 0;
}

int downloader_get_queue_size(void) {
    return task_queue_size();
}
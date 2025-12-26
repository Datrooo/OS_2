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

#include "downloader.h"
#include "cache.h"
#include "dirty.h"
#include "net.h"
#include "loop.h"

#define DOWNLOADER_THREAD_COUNT 4
#define DOWNLOADER_QUEUE_SIZE 1024
#define DOWNLOAD_BUFFER_SIZE 4096
#define ORIGIN_HEADER_MAX 16384
#define CONNECT_TIMEOUT_SEC 5.0


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

typedef struct DownloaderState {
    TaskQueue q;
    pthread_t threads[DOWNLOADER_THREAD_COUNT];
    int initialized;
} DownloaderState;

static DownloaderState g_downloader = {
    .q = {
        .head = 0,
        .tail = 0,
        .count = 0,
        .shutdown_flag = 0,
    },
    .initialized = 0,
};


static int task_queue_enqueue(DownloadTask *task) {
    pthread_mutex_lock(&g_downloader.q.m);
    
    if (g_downloader.q.count >= DOWNLOADER_QUEUE_SIZE) {
        pthread_mutex_unlock(&g_downloader.q.m);
        fprintf(stderr, "[DL] Task queue full\n");
        return -1;
    }
    
    g_downloader.q.tasks[g_downloader.q.tail] = *task;
    g_downloader.q.tail = (g_downloader.q.tail + 1) % DOWNLOADER_QUEUE_SIZE;
    g_downloader.q.count++;
    
    pthread_cond_signal(&g_downloader.q.cond);
    pthread_mutex_unlock(&g_downloader.q.m);
    
    return 0;
}

static int task_queue_dequeue(DownloadTask *task) {
    pthread_mutex_lock(&g_downloader.q.m);
    
    while (g_downloader.q.count == 0 && !g_downloader.q.shutdown_flag) {
        pthread_cond_wait(&g_downloader.q.cond, &g_downloader.q.m);
    }
    
    if (g_downloader.q.count == 0) {
        pthread_mutex_unlock(&g_downloader.q.m);
        return -1;
    }
    
    *task = g_downloader.q.tasks[g_downloader.q.head];
    g_downloader.q.head = (g_downloader.q.head + 1) % DOWNLOADER_QUEUE_SIZE;
    g_downloader.q.count--;
    
    pthread_mutex_unlock(&g_downloader.q.m);
    
    return 0;
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
            int wrc = loop_wait_connect(fd, CONNECT_TIMEOUT_SEC, &soerr);
            if (wrc < 0) {
                fprintf(stderr, "[DL] loop connect-wait failed\n");
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

static size_t parse_content_length(const char *hdr, size_t hdr_len) {
    if (!hdr || hdr_len == 0) return 0;

    char *tmp = (char *)malloc(hdr_len + 1);
    if (!tmp) return 0;
    memcpy(tmp, hdr, hdr_len);
    tmp[hdr_len] = '\0';

    const char *cl_start = strstr(tmp, "Content-Length:");
    if (!cl_start) cl_start = strstr(tmp, "content-length:");
    size_t out = 0;
    if (cl_start) {
        cl_start += 15;
        while (*cl_start == ' ' || *cl_start == '\t') cl_start++;
        errno = 0;
        unsigned long long v = strtoull(cl_start, NULL, 10);
        if (errno == 0) {
            out = (size_t)v;
        }
    }

    free(tmp);
    return out;
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
        size_t content_length = 0;
        size_t stored_bytes = 0;
        int aborted = 0;
        size_t max_cache_size = cache_get_max_size();

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
                content_length = parse_content_length(header_buf, header_len);
                fprintf(stdout, "[DL] Response status: %d\n", response_status);
                if (content_type) {
                    fprintf(stdout, "[DL] Content-Type: %s\n", content_type);
                }
                if (content_length > 0) {
                    fprintf(stdout, "[DL] Content-Length: %zu\n", content_length);
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
                entry->content_length = content_length;
                entry->header_ready = 1;
                pthread_mutex_unlock(&entry->m);

                if (max_cache_size > 0 && content_length > max_cache_size) {
                    fprintf(stderr,
                            "[DL] Huge object for entry %lu (%s): Content-Length=%zu > cache_max=%zu; aborting\n",
                            entry->id,
                            entry->key.s ? entry->key.s : "(null)",
                            content_length,
                            max_cache_size);
                    cache_entry_failed(entry);
                    aborted = 1;
                    break;
                }

                size_t spill = header_buf_len - header_len;
                if (spill > 0) {
                    if (max_cache_size > 0 && stored_bytes + spill > max_cache_size) {
                        fprintf(stderr,
                                "[DL] Huge object for entry %lu (%s): streamed_bytes=%zu + spill=%zu > cache_max=%zu; aborting\n",
                                entry->id,
                                entry->key.s ? entry->key.s : "(null)",
                                stored_bytes,
                                spill,
                                max_cache_size);
                        cache_entry_failed(entry);
                        aborted = 1;
                        break;
                    }
                    int rc = cache_append_chunk(entry, (const uint8_t *)(header_buf + header_len), spill);
                    if (rc == -2) {
                        fprintf(stderr, "[DL] Entry %lu exceeds cache capacity; aborting\n", entry->id);
                        cache_entry_failed(entry);
                        aborted = 1;
                        break;
                    } else if (rc != 0) {
                        fprintf(stderr, "[DL] Failed to append spill chunk\n");
                        cache_entry_failed(entry);
                        aborted = 1;
                        break;
                    }
                    stored_bytes += spill;
                }

                if ((size_t)n > can_copy) {
                    size_t rem = (size_t)n - can_copy;
                    if (max_cache_size > 0 && stored_bytes + rem > max_cache_size) {
                        fprintf(stderr,
                                "[DL] Huge object for entry %lu (%s): streamed_bytes=%zu + rem=%zu > cache_max=%zu; aborting\n",
                                entry->id,
                                entry->key.s ? entry->key.s : "(null)",
                                stored_bytes,
                                rem,
                                max_cache_size);
                        cache_entry_failed(entry);
                        aborted = 1;
                        break;
                    }
                    int rc = cache_append_chunk(entry, buffer + can_copy, rem);
                    if (rc == -2) {
                        fprintf(stderr, "[DL] Entry %lu exceeds cache capacity; aborting\n", entry->id);
                        cache_entry_failed(entry);
                        aborted = 1;
                        break;
                    } else if (rc != 0) {
                        fprintf(stderr, "[DL] Failed to append data chunk\n");
                        cache_entry_failed(entry);
                        aborted = 1;
                        break;
                    }
                    stored_bytes += rem;
                }
            } else {
                if (max_cache_size > 0 && stored_bytes + (size_t)n > max_cache_size) {
                    fprintf(stderr,
                            "[DL] Huge object for entry %lu (%s): streamed_bytes=%zu + chunk=%zu > cache_max=%zu; aborting\n",
                            entry->id,
                            entry->key.s ? entry->key.s : "(null)",
                            stored_bytes,
                            (size_t)n,
                            max_cache_size);
                    cache_entry_failed(entry);
                    aborted = 1;
                    break;
                }
                int rc = cache_append_chunk(entry, buffer, n);
                if (rc == -2) {
                    fprintf(stderr, "[DL] Entry %lu exceeds cache capacity; aborting\n", entry->id);
                    cache_entry_failed(entry);
                    aborted = 1;
                    break;
                } else if (rc != 0) {
                    fprintf(stderr, "[DL] Failed to append data chunk\n");
                    cache_entry_failed(entry);
                    aborted = 1;
                    break;
                }
                stored_bytes += (size_t)n;
            }
            
            pthread_mutex_lock(&entry->m);
            entry->is_dirty = 1;
            pthread_mutex_unlock(&entry->m);
            dirty_enqueue(entry);
        }

        if (!aborted) {
            if (header_parsed) {
                cache_entry_complete(entry, response_status, content_type);
            } else {
                cache_entry_failed(entry);
            }
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

static int downloader_state_create(DownloaderState *st) {
    if (!st) return -1;
    if (st->initialized) return 0;

    if (pthread_mutex_init(&st->q.m, NULL) != 0) {
        fprintf(stderr, "[DL] pthread_mutex_init failed\n");
        return -1;
    }
    if (pthread_cond_init(&st->q.cond, NULL) != 0) {
        fprintf(stderr, "[DL] pthread_cond_init failed\n");
        pthread_mutex_destroy(&st->q.m);
        return -1;
    }

    st->q.head = 0;
    st->q.tail = 0;
    st->q.count = 0;
    st->q.shutdown_flag = 0;

    for (int i = 0; i < DOWNLOADER_THREAD_COUNT; i++) {
        if (pthread_create(&st->threads[i], NULL, downloader_worker, (void *)(intptr_t)i) != 0) {
            fprintf(stderr, "[DL] Failed to create worker thread %d\n", i);
            st->q.shutdown_flag = 1;
            pthread_cond_broadcast(&st->q.cond);
            for (int j = 0; j < i; j++) {
                pthread_join(st->threads[j], NULL);
            }
            pthread_cond_destroy(&st->q.cond);
            pthread_mutex_destroy(&st->q.m);
            return -1;
        }
    }

    st->initialized = 1;
    fprintf(stdout, "[DL] Downloader pool initialized (%d threads)\n", DOWNLOADER_THREAD_COUNT);
    return 0;
}

static void downloader_state_destroy(DownloaderState *st) {
    if (!st || !st->initialized) {
        return;
    }

    fprintf(stdout, "[DL] Shutting down downloader pool\n");

    pthread_mutex_lock(&st->q.m);
    st->q.shutdown_flag = 1;
    pthread_cond_broadcast(&st->q.cond);
    pthread_mutex_unlock(&st->q.m);

    for (int i = 0; i < DOWNLOADER_THREAD_COUNT; i++) {
        pthread_join(st->threads[i], NULL);
    }

    pthread_cond_destroy(&st->q.cond);
    pthread_mutex_destroy(&st->q.m);

    st->initialized = 0;
    fprintf(stdout, "[DL] Downloader pool shut down\n");
}

int downloader_create(void) {
    return downloader_state_create(&g_downloader);
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

void downloader_destroy(void) {
    downloader_state_destroy(&g_downloader);
}
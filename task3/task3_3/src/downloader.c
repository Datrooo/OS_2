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

#include "downloader.h"
#include "cache.h"
#include "loop.h"

#define DOWNLOADER_THREAD_COUNT 4
#define DOWNLOADER_QUEUE_SIZE 1024
#define DOWNLOAD_BUFFER_SIZE 4096

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
    struct sockaddr_in addr;
    int fd = -1;
    
    fprintf(stdout, "[DL] Connecting to %s:%d\n", host, port);
    
    struct hostent *he = gethostbyname(host);
    if (!he) {
        fprintf(stderr, "[DL] Failed to resolve %s: %s\n", host, hstrerror(h_errno));
        return -1;
    }
    
    fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        fprintf(stderr, "[DL] Socket creation failed: %s\n", strerror(errno));
        return -1;
    }
    
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    memcpy(&addr.sin_addr, he->h_addr_list[0], he->h_length);
    
    if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        fprintf(stderr, "[DL] Connect failed to %s:%d: %s\n", host, port, strerror(errno));
        close(fd);
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
    
    ssize_t sent = send(fd, request, len, MSG_NOSIGNAL);
    if (sent < 0) {
        fprintf(stderr, "[DL] Send failed: %s\n", strerror(errno));
        return -1;
    }
    
    return 0;
}

static int parse_response_header(const char *buf, size_t buf_len,
                                 int *out_status, char **out_content_type,
                                 size_t *out_header_len) {
    const char *header_end = strstr(buf, "\r\n\r\n");
    if (!header_end) {
        return 0;
    }
    
    *out_header_len = (header_end - buf) + 4;
    
    int status = 0;
    sscanf(buf, "HTTP/1.%*d %d", &status);
    *out_status = status;
    
    fprintf(stdout, "[DL] Response status: %d\n", status);
    
    const char *ct_start = strstr(buf, "Content-Type:");
    if (ct_start) {
        ct_start += 13;
        while (*ct_start == ' ') ct_start++;
        
        const char *ct_end = strchr(ct_start, '\r');
        if (ct_end) {
            size_t ct_len = ct_end - ct_start;
            *out_content_type = (char *)malloc(ct_len + 1);
            if (*out_content_type) {
                memcpy(*out_content_type, ct_start, ct_len);
                (*out_content_type)[ct_len] = '\0';
                fprintf(stdout, "[DL] Content-Type: %s\n", *out_content_type);
            }
        }
    }
    
    return 1;
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
        
        /* Format: "host:port/path?query" */
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
            loop_notify_dirty();
            continue;
        }
        
        if (origin_send_request(origin_fd, target, host) < 0) {
            fprintf(stderr, "[DL] Worker %d: Failed to send request\n", thread_id);
            cache_entry_failed(entry);
            close(origin_fd);
            pthread_mutex_lock(&entry->m);
            entry->is_downloader_running = 0;
            pthread_mutex_unlock(&entry->m);
            loop_notify_dirty();
            continue;
        }
        
        uint8_t buffer[DOWNLOAD_BUFFER_SIZE];
        size_t total_received = 0;
        int header_parsed = 0;
        int response_status = 200;
        char *content_type = NULL;
        
        while (1) {
            ssize_t n = recv(origin_fd, buffer, sizeof(buffer), 0);
            
            if (n < 0) {
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
                size_t header_len = 0;
                int parse_result = parse_response_header((const char *)buffer, n,
                                                         &response_status, &content_type,
                                                         &header_len);
                if (parse_result > 0) {
                    header_parsed = 1;
                    
                    if (header_len < (size_t)n) {
                        cache_append_chunk(entry, buffer + header_len, n - header_len);
                    }
                } else if (n == (ssize_t)sizeof(buffer)) {
                    header_parsed = 1;
                }
            } else {
                cache_append_chunk(entry, buffer, n);
            }
            
            loop_notify_dirty();
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
        
        loop_notify_dirty();
        
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
    
    pthread_mutex_init(&g_task_queue.m, NULL);
    pthread_cond_init(&g_task_queue.cond, NULL);
    
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
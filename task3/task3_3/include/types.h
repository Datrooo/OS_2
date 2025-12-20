#ifndef __TYPES_H__
#define __TYPES_H__

#include <stdint.h>
#include <stddef.h>
#include <pthread.h>
#include <ev.h>

// ключ кэша: "host:port/path?query"
typedef struct {
    char  *s;      
    size_t len;
} CacheKey;

typedef struct {
    size_t size;
    uint8_t *data;
} Chunk;

typedef struct SubNode {
    struct Session *s;
    SubNode *hh_next;
} SubNode;

typedef struct CacheEntry {
    CacheKey key;
    uint64_t id;
    
    int in_map;
    int is_downloader_running;
    int is_completed;
    int is_failed;
    
    Chunk **chunks;
    int chunks_count;
    int chunks_capacity;
    
    size_t produced;
    
    int http_status;
    char *content_type; 
    size_t content_length;

    
    pthread_mutex_t m;
    int is_dirty;
    
    SubNode *subs; 
    int subs_count;
    
    CacheEntry *lru_prev;
    CacheEntry *lru_next;

    CacheEntry *hash_next;
    size_t bytes_total;
    uint64_t access_time;         // для LRU
    
} CacheEntry;

typedef enum {
    SESSION_REQ_RECV = 1,    // Читаем заголовок запроса
    SESSION_STREAMING = 2,   // Отправляем ответ клиенту
    SESSION_DONE = 3,
    SESSION_ERROR = 4
} SessionState;

typedef struct Session {
    int fd;
    ev_io read_w;
    ev_io write_w;
    
    // Состояние парсинга запроса 
    SessionState state;
    char reqbuf[8192];
    size_t req_len;
    
    char *method;
    char *target;
    char *http_version;
    char *host;
    
    CacheEntry *entry;                     // Указатель на entry при STREAMING
    size_t cursor;                         // Сколько байт мы уже отправили
    
    int read_active;
    int write_active;
    int closed;

    int header_sent;
    char *header_buf;
    size_t header_len;
    
    uint64_t id;
} Session;

typedef struct {
    CacheEntry *entry;
    int urgency;                // 0 = background, 1 = on-demand
} DownloadTask;

// Очередь для GC (pending free)
typedef struct {
    CacheEntry **items;
    int count;
    int capacity;
    pthread_mutex_t m;
} PendingFreeQueue;

#endif
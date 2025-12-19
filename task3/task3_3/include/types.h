#ifndef __TYPES_H__
#define __TYPES_H__

#include <stdint.h>
#include <stddef.h>
#include <pthread.h>
#include <ev.h>


typedef struct {
    char  *s;      
    size_t len;
} CacheKey;

typedef struct {
    size_t size;
    uint8_t *data;
} Chunk;

typedef Hash Hash;

typedef struct SubNode {
    struct Session *s;
    Hash hh;
} SubNode;

typedef struct CacheEntry {
    CacheKey key;
    
    int in_map;
    int is_downloader_running;
    int is_completed;
    int failed;
    
    Chunk **chunks;
    int chunks_count;
    int chunks_capacity;
    
    size_t produced;
    
    int http_status;
    char *content_type; 
    
    pthread_mutex_t m;
    int dirty;
    
    SubNode *subs; 
    int subs_count;
    
    struct CacheEntry *prev;
    struct CacheEntry *next;
    size_t bytes_total;
    uint64_t access_time;         // для LRU
    
} CacheEntry;

typedef struct {
    CacheKey key;
    CacheEntry *entry;
    Hash hh;
} CacheNode;

typedef enum {
    SESSION_REQ_RECV = 1,    // Читаем заголовок запроса
    SESSION_STREAMING = 2,   // Отправляем ответ клиенту
    SESSION_DONE = 3,
    SESSION_ERROR = 4
} SessionState;

typedef struct Session {
    int fd;
    struct ev_io read_w, write_w;
    
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
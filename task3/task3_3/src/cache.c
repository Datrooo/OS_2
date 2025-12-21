#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <time.h>

#include "cache.h"

typedef struct CacheMap {
    CacheEntry **entries;
    int capacity;
    int count;
} CacheMap;

static CacheMap g_cache_map = {NULL, 0, 0};
static CacheEntry *g_lru_head = NULL;
static CacheEntry *g_lru_tail = NULL;
static pthread_mutex_t g_cache_mutex = PTHREAD_MUTEX_INITIALIZER;
static size_t g_cache_max_size = 0;
static size_t g_cache_current_size = 0;

static uint64_t g_entry_id_counter = 0;

static unsigned int hash_djb2(const char *str, size_t len) {
    unsigned int hash = 5381;
    for (size_t i = 0; i < len; i++) {
        hash = ((hash << 5) + hash) + str[i];
    }
    return hash;
}

static CacheEntry *cache_map_find(const CacheKey *key) {
    if (!g_cache_map.entries || g_cache_map.count == 0) {
        return NULL;
    }
    
    unsigned int h = hash_djb2(key->s, key->len);
    int idx = h % g_cache_map.capacity;
    
    CacheEntry *entry = g_cache_map.entries[idx];
    while (entry) {
        if (entry->key.len == key->len && 
            strcmp(entry->key.s, key->s) == 0) {
            return entry;
        }
        entry = entry->hash_next;
    }
    
    return NULL;
}

static int cache_map_insert(CacheEntry *entry) {
    if (!entry || !entry->key.s) return -1;
    
    if (g_cache_map.count >= g_cache_map.capacity * 0.75) {
        int new_capacity = g_cache_map.capacity > 0 ? 
                          g_cache_map.capacity * 2 : 32;
        CacheEntry **new_entries = (CacheEntry **)calloc(new_capacity, sizeof(CacheEntry *));
        if (!new_entries) return -1;
        
        for (int i = 0; i < g_cache_map.capacity; i++) {
            CacheEntry *e = g_cache_map.entries[i];
            while (e) {
                unsigned int h = hash_djb2(e->key.s, e->key.len);
                int new_idx = h % new_capacity;
                CacheEntry *next = e->hash_next;
                e->hash_next = new_entries[new_idx];
                new_entries[new_idx] = e;
                e = next;
            }
        }
        
        free(g_cache_map.entries);
        g_cache_map.entries = new_entries;
        g_cache_map.capacity = new_capacity;
    }
    
    unsigned int h = hash_djb2(entry->key.s, entry->key.len);
    int idx = h % g_cache_map.capacity;
    
    entry->hash_next = g_cache_map.entries[idx];
    g_cache_map.entries[idx] = entry;
    g_cache_map.count++;
    
    return 0;
}

static void cache_map_remove(CacheEntry *entry) {
    if (!entry || !g_cache_map.entries) return;
    
    unsigned int h = hash_djb2(entry->key.s, entry->key.len);
    int idx = h % g_cache_map.capacity;
    
    CacheEntry **pp = &g_cache_map.entries[idx];
    while (*pp) {
        if (*pp == entry) {
            *pp = entry->hash_next;
            g_cache_map.count--;
            return;
        }
        pp = &((*pp)->hash_next);
    }
}

static void lru_add_to_head(CacheEntry *entry) {
    if (!entry) return;
    
    entry->lru_prev = NULL;
    entry->lru_next = g_lru_head;
    
    if (g_lru_head) {
        g_lru_head->lru_prev = entry;
    }
    g_lru_head = entry;
    
    if (!g_lru_tail) {
        g_lru_tail = entry;
    }
}

static void lru_remove(CacheEntry *entry) {
    if (!entry) return;
    
    if (entry->lru_prev) {
        entry->lru_prev->lru_next = entry->lru_next;
    } else {
        g_lru_head = entry->lru_next;
    }
    
    if (entry->lru_next) {
        entry->lru_next->lru_prev = entry->lru_prev;
    } else {
        g_lru_tail = entry->lru_prev;
    }
}

static void lru_move_to_head(CacheEntry *entry) {
    if (!entry) return;
    lru_remove(entry);
    lru_add_to_head(entry);
}


static CacheEntry *cache_entry_create(const CacheKey *key) {
    if (!key || !key->s) return NULL;
    
    CacheEntry *entry = (CacheEntry *)malloc(sizeof(CacheEntry));
    if (!entry) return NULL;
    
    memset(entry, 0, sizeof(CacheEntry));
    
    entry->key.s = (char *)malloc(key->len + 1);
    if (!entry->key.s) {
        free(entry);
        return NULL;
    }
    
    memcpy(entry->key.s, key->s, key->len);
    entry->key.s[key->len] = '\0';
    entry->key.len = key->len;
    
    entry->id = ++g_entry_id_counter;
    entry->in_map = 1;
    entry->is_downloader_running = 0;
    entry->is_completed = 0;
    entry->is_failed = 0;
    entry->http_status = 0;
    entry->content_type = NULL;
    entry->header_ready = 0;
    entry->resp_header = NULL;
    entry->resp_header_len = 0;
    entry->no_cache = 0;
    entry->chunks = NULL;
    entry->chunks_count = 0;
    entry->chunks_capacity = 0;
    entry->produced = 0;
    entry->bytes_total = 0;
    entry->subs_count = 0;
    entry->subs = NULL;
    entry->is_dirty = 0;
    
    pthread_mutex_init(&entry->m, NULL);
    
    return entry;
}

static void cache_entry_destroy(CacheEntry *entry) {
    if (!entry) return;
    
    if (entry->key.s) free(entry->key.s);
    if (entry->content_type) free(entry->content_type);
    if (entry->resp_header) free(entry->resp_header);
    
    for (int i = 0; i < entry->chunks_count; i++) {
        if (entry->chunks[i] && entry->chunks[i]->data) {
            free(entry->chunks[i]->data);
            free(entry->chunks[i]);
        }
    }
    if (entry->chunks) free(entry->chunks);
    
    SubNode *sub = entry->subs;
    while (sub) {
        SubNode *next = sub->hh_next;
        free(sub);
        sub = next;
    }
    
    pthread_mutex_destroy(&entry->m);
    
    free(entry);
}

int cache_init(size_t max_size) {
    g_cache_max_size = max_size;
    g_cache_current_size = 0;
    g_cache_map.entries = (CacheEntry **)calloc(32, sizeof(CacheEntry *));
    g_cache_map.capacity = 32;
    g_cache_map.count = 0;
    
    if (!g_cache_map.entries) {
        return -1;
    }
    
    fprintf(stdout, "[CACHE] Initialized with max size: %zu bytes\n", max_size);
    return 0;
}

CacheEntry *cache_lookup_or_create(CacheKey *key) {
    if (!key || !key->s) return NULL;
    
    pthread_mutex_lock(&g_cache_mutex);
    
    CacheEntry *entry = cache_map_find(key);
    
    if (!entry) {
        entry = cache_entry_create(key);
        if (!entry) {
            pthread_mutex_unlock(&g_cache_mutex);
            fprintf(stderr, "[CACHE] Failed to create cache entry\n");
            return NULL;
        }
        
        if (cache_map_insert(entry) != 0) {
            cache_entry_destroy(entry);
            pthread_mutex_unlock(&g_cache_mutex);
            fprintf(stderr, "[CACHE] Failed to insert entry into cache\n");
            return NULL;
        }
        
        lru_add_to_head(entry);
        
        fprintf(stdout, "[CACHE] Created new entry: %s (ID: %lu)\n", 
                entry->key.s, entry->id);
    } else {
        lru_move_to_head(entry);
        fprintf(stdout, "[CACHE] Found existing entry: %s (ID: %lu)\n", 
                entry->key.s, entry->id);
    }
    
    pthread_mutex_unlock(&g_cache_mutex);
    
    return entry;
}

int cache_append_chunk(CacheEntry *entry, const uint8_t *data, size_t size) {
    if (!entry || !data || size == 0) return -1;
    
    pthread_mutex_lock(&entry->m);
    
    if (entry->chunks_count >= entry->chunks_capacity) {
        int new_capacity = entry->chunks_capacity > 0 ? 
                          entry->chunks_capacity * 2 : 10;
        Chunk **new_chunks = (Chunk **)realloc(entry->chunks, 
                                               new_capacity * sizeof(Chunk *));
        if (!new_chunks) {
            pthread_mutex_unlock(&entry->m);
            return -1;
        }
        
        entry->chunks = new_chunks;
        entry->chunks_capacity = new_capacity;
    }
    
    Chunk *chunk = (Chunk *)malloc(sizeof(Chunk));
    if (!chunk) {
        pthread_mutex_unlock(&entry->m);
        return -1;
    }
    
    chunk->data = (uint8_t *)malloc(size);
    if (!chunk->data) {
        free(chunk);
        pthread_mutex_unlock(&entry->m);
        return -1;
    }
    
    memcpy(chunk->data, data, size);
    chunk->size = size;
    
    entry->chunks[entry->chunks_count++] = chunk;
    entry->produced += size;
    entry->bytes_total += size;
    entry->is_dirty = 1;
    
    pthread_mutex_unlock(&entry->m);
    
    fprintf(stdout, "[CACHE] Appended %zu bytes to entry %lu (total: %zu)\n",
            size, entry->id, entry->produced);
    
    return 0;
}

void cache_entry_complete(CacheEntry *entry, int http_status, const char *content_type) {
    if (!entry) return;
    
    pthread_mutex_lock(&entry->m);
    
    entry->http_status = http_status;
    entry->no_cache = (http_status != 200);
    entry->is_completed = 1;
    
    if (content_type) {
        entry->content_type = (char *)malloc(strlen(content_type) + 1);
        if (entry->content_type) {
            strcpy(entry->content_type, content_type);
        }
    }
    
    fprintf(stdout, "[CACHE] Entry %lu complete: status=%d, size=%zu bytes\n",
            entry->id, http_status, entry->produced);
    
    pthread_mutex_unlock(&entry->m);
}

void cache_entry_failed(CacheEntry *entry) {
    if (!entry) return;
    
    pthread_mutex_lock(&entry->m);
    entry->is_failed = 1;
    entry->is_completed = 1;
    fprintf(stdout, "[CACHE] Entry %lu marked as failed\n", entry->id);
    pthread_mutex_unlock(&entry->m);
}

int cache_get_chunk(CacheEntry *entry, size_t offset, uint8_t **out_data, size_t *out_size) {
    if (!entry || !out_data || !out_size) return -1;
    
    pthread_mutex_lock(&entry->m);
    
    *out_data = NULL;
    *out_size = 0;
    
    size_t current_offset = 0;
    
    for (int i = 0; i < entry->chunks_count; i++) {
        if (current_offset + entry->chunks[i]->size > offset) {
            size_t offset_in_chunk = offset - current_offset;
            *out_data = entry->chunks[i]->data + offset_in_chunk;
            *out_size = entry->chunks[i]->size - offset_in_chunk;
            
            pthread_mutex_unlock(&entry->m);
            return 0;
        }
        current_offset += entry->chunks[i]->size;
    }
    
    pthread_mutex_unlock(&entry->m);
    return 1;
}

int cache_entry_get_status(CacheEntry *entry) {
    if (!entry) return -1;
    
    pthread_mutex_lock(&entry->m);
    int status = entry->http_status;
    pthread_mutex_unlock(&entry->m);
    
    return status;
}

const char *cache_entry_get_content_type(CacheEntry *entry) {
    if (!entry) return NULL;
    pthread_mutex_lock(&entry->m);
    const char *ct = entry->content_type;
    pthread_mutex_unlock(&entry->m);
    return ct;
}

void cache_remove_entry(CacheEntry *entry) {
    if (!entry) return;
    
    pthread_mutex_lock(&g_cache_mutex);
    
    lru_remove(entry);
    cache_map_remove(entry);
    g_cache_current_size -= entry->bytes_total;
    
    fprintf(stdout, "[CACHE] Removed entry %lu\n", entry->id);
    
    cache_entry_destroy(entry);
    
    pthread_mutex_unlock(&g_cache_mutex);
}

void cache_cleanup(void) {
    fprintf(stdout, "[CACHE] Cleanup\n");
    
    pthread_mutex_lock(&g_cache_mutex);
    
    CacheEntry *entry = g_lru_head;
    while (entry) {
        CacheEntry *next = entry->lru_next;
        cache_map_remove(entry);
        cache_entry_destroy(entry);
        entry = next;
    }
    
    g_lru_head = NULL;
    g_lru_tail = NULL;
    g_cache_map.count = 0;
    g_cache_current_size = 0;
    
    free(g_cache_map.entries);
    g_cache_map.entries = NULL;
    
    pthread_mutex_unlock(&g_cache_mutex);
    
    fprintf(stdout, "[CACHE] Cleanup complete\n");
}


int cache_get_entry_count(void) {
    pthread_mutex_lock(&g_cache_mutex);
    int count = g_cache_map.count;
    pthread_mutex_unlock(&g_cache_mutex);
    return count;
}

size_t cache_get_total_size(void) {
    pthread_mutex_lock(&g_cache_mutex);
    size_t size = g_cache_current_size;
    pthread_mutex_unlock(&g_cache_mutex);
    return size;
}

void cache_print_stats(void) {
    pthread_mutex_lock(&g_cache_mutex);
    
    fprintf(stdout, "[CACHE STATS]\n");
    fprintf(stdout, "  Entries: %d\n", g_cache_map.count);
    fprintf(stdout, "  Total size: %zu / %zu bytes\n", g_cache_current_size, g_cache_max_size);
    fprintf(stdout, "  Load: %.1f%%\n", 
            g_cache_max_size > 0 ? (100.0 * g_cache_current_size / g_cache_max_size) : 0.0);
    
    fprintf(stdout, "  LRU chain:\n");
    int i = 0;
    for (CacheEntry *e = g_lru_head; e && i < 10; e = e->lru_next, i++) {
        fprintf(stdout, "    %d. %s (ID: %lu, size: %zu, status: %s)\n",
                i + 1, e->key.s, e->id, e->produced,
                e->is_completed ? "complete" : "incomplete");
    }
    if (i == 10 && g_lru_head->lru_next) {
        fprintf(stdout, "    ... and %d more\n", g_cache_map.count - 10);
    }
    
    pthread_mutex_unlock(&g_cache_mutex);
}
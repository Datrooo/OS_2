#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <time.h>
#include <errno.h>

#include "cache.h"
#include "gc.h"

typedef struct CacheMap {
    CacheEntry **entries;
    int capacity;
    int count;
} CacheMap;

typedef struct CacheState {
    CacheMap map;
    CacheEntry *lru_head;
    CacheEntry *lru_tail;
    pthread_mutex_t mutex;
    pthread_cond_t space_cond;
    size_t max_size;
    size_t current_size;
    uint64_t entry_id_counter;
} CacheState;

static CacheState g_cache = {
    .map = {NULL, 0, 0},
    .lru_head = NULL,
    .lru_tail = NULL,
    .mutex = PTHREAD_MUTEX_INITIALIZER,
    .space_cond = PTHREAD_COND_INITIALIZER,
    .max_size = 0,
    .current_size = 0,
    .entry_id_counter = 0,
};

static void cache_log_pthread_rc(const char *ctx, const char *op, int rc) {
    if (rc == 0) return;
    fprintf(stderr, "[CACHE] %s: %s failed: %s\n", ctx, op, strerror(rc));
}

static int cache_mutex_lock(pthread_mutex_t *m, const char *ctx) {
    int rc = pthread_mutex_lock(m);
    if (rc != 0) {
        cache_log_pthread_rc(ctx, "pthread_mutex_lock", rc);
        return -1;
    }
    return 0;
}

static int cache_mutex_unlock(pthread_mutex_t *m, const char *ctx) {
    int rc = pthread_mutex_unlock(m);
    if (rc != 0) {
        cache_log_pthread_rc(ctx, "pthread_mutex_unlock", rc);
        return -1;
    }
    return 0;
}

static unsigned int hash_djb2(const char *str, size_t len) {
    unsigned int hash = 5381;
    for (size_t i = 0; i < len; i++) {
        hash = ((hash << 5) + hash) + str[i];
    }
    return hash;
}

static CacheEntry *cache_map_find(const CacheKey *key) {
    if (!g_cache.map.entries || g_cache.map.count == 0) {
        return NULL;
    }
    
    unsigned int h = hash_djb2(key->s, key->len);
    int idx = h % g_cache.map.capacity;
    
    CacheEntry *entry = g_cache.map.entries[idx];
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
    
    if (g_cache.map.count >= g_cache.map.capacity * 0.75) {
        int new_capacity = g_cache.map.capacity > 0 ?
                          g_cache.map.capacity * 2 : 32;
        CacheEntry **new_entries = (CacheEntry **)calloc(new_capacity, sizeof(CacheEntry *));
        if (!new_entries) return -1;
        
        for (int i = 0; i < g_cache.map.capacity; i++) {
            CacheEntry *e = g_cache.map.entries[i];
            while (e) {
                unsigned int h = hash_djb2(e->key.s, e->key.len);
                int new_idx = h % new_capacity;
                CacheEntry *next = e->hash_next;
                e->hash_next = new_entries[new_idx];
                new_entries[new_idx] = e;
                e = next;
            }
        }
        
        free(g_cache.map.entries);
        g_cache.map.entries = new_entries;
        g_cache.map.capacity = new_capacity;
    }
    
    unsigned int h = hash_djb2(entry->key.s, entry->key.len);
    int idx = h % g_cache.map.capacity;
    
    entry->hash_next = g_cache.map.entries[idx];
    g_cache.map.entries[idx] = entry;
    g_cache.map.count++;
    
    return 0;
}

static void cache_map_remove(CacheEntry *entry) {
    if (!entry || !g_cache.map.entries) return;
    
    unsigned int h = hash_djb2(entry->key.s, entry->key.len);
    int idx = h % g_cache.map.capacity;
    
    CacheEntry **pp = &g_cache.map.entries[idx];
    while (*pp) {
        if (*pp == entry) {
            *pp = entry->hash_next;
            g_cache.map.count--;
            return;
        }
        pp = &((*pp)->hash_next);
    }
}

static void lru_add_to_head(CacheEntry *entry) {
    if (!entry) return;
    
    entry->lru_prev = NULL;
    entry->lru_next = g_cache.lru_head;
    
    if (g_cache.lru_head) {
        g_cache.lru_head->lru_prev = entry;
    }
    g_cache.lru_head = entry;
    
    if (!g_cache.lru_tail) {
        g_cache.lru_tail = entry;
    }
}

static void lru_remove(CacheEntry *entry) {
    if (!entry) return;
    
    if (entry->lru_prev) {
        entry->lru_prev->lru_next = entry->lru_next;
    } else {
        g_cache.lru_head = entry->lru_next;
    }
    
    if (entry->lru_next) {
        entry->lru_next->lru_prev = entry->lru_prev;
    } else {
        g_cache.lru_tail = entry->lru_prev;
    }
}

static void lru_move_to_head(CacheEntry *entry) {
    if (!entry) return;
    lru_remove(entry);
    lru_add_to_head(entry);
}

size_t cache_get_max_size(void) {
    if (cache_mutex_lock(&g_cache.mutex, "get_max_size") != 0) {
        return 0;
    }
    size_t out = g_cache.max_size;
    (void)cache_mutex_unlock(&g_cache.mutex, "get_max_size(unlock)");
    return out;
}

static int entry_can_delete(CacheEntry *entry) {
    if (!entry) return 0;

    int trc = pthread_mutex_trylock(&entry->m);
    if (trc != 0) {
        return 0;
    }
    int ok = (entry->is_completed && !entry->is_downloader_running && entry->subs_count == 0);
    (void)cache_mutex_unlock(&entry->m, "entry_can_delete(unlock)");
    return ok;
}

static CacheEntry *find_delete_candidate_locked(void) {
    for (CacheEntry *e = g_cache.lru_tail; e; e = e->lru_prev) {
        if (entry_can_delete(e)) {
            return e;
        }
    }
    return NULL;
}


static CacheEntry *cache_entry_create(const CacheKey *key) {
    if (!key || !key->s) return NULL;

    CacheEntry * entry = calloc(1, sizeof(CacheEntry));
    if (!entry) return NULL;
    
    entry->key.s = (char *)malloc(key->len + 1);
    if (!entry->key.s) {
        free(entry);
        return NULL;
    }
    
    memcpy(entry->key.s, key->s, key->len);
    entry->key.s[key->len] = '\0';
    entry->key.len = key->len;
    
    entry->id = ++g_cache.entry_id_counter;
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
    
    int irc = pthread_mutex_init(&entry->m, NULL);
    if (irc != 0) {
        cache_log_pthread_rc("entry_create", "pthread_mutex_init", irc);
        if (entry->key.s) free(entry->key.s);
        free(entry);
        return NULL;
    }
    
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
    
    int drc = pthread_mutex_destroy(&entry->m);
    if (drc != 0) {
        cache_log_pthread_rc("entry_destroy", "pthread_mutex_destroy", drc);
    }
    
    free(entry);
}

int cache_create(size_t max_size) {
    g_cache.max_size = max_size;
    g_cache.current_size = 0;
    g_cache.map.entries = (CacheEntry **)calloc(32, sizeof(CacheEntry *));
    g_cache.map.capacity = 32;
    g_cache.map.count = 0;
    g_cache.lru_head = NULL;
    g_cache.lru_tail = NULL;
    
    if (!g_cache.map.entries) {
        return -1;
    }
    
    fprintf(stdout, "[CACHE] Initialized with max size: %zu bytes\n", max_size);
    return 0;
}

CacheEntry *cache_lookup_or_create(CacheKey *key) {
    if (!key || !key->s) return NULL;
    
    if (cache_mutex_lock(&g_cache.mutex, "lookup_or_create") != 0) {
        return NULL;
    }
    
    CacheEntry *entry = cache_map_find(key);
    
    if (!entry) {
        entry = cache_entry_create(key);
        if (!entry) {
            (void)cache_mutex_unlock(&g_cache.mutex, "lookup_or_create(create_fail_unlock)");
            fprintf(stderr, "[CACHE] Failed to create cache entry\n");
            return NULL;
        }
        
        if (cache_map_insert(entry) != 0) {
            cache_entry_destroy(entry);
            (void)cache_mutex_unlock(&g_cache.mutex, "lookup_or_create(insert_fail_unlock)");
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
    
    (void)cache_mutex_unlock(&g_cache.mutex, "lookup_or_create(unlock)");
    
    return entry;
}

int cache_append_chunk(CacheEntry *entry, const uint8_t *data, size_t size) {
    if (!entry || !data || size == 0) return -1;

    if (cache_mutex_lock(&g_cache.mutex, "append_chunk(get_max)") != 0) {
        return -1;
    }
    size_t max_size = g_cache.max_size;
    (void)cache_mutex_unlock(&g_cache.mutex, "append_chunk(get_max_unlock)");
    if (max_size > 0 && size > max_size) {
        fprintf(stderr, "[CACHE] Chunk too large (%zu > %zu), refusing\n", size, max_size);
        return -2;
    }

    if (max_size > 0) {
        if (cache_mutex_lock(&g_cache.mutex, "append_chunk(space_lock)") != 0) {
            return -1;
        }
        while (g_cache.current_size + size > g_cache.max_size) {
            gc_notify_pressure();

            struct timespec ts;
            if (clock_gettime(CLOCK_REALTIME, &ts) != 0) {
                // Fallback: coarse wall-clock deadline.
                ts.tv_sec = time(NULL);
                ts.tv_nsec = 0;
            }
            ts.tv_nsec += 200 * 1000 * 1000;
            if (ts.tv_nsec >= 1000000000L) {
                ts.tv_sec += 1;
                ts.tv_nsec -= 1000000000L;
            }

            int wrc = pthread_cond_timedwait(&g_cache.space_cond, &g_cache.mutex, &ts);
            if (wrc != 0 && wrc != ETIMEDOUT) {
                fprintf(stderr, "[CACHE] cond_timedwait failed: %s\n", strerror(wrc));
                break;
            }
        }
        g_cache.current_size += size;
        (void)cache_mutex_unlock(&g_cache.mutex, "append_chunk(space_unlock)");
    }
    
    if (cache_mutex_lock(&entry->m, "append_chunk(entry_lock)") != 0) {
        if (max_size > 0) {
            if (cache_mutex_lock(&g_cache.mutex, "append_chunk(rollback_lock)") == 0) {
                if (g_cache.current_size >= size) g_cache.current_size -= size;
                (void)pthread_cond_broadcast(&g_cache.space_cond);
                (void)cache_mutex_unlock(&g_cache.mutex, "append_chunk(rollback_unlock)");
            }
        }
        return -1;
    }
    
    if (entry->chunks_count >= entry->chunks_capacity) {
        int new_capacity = entry->chunks_capacity > 0 ? 
                          entry->chunks_capacity * 2 : 10;
        Chunk **new_chunks = (Chunk **)realloc(entry->chunks, 
                                               new_capacity * sizeof(Chunk *));
        if (!new_chunks) {
            (void)cache_mutex_unlock(&entry->m, "append_chunk(realloc_fail_unlock)");
            return -1;
        }
        
        entry->chunks = new_chunks;
        entry->chunks_capacity = new_capacity;
    }
    
    Chunk *chunk = (Chunk *)malloc(sizeof(Chunk));
    if (!chunk) {
        (void)cache_mutex_unlock(&entry->m, "append_chunk(chunk_alloc_fail_unlock)");
        if (max_size > 0) {
            if (cache_mutex_lock(&g_cache.mutex, "append_chunk(chunk_alloc_fail_cache_lock)") == 0) {
            g_cache.current_size -= size;
            int brc = pthread_cond_broadcast(&g_cache.space_cond);
            cache_log_pthread_rc("append_chunk(chunk_alloc_fail)", "pthread_cond_broadcast", brc);
            (void)cache_mutex_unlock(&g_cache.mutex, "append_chunk(chunk_alloc_fail_cache_unlock)");
            }
        }
        return -1;
    }
    
    chunk->data = (uint8_t *)malloc(size);
    if (!chunk->data) {
        free(chunk);
        (void)cache_mutex_unlock(&entry->m, "append_chunk(data_alloc_fail_unlock)");
        if (max_size > 0) {
            if (cache_mutex_lock(&g_cache.mutex, "append_chunk(data_alloc_fail_cache_lock)") == 0) {
            g_cache.current_size -= size;
            int brc = pthread_cond_broadcast(&g_cache.space_cond);
            cache_log_pthread_rc("append_chunk(data_alloc_fail)", "pthread_cond_broadcast", brc);
            (void)cache_mutex_unlock(&g_cache.mutex, "append_chunk(data_alloc_fail_cache_unlock)");
            }
        }
        return -1;
    }
    
    memcpy(chunk->data, data, size);
    chunk->size = size;
    
    entry->chunks[entry->chunks_count++] = chunk;
    entry->produced += size;
    entry->bytes_total += size;
    entry->is_dirty = 1;
    
    (void)cache_mutex_unlock(&entry->m, "append_chunk(entry_unlock)");
    
    fprintf(stdout, "[CACHE] Appended %zu bytes to entry %lu (total: %zu)\n",
            size, entry->id, entry->produced);
    
    return 0;
}

void cache_entry_complete(CacheEntry *entry, int http_status, const char *content_type) {
    if (!entry) return;
    
    if (cache_mutex_lock(&entry->m, "entry_complete") != 0) {
        return;
    }
    
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
    
    (void)cache_mutex_unlock(&entry->m, "entry_complete(unlock)");
}

void cache_entry_failed(CacheEntry *entry) {
    if (!entry) return;
    
    if (cache_mutex_lock(&entry->m, "entry_failed") != 0) {
        return;
    }
    entry->http_status = 500;
    entry->no_cache = 1;
    entry->header_ready = 1;
    entry->is_failed = 1;
    entry->is_completed = 1;
    fprintf(stdout, "[CACHE] Entry %lu marked as failed\n", entry->id);
    (void)cache_mutex_unlock(&entry->m, "entry_failed(unlock)");
}

int cache_get_chunk(CacheEntry *entry, size_t offset, uint8_t **out_data, size_t *out_size) {
    if (!entry || !out_data || !out_size) return -1;
    
    if (cache_mutex_lock(&entry->m, "get_chunk") != 0) {
        return -1;
    }
    
    *out_data = NULL;
    *out_size = 0;
    
    size_t current_offset = 0;
    
    for (int i = 0; i < entry->chunks_count; i++) {
        if (current_offset + entry->chunks[i]->size > offset) {
            size_t offset_in_chunk = offset - current_offset;
            *out_data = entry->chunks[i]->data + offset_in_chunk;
            *out_size = entry->chunks[i]->size - offset_in_chunk;
            
            (void)cache_mutex_unlock(&entry->m, "get_chunk(found_unlock)");
            return 0;
        }
        current_offset += entry->chunks[i]->size;
    }
    
    (void)cache_mutex_unlock(&entry->m, "get_chunk(miss_unlock)");
    return 1;
}

int cache_entry_get_status(CacheEntry *entry) {
    if (!entry) return -1;
    
    if (cache_mutex_lock(&entry->m, "get_status") != 0) {
        return -1;
    }
    int status = entry->http_status;
    (void)cache_mutex_unlock(&entry->m, "get_status(unlock)");
    
    return status;
}

const char *cache_entry_get_content_type(CacheEntry *entry) {
    if (!entry) return NULL;
    if (cache_mutex_lock(&entry->m, "get_content_type") != 0) {
        return NULL;
    }
    const char *ct = entry->content_type;
    (void)cache_mutex_unlock(&entry->m, "get_content_type(unlock)");
    return ct;
}

void cache_remove_entry(CacheEntry *entry) {
    if (!entry) return;

    size_t freed = 0;
    if (cache_mutex_lock(&entry->m, "remove_entry(entry_lock)") != 0) {
        return;
    }
    freed = entry->bytes_total;
    (void)cache_mutex_unlock(&entry->m, "remove_entry(entry_unlock)");

    if (cache_mutex_lock(&g_cache.mutex, "remove_entry(cache_lock)") != 0) {
        return;
    }

    lru_remove(entry);
    cache_map_remove(entry);
    if (g_cache.current_size >= freed) {
        g_cache.current_size -= freed;
    } else {
        g_cache.current_size = 0;
    }

    int brc = pthread_cond_broadcast(&g_cache.space_cond);
    cache_log_pthread_rc("remove_entry", "pthread_cond_broadcast", brc);
    fprintf(stdout, "[CACHE] Removed entry %lu\n", entry->id);

    (void)cache_mutex_unlock(&g_cache.mutex, "remove_entry(cache_unlock)");

    cache_entry_destroy(entry);
}

void cache_destroy(void) {
    fprintf(stdout, "[CACHE] Cleanup\n");
    
    if (cache_mutex_lock(&g_cache.mutex, "cleanup") != 0) {
        return;
    }
    
    CacheEntry *entry = g_cache.lru_head;
    while (entry) {
        CacheEntry *next = entry->lru_next;
        cache_map_remove(entry);
        cache_entry_destroy(entry);
        entry = next;
    }
    
    g_cache.lru_head = NULL;
    g_cache.lru_tail = NULL;
    g_cache.map.count = 0;
    g_cache.current_size = 0;

    int brc = pthread_cond_broadcast(&g_cache.space_cond);
    cache_log_pthread_rc("cleanup", "pthread_cond_broadcast", brc);
    
    free(g_cache.map.entries);
    g_cache.map.entries = NULL;
    
    (void)cache_mutex_unlock(&g_cache.mutex, "cleanup(unlock)");
    
    fprintf(stdout, "[CACHE] Cleanup complete\n");
}

int cache_trim_to_max(void) {
    int deleted = 0;

    if (cache_mutex_lock(&g_cache.mutex, "trim_to_max") != 0) {
        return 0;
    }
    if (g_cache.max_size == 0) {
        (void)cache_mutex_unlock(&g_cache.mutex, "trim_to_max(unlock_zero)");
        return 0;
    }

    while (g_cache.current_size > g_cache.max_size) {
        CacheEntry *cand = find_delete_candidate_locked();
        if (!cand) {
            break;
        }

        size_t freed = 0;
        if (cache_mutex_lock(&cand->m, "trim_to_max(cand_lock)") != 0) {
            break;
        }
        freed = cand->bytes_total;
        (void)cache_mutex_unlock(&cand->m, "trim_to_max(cand_unlock)");

        lru_remove(cand);
        cache_map_remove(cand);

        if (g_cache.current_size >= freed) {
            g_cache.current_size -= freed;
        } else {
            g_cache.current_size = 0;
        }

        int brc = pthread_cond_broadcast(&g_cache.space_cond);
        cache_log_pthread_rc("trim_to_max", "pthread_cond_broadcast", brc);
        (void)cache_mutex_unlock(&g_cache.mutex, "trim_to_max(cache_unlock_before_destroy)");

        cache_entry_destroy(cand);
        deleted++;

        if (cache_mutex_lock(&g_cache.mutex, "trim_to_max(relock)") != 0) {
            return deleted;
        }
    }

    (void)cache_mutex_unlock(&g_cache.mutex, "trim_to_max(unlock)");
    return deleted;
}


int cache_get_entry_count(void) {
    if (cache_mutex_lock(&g_cache.mutex, "get_entry_count") != 0) {
        return -1;
    }
    int count = g_cache.map.count;
    (void)cache_mutex_unlock(&g_cache.mutex, "get_entry_count(unlock)");
    return count;
}

size_t cache_get_total_size(void) {
    if (cache_mutex_lock(&g_cache.mutex, "get_total_size") != 0) {
        return 0;
    }
    size_t size = g_cache.current_size;
    (void)cache_mutex_unlock(&g_cache.mutex, "get_total_size(unlock)");
    return size;
}

void cache_print_stats(void) {
    if (cache_mutex_lock(&g_cache.mutex, "print_stats") != 0) {
        return;
    }
    
    fprintf(stdout, "[CACHE STATS]\n");
        fprintf(stdout, "  Entries: %d\n", g_cache.map.count);
        fprintf(stdout, "  Total size: %zu / %zu bytes\n", g_cache.current_size, g_cache.max_size);
    fprintf(stdout, "  Load: %.1f%%\n", 
            g_cache.max_size > 0 ? (100.0 * g_cache.current_size / g_cache.max_size) : 0.0);
    
    fprintf(stdout, "  LRU chain:\n");
    int i = 0;
    for (CacheEntry *e = g_cache.lru_head; e && i < 10; e = e->lru_next, i++) {
        fprintf(stdout, "    %d. %s (ID: %lu, size: %zu, status: %s)\n",
                i + 1, e->key.s, e->id, e->produced,
                e->is_completed ? "complete" : "incomplete");
    }
    if (i == 10 && g_cache.lru_head && g_cache.lru_head->lru_next) {
        fprintf(stdout, "    ... and %d more\n", g_cache.map.count - 10);
    }
    
    (void)cache_mutex_unlock(&g_cache.mutex, "print_stats(unlock)");
}
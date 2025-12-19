#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>

#include "cache.h"

static pthread_mutex_t cache_mutex = PTHREAD_MUTEX_INITIALIZER;
static size_t max_cache_size = 0;
static size_t current_size = 0;

int cache_init(size_t max_size) {
    max_cache_size = max_size;
    current_size = 0;
    fprintf(stdout, "[CACHE] Initialized with max size: %zu bytes\n", max_size);
    return 0;
}

CacheEntry *cache_lookup_or_create(CacheKey *key) {
    if (!key) return NULL;
    
    /* TODO: реализовать поиск/создание entry */
    fprintf(stdout, "[CACHE] Lookup/create: %s\n", key->s);
    
    return NULL;
}

int cache_append_chunk(CacheEntry *entry, const uint8_t *data, size_t size) {
    if (!entry || !data) return -1;
    
    /* TODO: добавить чанк */
    
    return 0;
}

void cache_entry_complete(CacheEntry *entry, int http_status, const char *content_type) {
    if (!entry) return;
    
    /* TODO: отметить завершённой */
}

void cache_entry_failed(CacheEntry *entry) {
    if (!entry) return;
    
    /* TODO: отметить ошибку */
}

int cache_get_chunk(CacheEntry *entry, size_t offset, uint8_t **out_data, size_t *out_size) {
    if (!entry || !out_data || !out_size) return -1;
    
    /* TODO: получить данные */
    
    return 0;
}

int cache_entry_get_status(CacheEntry *entry) {
    if (!entry) return -1;
    
    /* TODO: получить статус */
    
    return 0;
}

const char *cache_entry_get_content_type(CacheEntry *entry) {
    if (!entry) return NULL;
    
    return entry->content_type;
}

void cache_remove_entry(CacheEntry *entry) {
    if (!entry) return;
    
    /* TODO: удалить entry */
}

void cache_cleanup(void) {
    fprintf(stdout, "[CACHE] Cleanup\n");
    
    /* TODO: очистить весь кэш */
}

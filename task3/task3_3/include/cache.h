#ifndef __CACHE_H__
#define __CACHE_H__

#include "types.h"

int cache_create(size_t max_size);

CacheEntry *cache_lookup_or_create(CacheKey *key);

void cache_entry_acquire(CacheEntry *entry);

void cache_entry_release(CacheEntry *entry);

int cache_append_chunk(CacheEntry *entry, const uint8_t *data, size_t size);

void cache_entry_complete(CacheEntry *entry, int http_status, const char *content_type);

void cache_entry_failed(CacheEntry *entry);

int cache_get_chunk(CacheEntry *entry, size_t offset, uint8_t **out_data, size_t *out_size);

int cache_entry_get_status(CacheEntry *entry);

const char *cache_entry_get_content_type(CacheEntry *entry);

void cache_remove_entry(CacheEntry *entry);

void cache_destroy(void);


int cache_trim_to_max(void);

size_t cache_get_max_size(void);

#endif
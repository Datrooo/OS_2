#ifndef __KEY_BUILDER_H__
#define __KEY_BUILDER_H__

#include "types.h"

// Формат: "host_lower:port/path?query"
CacheKey *build_cache_key(const char *host, const char *target, int port);

void cache_key_free(CacheKey *key);

int cache_key_equal(const CacheKey *a, const CacheKey *b);

#endif 
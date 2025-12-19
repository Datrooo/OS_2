#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include "key_builder.h"

CacheKey *build_cache_key(const char *host, const char *target, int port) {
    if (!host || !target) return NULL;
    
    char *host_lower = (char *)malloc(strlen(host) + 1);
    if (!host_lower) return NULL;
    
    for (size_t i = 0; host[i]; i++) {
        host_lower[i] = tolower((unsigned char)host[i]);
    }
    host_lower[strlen(host)] = '\0';
    
    // Строим ключ: "host:port/path?query"
    size_t key_size = strlen(host_lower) + 20 + strlen(target);
    char *key_str = (char *)malloc(key_size);
    if (!key_str) {
        free(host_lower);
        return NULL;
    }
    
    snprintf(key_str, key_size, "%s:%d%s", host_lower, port, target);
    
    CacheKey *key = (CacheKey *)malloc(sizeof(CacheKey));
    if (!key) {
        free(host_lower);
        free(key_str);
        return NULL;
    }
    
    key->s = key_str;
    key->len = strlen(key_str);
    
    free(host_lower);
    
    fprintf(stdout, "[KEY_BUILDER] Built key: %s\n", key->s);
    
    return key;
}

void cache_key_free(CacheKey *key) {
    if (!key) return;
    
    if (key->s) {
        free(key->s);
    }
    free(key);
}

int cache_key_equal(const CacheKey *a, const CacheKey *b) {
    if (!a || !b) return 0;
    if (a->len != b->len) return 0;
    return strcmp(a->s, b->s) == 0;
}
#ifndef __SUBSCRIBERS_H__
#define __SUBSCRIBERS_H__
#include "types.h"

int subscriber_add(CacheEntry *entry, Session *session);

int subscriber_remove(CacheEntry *entry, Session *session);

Session **subscriber_get_all(CacheEntry *entry, int *out_count);

int subscriber_clear_all(CacheEntry *entry);

int is_entry_downloading(CacheEntry *entry);

#endif
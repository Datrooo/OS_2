#ifndef __SUBSCRIBERS_H__
#define __SUBSCRIBERS_H__
#include "types.h"

int subscriber_add(CacheEntry *entry, Session *session);

int subscriber_remove(CacheEntry *entry, Session *session);

int subscriber_count(CacheEntry *entry);

int subscriber_snapshot(CacheEntry *entry, Session ***out_sessions);

void subscriber_forget_entry(CacheEntry *entry);

#endif
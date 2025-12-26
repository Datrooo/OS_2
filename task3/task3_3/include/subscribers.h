#ifndef __SUBSCRIBERS_H__
#define __SUBSCRIBERS_H__
#include "types.h"

int subscriber_add(CacheEntry *entry, Session *session);

int subscriber_remove(CacheEntry *entry, Session *session);

#endif
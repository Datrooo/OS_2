#ifndef __GC_H__
#define __GC_H__

#include "types.h"

int gc_create(size_t max_cache_size);

void gc_notify_pressure(void);

void gc_destroy(void);

#endif
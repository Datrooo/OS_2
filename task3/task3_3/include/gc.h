#ifndef __GC_H__
#define __GC_H__

#include "types.h"

int gc_init(size_t max_cache_size);

void gc_shutdown(void);

#endif
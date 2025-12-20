#ifndef __DIRTY_H__
#define __DIRTY_H__

#include "types.h"

/* 
    Downloader threads enqueue entries that have new data.
    Event loop periodically processes the queue in batch.
    Reduces context switches vs. calling loop_notify_dirty() every chunk.
*/

int dirty_init(void);

int dirty_enqueue(CacheEntry *entry);

int dirty_process_all(void);

int dirty_get_queue_size(void);

void dirty_cleanup(void);

#endif
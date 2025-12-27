#ifndef __DIRTY_H__
#define __DIRTY_H__

#include "types.h"

/* 
    Downloader threads enqueue entries that have new data.
    Event loop processes the queue when notified via ev_async.
*/

int dirty_create(void);

int dirty_enqueue(CacheEntry *entry);

int dirty_process_all(CacheEntry ***out_entries);

void dirty_destroy(void);

#endif
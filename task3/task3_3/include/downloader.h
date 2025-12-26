#ifndef __DOWNLOADER_H__
#define __DOWNLOADER_H__

#include "types.h"

/* worker threads that:
   1. Pop tasks from queue
   2. Connect to origin server
   3. Send HTTP/1.0 GET request
   4. Stream response into cache via cache_append_chunk()
   5. Mark complete via cache_entry_complete()
    */

int downloader_create(void);

int downloader_enqueue(CacheEntry *entry, int urgency);

void downloader_destroy(void);

#endif
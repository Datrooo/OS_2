#ifndef __DOWNLOADER_H__
#define __DOWNLOADER_H__

#include "types.h"

int downloader_init(int pool_size);

int downloader_enqueue(CacheEntry *entry);

void downloader_shutdown(void);

#endif
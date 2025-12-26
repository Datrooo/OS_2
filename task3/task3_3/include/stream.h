#ifndef __STREAM_H__
#define __STREAM_H__

#include "types.h"

/* Format HTTP response header for a cache entry */
int stream_format_response_header(CacheEntry *entry, 
                                  char *buf, size_t buf_len,
                                  size_t *out_len);

int stream_send_header(Session *session);

// returns bytes sent or error
int stream_send_body(Session *session);

int stream_can_send_more(Session *session);

#endif
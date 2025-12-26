#ifndef __SESSION_H__
#define __SESSION_H__

#include "types.h"

Session *session_new(int fd);

void session_free(Session *s);

int session_attach_entry(Session *s, CacheEntry *entry);

void session_detach_entry(Session *s);

#endif
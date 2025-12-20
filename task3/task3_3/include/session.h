#ifndef __SESSION_H__
#define __SESSION_H__

#include "types.h"

Session *session_new(int fd);

void session_free(Session *s);

int session_attach_entry(Session *s, CacheEntry *entry);

void session_detach_entry(Session *s);

int session_send_response_header(Session *s, CacheEntry *entry);
/*  0  - ждем данные
    1  - успех
    -1 - ошибка
*/
int session_send_cached_data(Session *s);

uint64_t session_get_id(Session *s);

#endif
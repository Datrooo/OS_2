#ifndef __PARSE_H__
#define __PARSE_H__

#include "types.h"

/* - 0: нужно ещё данные
   - 1: успешно распарсили заголовок
   - -1: ошибка */
int parse_http_request(const char *buf, size_t buf_len, 
                       char **method, char **target, char **http_version,
                       size_t *headers_end);

char *parse_host_header(const char *headers_start, size_t headers_len);

#endif
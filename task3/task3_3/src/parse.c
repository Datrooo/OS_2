#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include "parse.h"

static int parse_request_line(const char *buf, size_t buf_len,
                              char **out_method, char **out_target, char **out_version) {
    if (buf_len < 10) return 0;  // Минимум "GET / HTTP/1.0\r\n"
    
    // Ищем первый \r\n 
    const char *line_end = strstr(buf, "\r\n");
    if (!line_end) return 0;
    
    size_t line_len = line_end - buf;
    
    char *line = (char *)malloc(line_len + 1);
    if (!line) return -1;
    
    memcpy(line, buf, line_len);
    line[line_len] = '\0';
    
    char *method_end = strchr(line, ' ');
    if (!method_end) {
        free(line);
        return -1;
    }
    
    size_t method_len = method_end - line;
    char *method = (char *)malloc(method_len + 1);
    if (!method) {
        free(line);
        return -1;
    }
    memcpy(method, line, method_len);
    method[method_len] = '\0';
    
    char *target_start = method_end + 1;
    char *target_end = strchr(target_start, ' ');
    if (!target_end) {
        free(method);
        free(line);
        return -1;
    }
    
    size_t target_len = target_end - target_start;
    char *target = (char *)malloc(target_len + 1);
    if (!target) {
        free(method);
        free(line);
        return -1;
    }
    memcpy(target, target_start, target_len);
    target[target_len] = '\0';
    
    char *version_start = target_end + 1;
    size_t version_len = strlen(version_start);
    char *version = (char *)malloc(version_len + 1);
    if (!version) {
        free(method);
        free(target);
        free(line);
        return -1;
    }
    memcpy(version, version_start, version_len);
    version[version_len] = '\0';
    
    free(line);
    
    *out_method = method;
    *out_target = target;
    *out_version = version;
    
    return 1;
}

int parse_http_request(const char *buf, size_t buf_len,
                       char **method, char **target, char **http_version,
                       size_t *headers_end) {
    if (!buf || buf_len == 0) return 0;
    
    /* Ищем конец заголовков (\r\n\r\n) */
    const char *end_marker = strstr(buf, "\r\n\r\n");
    if (!end_marker) return 0;
    
    int result = parse_request_line(buf, buf_len, method, target, http_version);
    if (result <= 0) return result;
    
    *headers_end = (end_marker - buf) + 4;  /* +4 для \r\n\r\n */
    
    return 1;
}


char *parse_host_header(const char *headers_start, size_t headers_len) {
    if (!headers_start || headers_len == 0) return NULL;
    
    const char *host_pos = strstr(headers_start, "Host: ");
    if (!host_pos) {
        host_pos = strstr(headers_start, "host: ");
    }
    
    if (!host_pos) return NULL;
    
    /*"Host: " или "host: " */
    const char *value_start = host_pos + 6;
    
    /* Ищем конец строки (\r\n) */
    const char *value_end = strstr(value_start, "\r\n");
    if (!value_end) {
        /* Может быть просто \n */
        value_end = strchr(value_start, '\n');
    }
    
    if (!value_end) return NULL;
    
    /* Удаляем trailing whitespace */
    while (value_end > value_start && (value_end[-1] == '\r' || value_end[-1] == '\n')) {
        value_end--;
    }
    
    size_t host_len = value_end - value_start;
    
    char *host = (char *)malloc(host_len + 1);
    if (!host) return NULL;
    
    memcpy(host, value_start, host_len);
    host[host_len] = '\0';
    
    /* Trim whitespace */
    char *p = host;
    while (isspace(*p)) p++;
    
    if (p != host) {
        char *host2 = (char *)malloc(strlen(p) + 1);
        if (!host2) {
            free(host);
            return NULL;
        }
        strcpy(host2, p);
        free(host);
        host = host2;
    }
    
    return host;
}
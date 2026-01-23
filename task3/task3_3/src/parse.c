#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include "parse.h"

static int find_crlf(const char *buf, size_t len, size_t *out_pos) {
    if (!buf || len < 2) return 0;
    for (size_t i = 0; i + 1 < len; i++) {
        if (buf[i] == '\r' && buf[i + 1] == '\n') {
            *out_pos = i;
            return 1;
        }
    }
    return 0;
}

static int find_double_crlf_len(const char *buf, size_t len, size_t *out_hdr_len) {
    if (!buf || len < 4) return 0;
    for (size_t i = 0; i + 3 < len; i++) {
        if (buf[i] == '\r' && buf[i + 1] == '\n' && buf[i + 2] == '\r' && buf[i + 3] == '\n') {
            *out_hdr_len = i + 4;
            return 1;
        }
    }
    return 0;
}

static int ascii_ieq(char a, char b) {
    return tolower((unsigned char)a) == tolower((unsigned char)b);
}

static int mem_case_prefix(const char *buf, size_t len, const char *prefix) {
    size_t p_len = strlen(prefix);
    if (len < p_len) return 0;
    for (size_t i = 0; i < p_len; i++) {
        if (!ascii_ieq(buf[i], prefix[i])) return 0;
    }
    return 1;
}

static int parse_request_line(const char *buf, size_t buf_len,
                              char **out_method, char **out_target, char **out_version) {
    if (!buf || buf_len < 10) return 0;  // "GET / HTTP/1.0\r\n"

    size_t line_end_pos = 0;
    if (!find_crlf(buf, buf_len, &line_end_pos)) return 0;

    size_t i = 0;
    size_t method_start = i;
    while (i < line_end_pos && buf[i] != ' ') i++;
    if (i == method_start || i >= line_end_pos) return -1;
    size_t method_len = i - method_start;
    i++;

    size_t target_start = i;
    while (i < line_end_pos && buf[i] != ' ') i++;
    if (i == target_start || i >= line_end_pos) return -1;
    size_t target_len = i - target_start;
    i++;

    size_t ver_start = i;
    if (ver_start >= line_end_pos) return -1;
    size_t ver_len = line_end_pos - ver_start;

    char *method = (char *)malloc(method_len + 1);
    char *target = (char *)malloc(target_len + 1);
    char *version = (char *)malloc(ver_len + 1);
    if (!method || !target || !version) {
        free(method);
        free(target);
        free(version);
        return -1;
    }

    memcpy(method, buf + method_start, method_len);
    method[method_len] = '\0';
    memcpy(target, buf + target_start, target_len);
    target[target_len] = '\0';
    memcpy(version, buf + ver_start, ver_len);
    version[ver_len] = '\0';

    *out_method = method;
    *out_target = target;
    *out_version = version;

    return 1;
}

int parse_http_request(const char *buf, size_t buf_len,
                       char **method, char **target, char **http_version,
                       size_t *headers_end) {
    if (!buf || buf_len == 0) return 0;

    size_t hdr_len = 0;
    if (!find_double_crlf_len(buf, buf_len, &hdr_len)) return 0;
    
    int result = parse_request_line(buf, buf_len, method, target, http_version);
    if (result <= 0) return result;

    *headers_end = hdr_len;
    
    return 1;
}


char *parse_host_header(const char *headers_start, size_t headers_len) {
    if (!headers_start || headers_len == 0) return NULL;

    size_t pos = 0;
    while (pos < headers_len) {
        size_t line_end = pos;
        while (line_end < headers_len && headers_start[line_end] != '\n') line_end++;

        size_t line_len = (line_end > pos) ? (line_end - pos) : 0;
        if (line_len > 0 && headers_start[pos + line_len - 1] == '\r') {
            line_len--;
        }

        if (line_len >= 5 && mem_case_prefix(headers_start + pos, line_len, "Host:")) {
            size_t v = pos + 5;
            while (v < pos + line_len && (headers_start[v] == ' ' || headers_start[v] == '\t')) v++;
            size_t v_end = pos + line_len;
            while (v_end > v && (headers_start[v_end - 1] == ' ' || headers_start[v_end - 1] == '\t')) v_end--;
            if (v_end <= v) return NULL;

            size_t host_len = v_end - v;
            char *host = (char *)malloc(host_len + 1);
            if (!host) return NULL;
            memcpy(host, headers_start + v, host_len);
            host[host_len] = '\0';
            return host;
        }

        if (line_end >= headers_len) break;
        pos = line_end + 1;
    }

    return NULL;
}
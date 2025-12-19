#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <errno.h>

#include "net.h"

#define DEFAULT_PORT 80

int net_listen(int port) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd == -1) {
        fprintf(stderr, "[NET] Socket creation failed: %s\n", strerror(errno));
        return -1;
    }
    
    int opt = 1;
    if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        fprintf(stderr, "[NET] setsockopt faiюled: %s\n", strerror(errno));
        close(fd);
        return -1;
    }
    
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(port);
    
    if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        fprintf(stderr, "[NET] Bind failed: %s\n", strerror(errno));
        close(fd);
        return -1;
    }
    
    if (listen(fd, 1024) == -1) {
        fprintf(stderr, "[NET] Listen failed: %s\n", strerror(errno));
        close(fd);
        return -1;
    }
    
    fprintf(stdout, "[NET] Listen socket created: fd=%d, port=%d\n", fd, port);
    
    return fd;
}

int net_parse_host_port(const char *host_str, char **out_host, int *out_port) {
    if (!host_str || !out_host || !out_port) return -1;
    
    char *str = (char *)malloc(strlen(host_str) + 1);
    if (!str) return -1;
    
    strcpy(str, host_str);

    //host:port
    char *port_pos = strrchr(str, ':');
    if (port_pos) {
        *port_pos = '\0';
        *out_port = atoi(port_pos + 1);
    } else {
        *out_port = DEFAULT_PORT;
    }
    
    *out_host = str;
    
    return 0;
}
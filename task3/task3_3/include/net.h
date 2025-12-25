#ifndef __NET_H__
#define __NET_H__

int net_listen(int port);

int net_listen_on(const char *bind_ip, int port);

int net_parse_host_port(const char *host_str, char **out_host, int *out_port);

int net_set_nonblocking(int fd, const char *tag);

#endif
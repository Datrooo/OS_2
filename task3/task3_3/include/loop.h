#ifndef __LOOP_H__
#define __LOOP_H__

#include "types.h"

int loop_create(const char *bind_ip, int listen_port);

int loop_run(void);

void loop_stop(void);

void loop_destroy(void);

void loop_notify_dirty(void);

int loop_wait_connect(int fd, double timeout_sec, int *out_soerr);

#endif
#ifndef __LOOP_H__
#define __LOOP_H__

#include "types.h"

int loop_init(const char *bind_ip, int listen_port);

int loop_run(void);

void loop_stop(void);

void loop_shutdown(void);

struct ev_loop *loop_get(void);

void loop_notify_dirty(void);

#endif
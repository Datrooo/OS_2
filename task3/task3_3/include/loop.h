#ifndef __LOOP_H__
#define __LOOP_H__

#include "types.h"

int loop_init(int listen_port);

int loop_run(void);

void loop_stop(void);

struct ev_loop *loop_get(void);

void loop_notify_dirty(void);

#endif
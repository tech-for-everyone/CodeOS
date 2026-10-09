#ifndef AI_H
#define AI_H

#include "types.h"

/* Ziggy AI transport — the brain is a Python backend on the dev host
 * (Ziggy repo: backend/, port 8975). The kernel dials the host loopback
 * through the QEMU user-net gateway: 10.0.2.2 maps to the host's
 * loopback, so server.py listening on 127.0.0.1 is reachable from the
 * guest (bind 0.0.0.0 if you want to reach it from outside). */

#define ZIGGY_AI_HOST "10.0.2.2"
#define ZIGGY_AI_PORT 8975

int ai_query(const char *prompt, char *response, int max_len);

#endif
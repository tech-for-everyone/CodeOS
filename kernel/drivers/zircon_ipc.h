#ifndef ZIRCON_IPC_H
#define ZIRCON_IPC_H

#include "types.h"

#define ZIRCON_IPC_MAGIC  0x5A495243   /* "ZIRC" */
#define ZIRCON_IPC_MAX_MSG 256

typedef enum {
    ZIRCON_IPC_NONE = 0,
    ZIRCON_IPC_NOTIFY,        /* kernel → user: new notification */
    ZIRCON_IPC_APP_LAUNCH,    /* kernel → user: app launch request */
    ZIRCON_IPC_APP_CLOSE,     /* kernel → user: app close request */
    ZIRCON_IPC_TOUCH_EVENT,   /* kernel → user: touch/gesture event */
    ZIRCON_IPC_CMD_RESP,      /* user → kernel: command response */
    ZIRCON_IPC_APP_LIST,      /* user → kernel: available app list */
    ZIRCON_IPC_QS_TOGGLE,     /* user → kernel: toggle quick setting */
} zircon_ipc_type_t;

typedef struct {
    zircon_ipc_type_t type;
    int x, y, w, h;
    int id;
    char text[128];
} zircon_ipc_msg_t;

typedef struct {
    uint32_t magic;
    volatile uint32_t head;
    volatile uint32_t tail;
    zircon_ipc_msg_t msgs[16];
} zircon_ipc_ring_t;

/* Kernel API */
void zircon_ipc_init(void);
int  zircon_ipc_send(const zircon_ipc_msg_t *msg);
int  zircon_ipc_recv(zircon_ipc_msg_t *msg);
int  zircon_ipc_available(void);
void zircon_ipc_dump(void);

/* Shared memory page exported for userspace */
extern zircon_ipc_ring_t *zircon_ipc_ring;

#endif

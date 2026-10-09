#ifndef NIC_H
#define NIC_H

#include "types.h"

#define NIC_NAME_MAX 32
#define NIC_MAX 6

typedef struct {
    const char *name;

    int (*probe)(void);

    int (*send)(const void *data, int len);
    int (*recv)(void *buf, int max_len);

    void (*get_mac)(uint8_t *mac);
} nic_driver_t;

void nic_register(nic_driver_t *drv);

int nic_init(void);
int nic_send(const void *data, int len);
int nic_recv(void *buf, int max_len);
void nic_get_mac(uint8_t *mac);

int nic_ready(void);
const char *nic_name(void);

#endif
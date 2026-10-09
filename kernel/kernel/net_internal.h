#ifndef NET_INTERNAL_H
#define NET_INTERNAL_H

#include "types.h"

uint32_t net_get_ip(void);
uint32_t net_get_gateway(void);
uint32_t net_get_dns(void);
void net_get_mac(uint8_t *mac);

#endif
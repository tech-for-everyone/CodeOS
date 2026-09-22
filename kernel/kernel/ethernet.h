#ifndef ETHERNET_H
#define ETHERNET_H
#include "types.h"
#include "spinlock.h"

#define ETH_ALEN           6
#define ETH_HLEN          14
#define ETH_FRAME_MIN     60
#define ETH_FRAME_MAX   1514
#define ETH_MTU        1500
#define ETH_P_ALL      0x0003
#define ETH_P_IP       0x0800
#define ETH_P_ARP      0x0806
#define ETH_P_IPV6     0x86DD
#define ETH_P_8021Q    0x8100
#define ETH_P_LLDP     0x88CC
#define ETH_P_PAUSE     0x8808

typedef struct {
    uint8_t  dst[ETH_ALEN];
    uint8_t  src[ETH_ALEN];
    uint16_t type;
} __attribute__((packed)) eth_frame_t;

typedef struct {
    uint8_t  dst[ETH_ALEN];
    uint8_t  src[ETH_ALEN];
    uint16_t tpid;
    uint16_t tci;
    uint16_t inner_type;
} __attribute__((packed)) eth_vlan_frame_t;

typedef struct {
    uint64_t rx_packets;
    uint64_t tx_packets;
    uint64_t rx_bytes;
    uint64_t tx_bytes;
    uint64_t rx_errors;
    uint64_t tx_errors;
    uint64_t rx_dropped;
    uint64_t tx_dropped;
    uint64_t rx_broadcast;
    uint64_t tx_broadcast;
    uint64_t rx_multicast;
    uint64_t tx_multicast;
    uint64_t collisions;
    uint64_t rx_overruns;
} eth_stats_t;

typedef void (*eth_handler_t)(uint16_t type, const uint8_t *src, const uint8_t *data, int len, void *ctx);

typedef struct eth_hook {
    uint16_t type;
    eth_handler_t handler;
    void *ctx;
    struct eth_hook *next;
} eth_hook_t;

int  eth_init(void);
int  eth_send(const uint8_t *dst, uint16_t type, const void *payload, uint16_t len);
int  eth_send_raw(const uint8_t *frame, int len);
int  eth_send_vlan(uint16_t vlan_id, const uint8_t *dst, uint16_t type, const void *payload, uint16_t len);
void eth_recv(const uint8_t *frame, int len);
int  eth_register_handler(uint16_t type, eth_handler_t handler, void *ctx);
int  eth_unregister_handler(uint16_t type);
void eth_get_stats(eth_stats_t *stats);
void eth_reset_stats(void);
void eth_get_addr(uint8_t *mac);
void eth_set_addr(const uint8_t *mac);
int  eth_ready(void);
int  eth_set_promiscuous(int on);
int  eth_set_mtu(int mtu);
void eth_dump_frame(const uint8_t *frame, int len);
void eth_periodic(void);

#endif

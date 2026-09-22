#ifndef NETDEV_H
#define NETDEV_H
#include "types.h"

#define NETDEV_MAX       8
#define NETDEV_NAME_LEN 16
#define NETDEV_MAC_LEN   6

#define NETDEV_UP        0x0001
#define NETDEV_BROADCAST 0x0002
#define NETDEV_PROMISC   0x0004
#define NETDEV_MULTICAST 0x0008
#define NETDEV_RUNNING   0x0010
#define NETDEV_LOWER_UP  0x0020

#define NETDEV_ETH       0
#define NETDEV_LOOPBACK  1
#define NETDEV_TUN       2

typedef struct {
    char     name[NETDEV_NAME_LEN];
    int      type;
    int      index;
    int      flags;
    uint8_t  addr[NETDEV_MAC_LEN];
    uint8_t  broadcast[NETDEV_MAC_LEN];
    uint32_t ip_addr;
    uint32_t netmask;
    uint32_t gateway;
    uint32_t dns;
    int      mtu;
    int      metric;
    uint64_t rx_bytes;
    uint64_t tx_bytes;
    uint64_t rx_packets;
    uint64_t tx_packets;
    uint64_t rx_errors;
    uint64_t tx_errors;
    uint64_t rx_dropped;
    uint64_t tx_dropped;
    int      (*send)(const void *data, int len);
    int      (*recv)(void *buf, int max_len);
    void     (*get_mac)(uint8_t *mac);
    int      (*set_mtu)(int mtu);
    int      (*set_promisc)(int on);
    void     *priv;
} netdev_t;

int      netdev_init(void);
int      netdev_register(const char *name, int type, const netdev_t *ops);
int      netdev_unregister(const char *name);
netdev_t *netdev_get(const char *name);
netdev_t *netdev_get_by_index(int index);
int      netdev_get_count(void);
int      netdev_up(const char *name);
int      netdev_down(const char *name);
int      netdev_set_ip(const char *name, uint32_t ip);
int      netdev_set_netmask(const char *name, uint32_t mask);
int      netdev_set_gateway(const char *name, uint32_t gw);
int      netdev_set_dns(const char *name, uint32_t dns);
int      netdev_set_mtu(const char *name, int mtu);
int      netdev_set_promisc(const char *name, int on);
int      netdev_send(const char *name, const void *data, int len);
int      netdev_recv(const char *name, void *buf, int max_len);
void     netdev_get_stats(const char *name, uint64_t *rxp, uint64_t *txp,
                          uint64_t *rxb, uint64_t *txb);
void     netdev_dump(void);
int      netdev_ready(const char *name);
netdev_t *netdev_default(void);
void     netdev_set_default(const char *name);

#endif

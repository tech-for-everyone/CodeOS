#ifndef IP_H
#define IP_H
#include "types.h"

#define IP_VERSION      4
#define IP_IHL_MIN      5
#define IP_HDR_LEN     20
#define IP_TTL_DEFAULT  64
#define IP_TTL_MAX     255
#define IP_DF         0x4000
#define IP_MF         0x2000
#define IP_OFFSET_MASK 0x1FFF
#define IP_OPT_RR       7
#define IP_OPT_SSRR    137
#define IP_OPT_LSRR    131
#define IP_OPT_TS      68

#define IP_PROTO_ICMP   1
#define IP_PROTO_IGMP   2
#define IP_PROTO_TCP    6
#define IP_PROTO_UDP   17
#define IP_PROTO_RAW  255

#define RT_TABLE_SIZE    16
#define RT_DEFAULT       0
#define RT_LOOPBACK      1
#define RT_LOCAL         2

typedef struct {
    uint8_t  ver_ihl;
    uint8_t  tos;
    uint16_t total_len;
    uint16_t id;
    uint16_t flags_frag;
    uint8_t  ttl;
    uint8_t  protocol;
    uint16_t checksum;
    uint32_t src;
    uint32_t dst;
} __attribute__((packed)) ip_packet_t;

typedef struct {
    uint32_t dst;
    uint32_t gateway;
    uint32_t netmask;
    int      metric;
    int      interface;
    int      type;
    int      flags;
    int      refcount;
    uint64_t use;
} ip_route_t;

typedef struct {
    uint64_t rx_packets;
    uint64_t tx_packets;
    uint64_t rx_bytes;
    uint64_t tx_bytes;
    uint64_t rx_errors;
    uint64_t tx_errors;
    uint64_t rx_dropped;
    uint64_t tx_dropped;
    uint64_t forwarded;
    uint64_t delivered;
    uint64_t fragmented;
    uint64_t reassembled;
    uint64_t frag_failed;
    uint64_t no_route;
    uint64_t ttl_expired;
    uint64_t bad_checksum;
    uint64_t bad_header;
    uint64_t unknown_proto;
} ip_stats_t;

int  ip_init(void);
int  ip_send(uint32_t dst, uint8_t proto, const void *data, uint16_t len);
int  ip_send_to(uint32_t src, uint32_t dst, uint8_t proto, const void *data, uint16_t len);
int  ip_send_fragmented(uint32_t dst, uint8_t proto, const void *data, uint16_t len);
void ip_recv(const uint8_t *pkt, int len, uint32_t src_mac_ip);
int  ip_route_add(uint32_t dst, uint32_t gw, uint32_t mask, int metric, int iface);
int  ip_route_del(uint32_t dst, uint32_t mask);
int  ip_route_lookup(uint32_t dst, ip_route_t *route);
void ip_route_default(uint32_t gw, int iface);
int  ip_route_table(ip_route_t *table, int max);
void ip_set_addr(uint32_t ip);
void ip_set_netmask(uint32_t mask);
void ip_set_gateway(uint32_t gw);
uint32_t ip_get_addr(void);
uint32_t ip_get_netmask(void);
uint32_t ip_get_gateway(void);
int  ip_is_local(uint32_t ip);
int  ip_is_broadcast(uint32_t ip);
uint16_t ip_checksum(const void *data, int len);
uint16_t ip_csum(const void *data, int len);
void ip_get_stats(ip_stats_t *stats);
void ip_reset_stats(void);
void ip_dump_routes(void);
void ip_periodic(void);
void ip_handle_icmp(uint32_t src, uint8_t type, uint8_t code, const void *data, int len);

#endif

#ifndef DHCP_H
#define DHCP_H
#include "types.h"

#define DHCP_STATE_INIT        0
#define DHCP_STATE_SELECTING   1
#define DHCP_STATE_REQUESTING  2
#define DHCP_STATE_BOUND       3
#define DHCP_STATE_RENEWING    4
#define DHCP_STATE_REBINDING   5

#define DHCP_PORT_CLIENT      68
#define DHCP_PORT_SERVER      67
#define DHCP_MAGIC           0x63825363
#define DHCP_MAX_OPTIONS    312

#define DHCP_OPT_PAD           0
#define DHCP_OPT_SUBNET_MASK    1
#define DHCP_OPT_ROUTER         3
#define DHCP_OPT_DNS_SERVER     6
#define DHCP_OPT_HOSTNAME      12
#define DHCP_OPT_DOMAIN_NAME   15
#define DHCP_OPT_BROADCAST     28
#define DHCP_OPT_REQUESTED_IP  50
#define DHCP_OPT_LEASE_TIME    51
#define DHCP_OPT_MSG_TYPE      53
#define DHCP_OPT_SERVER_ID     54
#define DHCP_OPT_PARAM_LIST    55
#define DHCP_OPT_MAX_MSG_SIZE  57
#define DHCP_OPT_CLIENT_ID     61
#define DHCP_OPT_T1            58
#define DHCP_OPT_T2            59
#define DHCP_OPT_END         255

#define DHCP_MSG_DISCOVER  1
#define DHCP_MSG_OFFER     2
#define DHCP_MSG_REQUEST   3
#define DHCP_MSG_DECLINE   4
#define DHCP_MSG_ACK       5
#define DHCP_MSG_NAK       6
#define DHCP_MSG_RELEASE   7
#define DHCP_MSG_INFORM    8

typedef struct {
    uint8_t  op;
    uint8_t  htype;
    uint8_t  hlen;
    uint8_t  hops;
    uint32_t xid;
    uint16_t secs;
    uint16_t flags;
    uint32_t ciaddr;
    uint32_t yiaddr;
    uint32_t siaddr;
    uint32_t giaddr;
    uint8_t  chaddr[16];
    char     sname[64];
    char     file[128];
    uint32_t magic;
    uint8_t  options[DHCP_MAX_OPTIONS];
} __attribute__((packed)) dhcp_packet_t;

typedef struct {
    int      state;
    uint32_t xid;
    uint32_t offered_ip;
    uint32_t server_ip;
    uint32_t my_ip;
    uint32_t netmask;
    uint32_t gateway;
    uint32_t dns[4];
    int      dns_count;
    uint32_t broadcast;
    char     hostname[64];
    char     domain[64];
    uint32_t lease_time;
    uint32_t t1;
    uint32_t t2;
    uint64_t lease_obtained;
    uint64_t lease_expires;
    uint64_t t1_time;
    uint64_t t2_time;
    int      rebind_count;
} dhcp_client_t;

#endif

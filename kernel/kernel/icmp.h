#ifndef ICMP_H
#define ICMP_H
#include "types.h"

#define ICMP_ECHO_REPLY        0
#define ICMP_DEST_UNREACHABLE  3
#define ICMP_SOURCE_QUENCH     4
#define ICMP_REDIRECT          5
#define ICMP_ECHO_REQUEST      8
#define ICMP_TIME_EXCEEDED    11
#define ICMP_PARAMETER_PROB   12
#define ICMP_TIMESTAMP        13
#define ICMP_TIMESTAMP_REPLY  14
#define ICMP_INFO_REQUEST     15
#define ICMP_INFO_REPLY       16
#define ICMP_ADDRESS_REQUEST  17
#define ICMP_ADDRESS_REPLY    18

#define ICMP_NET_UNREACH      0
#define ICMP_HOST_UNREACH     1
#define ICMP_PROTOCOL_UNREACH 2
#define ICMP_PORT_UNREACH     3
#define ICMP_FRAG_NEEDED      4
#define ICMP_SRCROUTE_FAIL    5
#define ICMP_NET_UNKNOWN      6
#define ICMP_HOST_UNKNOWN     7
#define ICMP_HOST_ISOLATED    8
#define ICMP_NET_PROHIBITED   9
#define ICMP_HOST_PROHIBITED  10
#define ICMP_NET_TOS          11
#define ICMP_HOST_TOS         12

#define ICMP_TTL_EXCEED_TRANSIT   0
#define ICMP_TTL_EXCEED_REASSEMB  1

typedef struct {
    uint8_t  type;
    uint8_t  code;
    uint16_t checksum;
    uint16_t id;
    uint16_t seq;
} __attribute__((packed)) icmp_echo_t;

typedef struct {
    uint8_t  type;
    uint8_t  code;
    uint16_t checksum;
    uint32_t unused;
} __attribute__((packed)) icmp_error_t;

typedef struct {
    uint8_t  type;
    uint8_t  code;
    uint16_t checksum;
    uint32_t original_datagram[2];
} __attribute__((packed)) icmp_unreach_t;

typedef struct {
    uint8_t  type;
    uint8_t  code;
    uint16_t checksum;
    uint8_t  pointer;
    uint8_t  unused[3];
} __attribute__((packed)) icmp_redirect_t;

typedef struct {
    uint8_t  type;
    uint8_t  code;
    uint16_t checksum;
    uint32_t unused;
    uint8_t  ip[28];
} __attribute__((packed)) icmp_exceeded_t;

typedef struct {
    uint8_t  type;
    uint8_t  code;
    uint16_t checksum;
    uint16_t id;
    uint16_t seq;
    uint32_t originate;
    uint32_t receive;
    uint32_t transmit;
} __attribute__((packed)) icmp_timestamp_t;

typedef struct {
    uint64_t rx_msgs;
    uint64_t tx_msgs;
    uint64_t tx_echo;
    uint64_t rx_echo_reply;
    uint64_t rx_dest_unreach;
    uint64_t rx_time_exceeded;
    uint64_t rx_redirect;
    uint64_t rx_errors;
    uint64_t tx_errors;
    uint64_t timeout;
} icmp_stats_t;

int  icmp_init(void);
int  icmp_echo(uint32_t dst, int timeout_ms);
int  icmp_echo_seq(uint32_t dst, uint16_t seq, int timeout_ms);
void icmp_recv(const uint8_t *pkt, int len, uint32_t src_ip);
void icmp_send_echo(uint32_t dst, uint16_t id, uint16_t seq);
void icmp_send_unreachable(uint32_t dst, uint8_t code, const void *orig, int orig_len);
void icmp_send_redirect(uint32_t dst, uint32_t gw, const void *orig, int orig_len);
void icmp_send_time_exceeded(uint32_t dst, uint8_t code, const void *orig, int orig_len);
void icmp_send_quench(uint32_t dst);
void icmp_send_timestamp(uint32_t dst, uint16_t id);
int  icmp_timestamp(uint32_t dst, uint32_t *ts, int timeout_ms);
int  icmp_ping(uint32_t ip, int timeout_ms);
void icmp_get_stats(icmp_stats_t *stats);
void icmp_reset_stats(void);
void icmp_dump(void);

/* ── ICMP reply queue ──
 * icmp_recv() enqueues ECHO_REPLY here; icmp_echo_seq() dequeues from it
 * instead of racing nic_recv() against the netd thread. */
#define ICMP_REPLY_QUEUE_SIZE 16
typedef struct {
    uint16_t id;
    uint16_t seq;
    uint32_t src_ip;
    uint64_t timestamp;  /* ms when reply was queued */
} icmp_reply_entry_t;

void icmp_reply_queue_init(void);
int  icmp_reply_dequeue(uint16_t id, uint16_t seq, uint32_t *src_out);

#endif

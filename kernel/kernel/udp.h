#ifndef UDP_H
#define UDP_H
#include "types.h"

#define UDP_MAX_SOCKETS 32
#define UDP_MAX_DATA   1472
#define UDP_BUF_SIZE   4096
#define UDP_HDR_LEN      8

typedef struct {
    uint16_t src_port;
    uint16_t dst_port;
    uint16_t length;
    uint16_t checksum;
} __attribute__((packed)) udp_header_t;

typedef struct {
    int      id;
    uint16_t local_port;
    uint32_t local_addr;
    uint16_t remote_port;
    uint32_t remote_ip;
    int      bound;
    int      connected;
    int      pid;
    int      in_use;
    char     rx_buf[UDP_BUF_SIZE];
    int      rx_head;
    int      rx_tail;
    int      rx_count;
    uint32_t last_src_ip;
    uint16_t last_src_port;
} udp_socket_t;

typedef struct {
    uint64_t rx_packets;
    uint64_t tx_packets;
    uint64_t rx_bytes;
    uint64_t tx_bytes;
    uint64_t rx_errors;
    uint64_t tx_errors;
    uint64_t no_port;
    uint64_t checksum_err;
} udp_stats_t;

int  udp_init(void);
int  udp_socket_create(void);
int  udp_socket_close(int fd);
int  udp_get_local_port(int fd);
int  udp_bind(int fd, uint32_t addr, uint16_t port);
int  udp_connect(int fd, uint32_t addr, uint16_t port);
int  udp_send(int fd, const void *data, int len);
int  udp_sendto(int fd, const void *data, int len, uint32_t dst_ip, uint16_t dst_port);
int  udp_recv(int fd, void *buf, int len, uint32_t *src_ip, uint16_t *src_port);
int  udp_recvfrom(int fd, void *buf, int len, uint32_t *src_ip, uint16_t *src_port);
int  udp_recv_timeout(int fd, void *buf, int len, uint32_t *src_ip, uint16_t *src_port, uint64_t timeout_ms);
void udp_recv_packet(uint32_t src_ip, uint32_t dst_ip, const uint8_t *data, int len);
uint16_t udp_checksum(uint32_t src_ip, uint32_t dst_ip, uint16_t sport, uint16_t dport, uint16_t total_len, const void *data, int data_len);
void udp_get_stats(udp_stats_t *stats);
void udp_reset_stats(void);
void udp_dump_sockets(void);

#endif

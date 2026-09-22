#ifndef NET_H
#define NET_H

#include <stdint.h>

struct eth_hdr {
    uint8_t  dst_mac[6];
    uint8_t  src_mac[6];
    uint16_t type;
} __attribute__((packed));

struct arp_hdr {
    uint16_t htype;
    uint16_t ptype;
    uint8_t  hlen;
    uint8_t  plen;
    uint16_t oper;
    uint8_t  sha[6];
    uint32_t spa;
    uint8_t  tha[6];
    uint32_t tpa;
} __attribute__((packed));

struct ip_hdr {
    uint8_t  ver_ihl;
    uint8_t  dscp;
    uint16_t total_len;
    uint16_t id;
    uint16_t flags_frag;
    uint8_t  ttl;
    uint8_t  protocol;
    uint16_t checksum;
    uint32_t src_ip;
    uint32_t dst_ip;
} __attribute__((packed));

struct tcp_hdr {
    uint16_t src_port;
    uint16_t dst_port;
    uint32_t seq;
    uint32_t ack;
    uint8_t  offset;
    uint8_t  flags;
    uint16_t window;
    uint16_t checksum;
    uint16_t urgent;
} __attribute__((packed));

struct udp_hdr {
    uint16_t src_port;
    uint16_t dst_port;
    uint16_t len;
    uint16_t checksum;
} __attribute__((packed));

#define ETH_TYPE_ARP  0x0806
#define ETH_TYPE_IP   0x0800
#define ARP_REQUEST   1
#define ARP_REPLY     2
#define IP_PROTO_TCP  6
#define IP_PROTO_UDP  17
#define IP_PROTO_ICMP 1
#define TCP_FIN  (1 << 0)
#define TCP_SYN  (1 << 1)
#define TCP_RST  (1 << 2)
#define TCP_PSH  (1 << 3)
#define TCP_ACK  (1 << 4)

#define NET_BUF_LEN 2048
#define TCP_BUF_LEN 4096

int  net_init(void);
int  net_ready(void);
int  arp_resolve(uint32_t ip, uint8_t *mac);
void net_set_ip(uint32_t ip);
void net_set_gateway(uint32_t ip);
void net_set_dns(uint32_t ip);
void net_poll(void);
struct icmp_hdr {
    uint8_t  type;
    uint8_t  code;
    uint16_t checksum;
    uint16_t id;
    uint16_t seq;
} __attribute__((packed));

#define ICMP_ECHO_REPLY   0
#define ICMP_ECHO_REQUEST 8

int  icmp_ping(uint32_t ip, int timeout_ms);
int  http_get(const char *host, uint16_t port, const char *path, void *buf, uint16_t max_len);
typedef void (*http_progress_cb)(int received, int total_estimate);
int  http_get_with_progress(const char *host, uint16_t port, const char *path, void *buf, uint16_t max_len, http_progress_cb cb);
int  http_post(const char *host, uint16_t port, const char *path, const void *body, uint16_t body_len, void *buf, uint16_t max_len);
int  https_get(const char *host, uint16_t port, const char *path, void *buf, uint16_t max_len);
int  https_post(const char *host, uint16_t port, const char *path, const void *body, uint16_t body_len, void *buf, uint16_t max_len);
int  dhcp_configure(void);
int  dns_resolve(const char *hostname, uint32_t *ip);
void dns_cache_flush(void);
uint32_t dns_get_server(void);
const char *dns_get_server_str(void);
void dns_status(void);
void dns_cache_dump(void);

/* ── WebSocket (client, over the single-connection stack) ── */
#define WS_CONN_NONE 0
#define WS_CONN_OPEN 1

typedef struct {
    int state;
    int use_tls;
    int (*send)(void *ctx, const void *data, int len);
    int (*recv)(void *ctx, void *buf, int max_len, int timeout_ms);
    void *ctx;
} ws_client_t;

int  ws_connect(ws_client_t *w, const char *host, uint16_t port, int use_tls, const char *path);
int  ws_send_text(ws_client_t *w, const char *text);
int  ws_send_binary(ws_client_t *w, const void *data, int len);
int  ws_send_ping(ws_client_t *w, const void *data, int len);
int  ws_recv(ws_client_t *w, void *buf, int max_len, int timeout_ms);
void ws_close(ws_client_t *w);

/* ── TCP socket abstraction ── */
#define TCP_SOCK_CLOSED    0
#define TCP_SOCK_SYN_SENT  1
#define TCP_SOCK_ESTAB    2

typedef struct {
    int      state;
    uint32_t dst_ip;
    uint16_t src_port, dst_port;
    uint32_t seq, ack;
    int      rx_len;
    uint8_t  rx_buf[TCP_BUF_LEN];
} tcp_sock_t;

int  tcp_sock_connect(tcp_sock_t *s, uint32_t dst_ip, uint16_t dst_port);
int  tcp_sock_send(tcp_sock_t *s, const void *data, int len);
int  tcp_sock_recv(tcp_sock_t *s, void *buf, int max_len, int timeout_ms);
void tcp_sock_close(tcp_sock_t *s);

#endif

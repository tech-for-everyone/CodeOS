#ifndef TCP_H
#define TCP_H
#include "types.h"

#define TCP_MAX_SOCKETS 32
#define TCP_MAX_BACKLOG 16
#define TCP_MSS 1460
#define TCP_DEFAULT_WINDOW 4096
#define TCP_RETRIES 3
#define TCP_TIMEOUT_MS 3000
#define TCP_BUF_SIZE 4096

#define TCP_FIN 0x01
#define TCP_SYN 0x02
#define TCP_RST 0x04
#define TCP_PSH 0x08
#define TCP_ACK 0x10

typedef enum {
    TCP_CLOSED, TCP_LISTEN, TCP_SYN_SENT, TCP_SYN_RECEIVED,
    TCP_ESTABLISHED, TCP_CLOSE_WAIT, TCP_LAST_ACK,
    TCP_FIN_WAIT_1, TCP_FIN_WAIT_2, TCP_CLOSING, TCP_TIME_WAIT
} tcp_state_t;

typedef struct __attribute__((packed)) {
    uint16_t src_port, dst_port;
    uint32_t seq_num, ack_num;
    uint8_t data_offset_flags, flags;
    uint16_t window, checksum, urgent_ptr;
} tcp_header_t;

typedef struct {
    int id; uint16_t local_port, remote_port; uint32_t local_ip, remote_ip;
    tcp_state_t state; uint32_t seq_num, ack_num, recv_next_seq;
    uint16_t send_window, recv_window;
    char send_buf[TCP_BUF_SIZE], recv_buf[TCP_BUF_SIZE];
    int send_len, recv_len, send_off, recv_off;
    int retransmit_count; uint64_t timer;
    int pid, backlog, backlog_count, flags, in_use;
    int backlog_fds[TCP_MAX_BACKLOG];
} tcp_socket_t;

void tcp_init(void);
int  tcp_socket(void);
int  tcp_connect(uint32_t ip, uint16_t port);
int  tcp_bind(int fd, uint32_t ip, uint16_t port);
int  tcp_listen(int fd, int backlog);
int  tcp_accept(int fd);
int  tcp_pending(int fd);
int  tcp_send(int fd, const void *data, int len);
int  tcp_recv(int fd, void *buf, int len);
int  tcp_poll_recv(int fd, int timeout_ms);
int  tcp_close(int fd);
int  tcp_reset(int fd);
void tcp_timer_tick(void);
void tcp_process_packet(uint32_t src_ip, const void *pkt, int len);
tcp_state_t tcp_get_state(int fd);
int  tcp_socket_count(void);
int  tcp_get_peer(int fd, uint32_t *ip, uint16_t *port);
const char *tcp_get_state_name(tcp_state_t s);

#endif

#ifndef SOCKET_H
#define SOCKET_H
#include "types.h"

#define AF_INET   2
#define AF_INET6 10
#define AF_UNIX   1
#define AF_PACKET 17

#define SOCK_STREAM    1
#define SOCK_DGRAM     2
#define SOCK_RAW       3
#define SOCK_SEQPACKET 5

#define IPPROTO_IP    0
#define IPPROTO_ICMP  1
#define IPPROTO_TCP   6
#define IPPROTO_UDP  17
#define IPPROTO_RAW 255

#define SOL_SOCKET  1
#define SOL_IP      0

#define SO_REUSEADDR 1
#define SO_KEEPALIVE 2
#define SO_BROADCAST 6
#define SO_RCVBUF    7
#define SO_SNDBUF    8
#define SO_ERROR     4
#define SO_TYPE      3
#define SO_LINGER    13
#define SO_RCVTIMEO  20
#define SO_SNDTIMEO  21

#define MSG_PEEK     0x02
#define MSG_DONTROUTE 0x04
#define MSG_WAITALL  0x08
#define MSG_NOSIGNAL 0x4000

#define SOCK_MAX_BIND 128

typedef struct {
    uint32_t s_addr;
    uint16_t sin_port;
} sockaddr_in_t;

typedef struct {
    uint16_t sa_family;
    char     sa_data[14];
} sockaddr_t;

typedef struct {
    int      fd;
    int      domain;
    int      type;
    int      protocol;
    int      flags;
    int      bound;
    int      connected;
    int      listening;
    uint16_t local_port;
    uint32_t local_addr;
    uint16_t remote_port;
    uint32_t remote_addr;
    int      backlog;
    int      error;
    int      pid;
    int      in_use;
    uint64_t rx_bytes;
    uint64_t tx_bytes;
    uint64_t rx_packets;
    uint64_t tx_packets;
} socket_t;

int  socket_init(void);
int  socket_create(int domain, int type, int protocol);
int  socket_bind(int fd, const sockaddr_t *addr, int addrlen);
int  socket_listen(int fd, int backlog);
int  socket_accept(int fd, sockaddr_t *addr, int *addrlen);
int  socket_connect(int fd, const sockaddr_t *addr, int addrlen);
int  socket_send(int fd, const void *buf, int len, int flags);
int  socket_sendto(int fd, const void *buf, int len, int flags,
                   const sockaddr_t *addr, int addrlen);
int  socket_recv(int fd, void *buf, int len, int flags);
int  socket_recvfrom(int fd, void *buf, int len, int flags,
                     sockaddr_t *addr, int *addrlen);
int  socket_recvfrom_nb(int fd, void *buf, int len, int flags,
                        sockaddr_t *addr, int *addrlen);
int  socket_close(int fd);
int  socket_setsockopt(int fd, int level, int opt, const void *val, int len);
int  socket_getsockopt(int fd, int level, int opt, void *val, int *len);
int  socket_getsockname(int fd, sockaddr_t *addr, int *addrlen);
int  socket_getpeername(int fd, sockaddr_t *addr, int *addrlen);
int  socket_ioctl(int fd, int req, void *arg);
int  socket_select(int nfds, void *readfds, void *writefds, void *exceptfds, int timeout_ms);
socket_t *socket_get(int fd);
void socket_dump(void);

#endif

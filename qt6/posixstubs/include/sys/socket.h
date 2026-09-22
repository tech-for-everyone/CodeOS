/* CodeOS POSIX Stubs - Socket API (stub implementation) */

#ifndef _CODEOS_SYS_SOCKET_H
#define _CODEOS_SYS_SOCKET_H

#include <stdint.h>

/* Socket types */
#define SOCK_STREAM    1
#define SOCK_DGRAM     2
#define SOCK_RAW       3
#define SOCK_SEQPACKET 5

/* Protocol families */
#define PF_UNSPEC     0
#define PF_UNIX       1
#define PF_LOCAL      1
#define PF_INET       2
#define PF_INET6      10
#define PF_NETLINK    16

#define AF_UNSPEC     PF_UNSPEC
#define AF_UNIX       PF_UNIX
#define AF_LOCAL      PF_LOCAL
#define AF_INET       PF_INET
#define AF_INET6      PF_INET6
#define AF_NETLINK    PF_NETLINK

/* IP protocols */
#define IPPROTO_IP    0
#define IPPROTO_TCP   6
#define IPPROTO_UDP  17

/* Socket options */
#define SOL_SOCKET    1
#define SOL_TCP       6
#define SOL_UDP      17

#define SO_REUSEADDR  2
#define SO_KEEPALIVE  9
#define SO_LINGER    13
#define SO_RCVBUF    18
#define SO_SNDBUF    19
#define SO_ERROR     11
#define SO_TYPE       3
#define SO_REUSEPORT  15
#define SO_BINDTODEVICE 25

/* Address structures */
struct sockaddr {
    uint16_t sa_family;
    char sa_data[14];
};

struct sockaddr_in {
    uint16_t sin_family;
    uint16_t sin_port;
    uint32_t sin_addr;
    char sin_zero[8];
};

struct sockaddr_in6 {
    uint16_t sin6_family;
    uint16_t sin6_port;
    uint32_t sin6_flowinfo;
    uint8_t  sin6_addr[16];
    uint32_t sin6_scope_id;
};

struct sockaddr_un {
    uint16_t sun_family;
    char sun_path[108];
};

struct sockaddr_storage {
    uint16_t ss_family;
    char __ss_padding[126];
};

/* Address conversion */
struct in_addr {
    uint32_t s_addr;
};

struct hostent {
    char *h_name;
    char **h_aliases;
    int h_addrtype;
    int h_length;
    char **h_addr_list;
};

struct addrinfo {
    int ai_flags;
    int ai_family;
    int ai_socktype;
    int ai_protocol;
    int ai_addrlen;
    struct sockaddr *ai_addr;
    char *ai_canonname;
    struct addrinfo *ai_next;
};

struct msghdr {
    void *msg_name;
    int msg_namelen;
    struct iovec *msg_iov;
    int msg_iovlen;
    void *msg_control;
    int msg_controllen;
    int msg_flags;
};

struct iovec {
    void *iov_base;
    int iov_len;
};

/* Constants */
#define MSG_PEEK        0x02
#define MSG_OOB         0x01
#define MSG_DONTWAIT    0x40
#define MSG_WAITALL     0x100
#define MSG_NOSIGNAL    0x4000

#define SHUT_RD    0
#define SHUT_WR    1
#define SHUT_RDWR  2

#define INADDR_ANY       0
#define INADDR_NONE      ((uint32_t)-1)
#define INADDR_LOOPBACK  0x7f000001

#define INET_ADDRSTRLEN  16
#define INET6_ADDRSTRLEN 46

#define AI_PASSIVE     0x01
#define AI_CANONNAME   0x02
#define AI_NUMERICHOST 0x04

#define NI_NUMERICHOST 0x01
#define NI_NUMERICSERV 0x02

/* Stub socket functions - return errors since CodeOS has no networking stack yet */
static inline int socket(int domain, int type, int protocol) {
    (void)domain; (void)type; (void)protocol;
    return -1;  /* ENOSYS */
}

static inline int bind(int sockfd, const struct sockaddr *addr, int addrlen) {
    (void)sockfd; (void)addr; (void)addrlen;
    return -1;
}

static inline int listen(int sockfd, int backlog) {
    (void)sockfd; (void)backlog;
    return -1;
}

static inline int accept(int sockfd, struct sockaddr *addr, int *addrlen) {
    (void)sockfd; (void)addr; (void)addrlen;
    return -1;
}

static inline int connect(int sockfd, const struct sockaddr *addr, int addrlen) {
    (void)sockfd; (void)addr; (void)addrlen;
    return -1;
}

static inline int send(int sockfd, const void *buf, int len, int flags) {
    (void)sockfd; (void)buf; (void)len; (void)flags;
    return -1;
}

static inline int recv(int sockfd, void *buf, int len, int flags) {
    (void)sockfd; (void)buf; (void)len; (void)flags;
    return -1;
}

static inline int sendto(int sockfd, const void *buf, int len, int flags,
                         const struct sockaddr *dest_addr, int addrlen) {
    (void)sockfd; (void)buf; (void)len; (void)flags;
    (void)dest_addr; (void)addrlen;
    return -1;
}

static inline int recvfrom(int sockfd, void *buf, int len, int flags,
                           struct sockaddr *src_addr, int *addrlen) {
    (void)sockfd; (void)buf; (void)len; (void)flags;
    (void)src_addr; (void)addrlen;
    return -1;
}

static inline int sendmsg(int sockfd, const struct msghdr *msg, int flags) {
    (void)sockfd; (void)msg; (void)flags;
    return -1;
}

static inline int recvmsg(int sockfd, struct msghdr *msg, int flags) {
    (void)sockfd; (void)msg; (void)flags;
    return -1;
}

static inline int shutdown(int sockfd, int how) {
    (void)sockfd; (void)how;
    return -1;
}

static inline int close(int fd) {
    (void)fd;
    return 0;
}

static inline int getsockopt(int sockfd, int level, int optname,
                             void *optval, int *optlen) {
    (void)sockfd; (void)level; (void)optname;
    (void)optval; (void)optlen;
    return -1;
}

static inline int setsockopt(int sockfd, int level, int optname,
                             const void *optval, int optlen) {
    (void)sockfd; (void)level; (void)optname;
    (void)optval; (void)optlen;
    return -1;
}

static inline int getsockname(int sockfd, struct sockaddr *addr, int *addrlen) {
    (void)sockfd; (void)addr; (void)addrlen;
    return -1;
}

static inline int getpeername(int sockfd, struct sockaddr *addr, int *addrlen) {
    (void)sockfd; (void)addr; (void)addrlen;
    return -1;
}

/* Address conversion */
static inline uint32_t inet_addr(const char *cp) {
    (void)cp;
    return INADDR_NONE;
}

static inline char *inet_ntoa(struct in_addr in) {
    (void)in;
    static char buf[16] = "0.0.0.0";
    return buf;
}

static inline const char *inet_ntop(int af, const void *src, char *dst, int size) {
    (void)af; (void)src; (void)dst; (void)size;
    return 0;
}

static inline int inet_pton(int af, const char *src, void *dst) {
    (void)af; (void)src; (void)dst;
    return -1;
}

/* Name resolution */
static inline struct hostent *gethostbyname(const char *name) {
    (void)name;
    return 0;
}

static inline int getaddrinfo(const char *node, const char *service,
                              const struct addrinfo *hints, struct addrinfo **res) {
    (void)node; (void)service; (void)hints; (void)res;
    return -1;  /* EAI_NONAME */
}

static inline void freeaddrinfo(struct addrinfo *res) {
    (void)res;
}

static inline int getnameinfo(const struct sockaddr *sa, int salen,
                              char *host, int hostlen, char *serv, int servlen, int flags) {
    (void)sa; (void)salen; (void)host; (void)hostlen;
    (void)serv; (void)servlen; (void)flags;
    return -1;
}

/* Socketpair (for Qt event loop) */
static inline int socketpair(int domain, int type, int protocol, int sv[2]) {
    (void)domain; (void)type; (void)protocol;
    sv[0] = -1;
    sv[1] = -1;
    return -1;
}

#endif /* _CODEOS_SYS_SOCKET_H */

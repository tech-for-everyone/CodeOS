#ifndef COS_SOCKET_H
#define COS_SOCKET_H

#include <stdint.h>
#include <codeos/syscall_abi.h>

#define AF_INET       2
#define SOCK_STREAM   1
#define SOCK_DGRAM    2
#define IPPROTO_UDP  17
#define IPPROTO_TCP   6

/* codeos socket address ABI — matches the kernel socket layer layout:
 *   s_addr   offset 0, network-order value (10.0.2.3 -> 0x0A000203)
 *   sin_port offset 4, HOST byte order
 * The struct is 16 bytes so Linux-style addrlen=16 calls work. */
typedef struct {
    uint32_t s_addr;
    uint16_t sin_port;
    uint16_t sin_family;
    uint8_t  sin_zero[8];
} codeos_sockaddr_t;

static inline int sock_socket(int domain, int type, int proto) {
    int ret;
    __asm__ volatile("int $0x80" : "=a"(ret)
                     : "a"(SYSCALL_SOCKET_SOCKET), "D"(domain), "S"(type), "d"(proto) : "memory");
    return ret;
}

static inline int sock_close(int fd) {
    int ret;
    __asm__ volatile("int $0x80" : "=a"(ret) : "a"(SYSCALL_SOCKET_CLOSE), "D"(fd) : "memory");
    return ret;
}

static inline int sock_bind(int fd, const codeos_sockaddr_t *addr, int addrlen) {
    int ret;
    __asm__ volatile("int $0x80" : "=a"(ret)
                     : "a"(SYSCALL_SOCKET_BIND), "D"(fd), "S"(addr), "d"(addrlen) : "memory");
    return ret;
}

static inline int sock_connect(int fd, const codeos_sockaddr_t *addr, int addrlen) {
    int ret;
    __asm__ volatile("int $0x80" : "=a"(ret)
                     : "a"(SYSCALL_SOCKET_CONNECT), "D"(fd), "S"(addr), "d"(addrlen) : "memory");
    return ret;
}

static inline int sock_send(int fd, const void *buf, int len, int flags) {
    int ret;
    register uint64_t _a4 asm("r10") = (uint64_t)flags;
    __asm__ volatile("int $0x80" : "=a"(ret)
                     : "a"(SYSCALL_SOCKET_SEND), "D"(fd), "S"(buf), "d"(len),
                       "r"(_a4) : "memory");
    return ret;
}

static inline int sock_recv(int fd, void *buf, int len, int flags) {
    int ret;
    register uint64_t _a4 asm("r10") = (uint64_t)flags;
    __asm__ volatile("int $0x80" : "=a"(ret)
                     : "a"(SYSCALL_SOCKET_RECV), "D"(fd), "S"(buf), "d"(len),
                       "r"(_a4) : "memory");
    return ret;
}

static inline int sock_sendto(int fd, const void *buf, int len, int flags,
                              const codeos_sockaddr_t *addr, int addrlen) {
    int ret;
    register uint64_t _a4 asm("r10") = (uint64_t)flags;
    register uint64_t _a5 asm("r8")  = (uint64_t)addr;
    register uint64_t _a6 asm("r9")  = (uint64_t)addrlen;
    __asm__ volatile("int $0x80" : "=a"(ret)
                     : "a"(SYSCALL_SOCKET_SENDTO), "D"(fd), "S"(buf), "d"(len),
                       "r"(_a4), "r"(_a5), "r"(_a6) : "memory");
    return ret;
}

static inline int sock_recvfrom(int fd, void *buf, int len, int flags,
                                codeos_sockaddr_t *addr, unsigned int *addrlen) {
    int ret;
    register uint64_t _a4 asm("r10") = (uint64_t)flags;
    register uint64_t _a5 asm("r8")  = (uint64_t)addr;
    register uint64_t _a6 asm("r9")  = (uint64_t)addrlen;
    __asm__ volatile("int $0x80" : "=a"(ret)
                     : "a"(SYSCALL_SOCKET_RECVFROM), "D"(fd), "S"(buf), "d"(len),
                       "r"(_a4), "r"(_a5), "r"(_a6) : "memory");
    return ret;
}

static inline int sock_getsockname(int fd, codeos_sockaddr_t *addr, unsigned int *addrlen) {
    int ret;
    __asm__ volatile("int $0x80" : "=a"(ret)
                     : "a"(SYSCALL_SOCKET_GETSOCKNAME), "D"(fd), "S"(addr), "d"(addrlen) : "memory");
    return ret;
}

static inline int sock_getpeername(int fd, codeos_sockaddr_t *addr, unsigned int *addrlen) {
    int ret;
    __asm__ volatile("int $0x80" : "=a"(ret)
                     : "a"(SYSCALL_SOCKET_GETPEERNAME), "D"(fd), "S"(addr), "d"(addrlen) : "memory");
    return ret;
}

static inline int sock_setsockopt(int fd, int level, int opt, const void *val, int len) {
    int ret;
    register uint64_t _a4 asm("r10") = (uint64_t)val;
    register uint64_t _a5 asm("r8")  = (uint64_t)len;
    __asm__ volatile("int $0x80" : "=a"(ret)
                     : "a"(SYSCALL_SOCKET_SETSOCKOPT), "D"(fd), "S"(level), "d"(opt),
                       "r"(_a4), "r"(_a5) : "memory");
    return ret;
}

static inline int sock_getsockopt(int fd, int level, int opt, void *val,
                                  unsigned int *len) {
    int ret;
    register uint64_t _a4 asm("r10") = (uint64_t)val;
    register uint64_t _a5 asm("r8")  = (uint64_t)len;
    __asm__ volatile("int $0x80" : "=a"(ret)
                     : "a"(SYSCALL_SOCKET_GETSOCKOPT), "D"(fd), "S"(level), "d"(opt),
                       "r"(_a4), "r"(_a5) : "memory");
    return ret;
}

/* Dotted-quad -> network-order u32 (no libc dependency). */
static inline uint32_t cos_inet_addr(const char *ip) {
    uint32_t a[4] = {0, 0, 0, 0};
    int n = 0, cur = 0;
    const char *p = ip;
    while (*p) {
        if (*p == '.') { n++; cur = 0; p++; continue; }
        if (n > 3 || *p < '0' || *p > '9') return 0;
        cur = cur * 10 + (*p - '0');
        if (cur > 255) return 0;
        a[n] = (uint32_t)cur;
        p++;
    }
    return (a[0] << 24) | (a[1] << 16) | (a[2] << 8) | a[3];
}

#endif
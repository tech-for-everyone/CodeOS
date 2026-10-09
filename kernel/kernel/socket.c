#include "socket.h"
#include "tcp.h"
#include "udp.h"
#include "ip.h"
#include "net_internal.h"
#include "kprintf.h"
#include "string.h"
#include "spinlock.h"

static socket_t sockets[SOCK_MAX_BIND];
static spinlock_t sock_lock = SPINLOCK_INIT;
static int sock_tcp_fd[SOCK_MAX_BIND];
static int sock_udp_fd[SOCK_MAX_BIND];

static void sock_to_addr(sockaddr_t *addr, uint32_t ip, uint16_t port) {
    if (!addr) return;
    sockaddr_in_t *in = (sockaddr_in_t *)addr;
    in->s_addr = ip;
    in->sin_port = port;
}

static void addr_to_sock(const sockaddr_t *addr, uint32_t *ip, uint16_t *port) {
    if (!addr || !ip || !port) return;
    const sockaddr_in_t *in = (const sockaddr_in_t *)addr;
    *ip = in->s_addr;
    *port = in->sin_port;
}

int socket_init(void) {
    memset(sockets, 0, sizeof(sockets));
    memset(sock_tcp_fd, -1, sizeof(sock_tcp_fd));
    memset(sock_udp_fd, -1, sizeof(sock_udp_fd));
    kprintf("socket: init %d slots\n", SOCK_MAX_BIND);
    return 0;
}

socket_t *socket_get(int fd) {
    if (fd < 0 || fd >= SOCK_MAX_BIND) return 0;
    return sockets[fd].in_use ? &sockets[fd] : 0;
}

int socket_create(int domain, int type, int protocol) {
    (void)protocol;
    if (domain != AF_INET) return -1;
    spin_lock(&sock_lock);
    for (int i = 0; i < SOCK_MAX_BIND; i++) {
        if (!sockets[i].in_use) {
            memset(&sockets[i], 0, sizeof(socket_t));
            sockets[i].fd = i;
            sockets[i].domain = domain;
            sockets[i].type = type;
            sockets[i].in_use = 1;
            sock_tcp_fd[i] = -1;
            sock_udp_fd[i] = -1;
            spin_unlock(&sock_lock);
            return i;
        }
    }
    spin_unlock(&sock_lock);
    return -1;
}

int socket_close(int fd) {
    if (fd < 0 || fd >= SOCK_MAX_BIND) return -1;
    spin_lock(&sock_lock);
    if (!sockets[fd].in_use) { spin_unlock(&sock_lock); return -1; }
    int tcp_fd = sock_tcp_fd[fd];
    int udp_fd = sock_udp_fd[fd];
    sockets[fd].in_use = 0;
    sockets[fd].bound = 0;
    sockets[fd].connected = 0;
    sockets[fd].listening = 0;
    sock_tcp_fd[fd] = -1;
    sock_udp_fd[fd] = -1;
    spin_unlock(&sock_lock);
    if (tcp_fd >= 0) tcp_close(tcp_fd);
    if (udp_fd >= 0) udp_socket_close(udp_fd);
    return 0;
}

int socket_bind(int fd, const sockaddr_t *addr, int addrlen) {
    if (fd < 0 || fd >= SOCK_MAX_BIND) return -1;
    spin_lock(&sock_lock);
    if (!sockets[fd].in_use) { spin_unlock(&sock_lock); return -1; }
    int type = sockets[fd].type;
    spin_unlock(&sock_lock);
    if (!addr || addrlen < (int)sizeof(sockaddr_in_t)) return -1;
    uint32_t ip; uint16_t port;
    addr_to_sock(addr, &ip, &port);
    if (type == SOCK_STREAM) {
        int tcp_fd = tcp_socket();
        if (tcp_fd < 0) return -1;
        if (tcp_bind(tcp_fd, ip, port) < 0) { tcp_close(tcp_fd); return -1; }
        spin_lock(&sock_lock);
        sock_tcp_fd[fd] = tcp_fd;
        sockets[fd].local_addr = ip;
        sockets[fd].local_port = port;
        sockets[fd].bound = 1;
        spin_unlock(&sock_lock);
        return 0;
    } else if (type == SOCK_DGRAM) {
        int udp_fd = udp_socket_create();
        if (udp_fd < 0) return -1;
        if (udp_bind(udp_fd, ip, port) < 0) { udp_socket_close(udp_fd); return -1; }
        spin_lock(&sock_lock);
        sock_udp_fd[fd] = udp_fd;
        sockets[fd].local_addr = ip;
        sockets[fd].local_port = port;
        sockets[fd].bound = 1;
        spin_unlock(&sock_lock);
        return 0;
    }
    return -1;
}

int socket_connect(int fd, const sockaddr_t *addr, int addrlen) {
    if (fd < 0 || fd >= SOCK_MAX_BIND) return -1;
    spin_lock(&sock_lock);
    if (!sockets[fd].in_use) { spin_unlock(&sock_lock); return -1; }
    int type = sockets[fd].type;
    spin_unlock(&sock_lock);
    if (!addr || addrlen < (int)sizeof(sockaddr_in_t)) return -1;
    uint32_t ip; uint16_t port;
    addr_to_sock(addr, &ip, &port);
    if (type == SOCK_STREAM) {
        int tcp_fd = tcp_connect(ip, port);
        if (tcp_fd < 0) return -1;
        spin_lock(&sock_lock);
        sock_tcp_fd[fd] = tcp_fd;
        sockets[fd].remote_addr = ip;
        sockets[fd].remote_port = port;
        sockets[fd].connected = 1;
        spin_unlock(&sock_lock);
        return 0;
    } else if (type == SOCK_DGRAM) {
        int udp_fd = udp_socket_create();
        if (udp_fd < 0) return -1;
        if (udp_connect(udp_fd, ip, port) < 0) {
            udp_socket_close(udp_fd);
            return -1;
        }
        spin_lock(&sock_lock);
        sock_udp_fd[fd] = udp_fd;
        sockets[fd].remote_addr = ip;
        sockets[fd].remote_port = port;
        sockets[fd].connected = 1;
        sockets[fd].local_addr = net_get_ip();
        sockets[fd].local_port = (uint16_t)udp_get_local_port(udp_fd);
        sockets[fd].bound = 1;
        spin_unlock(&sock_lock);
        return 0;
    }
    return -1;
}

int socket_listen(int fd, int backlog) {
    if (fd < 0 || fd >= SOCK_MAX_BIND) return -1;
    spin_lock(&sock_lock);
    if (!sockets[fd].in_use) { spin_unlock(&sock_lock); return -1; }
    if (sockets[fd].type != SOCK_STREAM) { spin_unlock(&sock_lock); return -1; }
    int tcp_fd = sock_tcp_fd[fd];
    if (tcp_fd < 0) { spin_unlock(&sock_lock); return -1; }
    spin_unlock(&sock_lock);
    int r = tcp_listen(tcp_fd, backlog);
    if (r == 0) {
        spin_lock(&sock_lock);
        sockets[fd].listening = 1;
        sockets[fd].backlog = backlog;
        spin_unlock(&sock_lock);
    }
    return r;
}

int socket_accept(int fd, sockaddr_t *addr, int *addrlen) {
    if (fd < 0 || fd >= SOCK_MAX_BIND) return -1;
    spin_lock(&sock_lock);
    if (!sockets[fd].in_use) { spin_unlock(&sock_lock); return -1; }
    int tcp_fd = sock_tcp_fd[fd];
    spin_unlock(&sock_lock);
    if (tcp_fd < 0) return -1;
    int new_tcp = tcp_accept(tcp_fd);
    if (new_tcp < 0) return -1;
    spin_lock(&sock_lock);
    for (int i = 0; i < SOCK_MAX_BIND; i++) {
        if (!sockets[i].in_use) {
            memset(&sockets[i], 0, sizeof(socket_t));
            sockets[i].fd = i;
            sockets[i].domain = AF_INET;
            sockets[i].type = SOCK_STREAM;
            sockets[i].in_use = 1;
            sock_tcp_fd[i] = new_tcp;
            sock_udp_fd[i] = -1;
            uint32_t peer_ip = 0;
            uint16_t peer_port = 0;
            tcp_get_peer(new_tcp, &peer_ip, &peer_port);
            sockets[i].remote_addr = peer_ip;
            sockets[i].remote_port = peer_port;
            spin_unlock(&sock_lock);
            if (addr && addrlen && *addrlen >= (int)sizeof(sockaddr_in_t)) {
                sock_to_addr(addr, peer_ip, peer_port);
                *addrlen = sizeof(sockaddr_in_t);
            }
            return i;
        }
    }
    spin_unlock(&sock_lock);
    tcp_close(new_tcp);
    return -1;
}

int socket_send(int fd, const void *buf, int len, int flags) {
    (void)flags;
    if (fd < 0 || fd >= SOCK_MAX_BIND) return -1;
    spin_lock(&sock_lock);
    if (!sockets[fd].in_use) { spin_unlock(&sock_lock); return -1; }
    if (sockets[fd].type == SOCK_STREAM) {
        int tcp_fd = sock_tcp_fd[fd];
        spin_unlock(&sock_lock);
        return tcp_fd >= 0 ? tcp_send(tcp_fd, buf, len) : -1;
    }
    if (sockets[fd].type == SOCK_DGRAM) {
        int udp_fd = sock_udp_fd[fd];
        spin_unlock(&sock_lock);
        return udp_fd >= 0 ? udp_send(udp_fd, buf, len) : -1;
    }
    spin_unlock(&sock_lock);
    return -1;
}

int socket_sendto(int fd, const void *buf, int len, int flags, const sockaddr_t *addr, int addrlen) {
    (void)flags;
    if (fd < 0 || fd >= SOCK_MAX_BIND) return -1;
    spin_lock(&sock_lock);
    if (!sockets[fd].in_use) { spin_unlock(&sock_lock); return -1; }
    int udp_fd = sock_udp_fd[fd];
    spin_unlock(&sock_lock);
    if (udp_fd < 0) return -1;
    if (!addr || addrlen < (int)sizeof(sockaddr_in_t)) return -1;
    uint32_t ip; uint16_t port;
    addr_to_sock(addr, &ip, &port);
    return udp_sendto(udp_fd, buf, len, ip, port);
}

int socket_recv(int fd, void *buf, int len, int flags) {
    (void)flags;
    if (fd < 0 || fd >= SOCK_MAX_BIND) return -1;
    spin_lock(&sock_lock);
    if (!sockets[fd].in_use) { spin_unlock(&sock_lock); return -1; }
    if (sockets[fd].type == SOCK_STREAM) {
        int tcp_fd = sock_tcp_fd[fd];
        spin_unlock(&sock_lock);
        return tcp_fd >= 0 ? tcp_recv(tcp_fd, buf, len) : -1;
    }
    if (sockets[fd].type == SOCK_DGRAM) {
        int udp_fd = sock_udp_fd[fd];
        spin_unlock(&sock_lock);
        return udp_fd >= 0 ? udp_recv(udp_fd, buf, len, 0, 0) : -1;
    }
    spin_unlock(&sock_lock);
    return -1;
}

int socket_recvfrom(int fd, void *buf, int len, int flags, sockaddr_t *addr, int *addrlen) {
    (void)flags;
    if (fd < 0 || fd >= SOCK_MAX_BIND) return -1;
    spin_lock(&sock_lock);
    if (!sockets[fd].in_use) { spin_unlock(&sock_lock); return -1; }
    int udp_fd = sock_udp_fd[fd];
    spin_unlock(&sock_lock);
    if (udp_fd < 0) return -1;
    uint32_t sip = 0; uint16_t sport = 0;
    int r = udp_recvfrom(udp_fd, buf, len, &sip, &sport);
    if (r >= 0 && addr && addrlen && *addrlen >= (int)sizeof(sockaddr_in_t)) {
        sock_to_addr(addr, sip, sport);
        *addrlen = sizeof(sockaddr_in_t);
    }
    return r;
}

/* Non-blocking receive: returns -1 immediately when no datagram is queued.
 * Used by the Linux-compat recvfrom, whose callers poll and expect EAGAIN
 * rather than a multi-second block. */
int socket_recvfrom_nb(int fd, void *buf, int len, int flags, sockaddr_t *addr, int *addrlen) {
    (void)flags;
    if (fd < 0 || fd >= SOCK_MAX_BIND) return -1;
    spin_lock(&sock_lock);
    if (!sockets[fd].in_use) { spin_unlock(&sock_lock); return -1; }
    int type = sockets[fd].type;
    int udp_fd = sock_udp_fd[fd];
    spin_unlock(&sock_lock);
    if (type == SOCK_STREAM) {
        /* No non-blocking TCP path yet; fall back to the existing receive. */
        return socket_recv(fd, buf, len, flags);
    }
    if (udp_fd < 0) return -1;
    uint32_t sip = 0; uint16_t sport = 0;
    int r = udp_recv_timeout(udp_fd, buf, len, &sip, &sport, 0);
    if (r >= 0 && addr && addrlen && *addrlen >= (int)sizeof(sockaddr_in_t)) {
        sock_to_addr(addr, sip, sport);
        *addrlen = sizeof(sockaddr_in_t);
    }
    return r;
}

int socket_setsockopt(int fd, int level, int opt, const void *val, int len) {
    (void)level; (void)opt; (void)val; (void)len;
    if (fd < 0 || fd >= SOCK_MAX_BIND || !sockets[fd].in_use) return -1;
    return 0;
}

int socket_getsockopt(int fd, int level, int opt, void *val, int *len) {
    (void)level; (void)opt; (void)val; (void)len;
    if (fd < 0 || fd >= SOCK_MAX_BIND || !sockets[fd].in_use) return -1;
    return 0;
}

int socket_getsockname(int fd, sockaddr_t *addr, int *addrlen) {
    if (fd < 0 || fd >= SOCK_MAX_BIND || !sockets[fd].in_use) return -1;
    if (addr && addrlen && *addrlen >= (int)sizeof(sockaddr_in_t)) {
        sock_to_addr(addr, sockets[fd].local_addr, sockets[fd].local_port);
    }
    return 0;
}

int socket_getpeername(int fd, sockaddr_t *addr, int *addrlen) {
    if (fd < 0 || fd >= SOCK_MAX_BIND || !sockets[fd].in_use) return -1;
    if (addr && addrlen && *addrlen >= (int)sizeof(sockaddr_in_t)) {
        sock_to_addr(addr, sockets[fd].remote_addr, sockets[fd].remote_port);
    }
    return 0;
}

int socket_ioctl(int fd, int req, void *arg) {
    (void)fd; (void)req; (void)arg;
    return -1;
}

int socket_select(int nfds, void *readfds, void *writefds, void *exceptfds, int timeout_ms) {
    (void)nfds; (void)readfds; (void)writefds; (void)exceptfds; (void)timeout_ms;
    return -1;
}

void socket_dump(void) {
    kprintf("BSD Sockets:\n");
    for (int i = 0; i < SOCK_MAX_BIND; i++) {
        if (!sockets[i].in_use) continue;
        kprintf("  [%d] domain=%d type=%d bound=%d conn=%d listen=%d tcp_fd=%d udp_fd=%d\n",
                i, sockets[i].domain, sockets[i].type, sockets[i].bound,
                sockets[i].connected, sockets[i].listening,
                sock_tcp_fd[i], sock_udp_fd[i]);
    }
}

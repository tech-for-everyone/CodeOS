#include "udp.h"
#include "ip.h"
#include "ethernet.h"
#include "arp.h"
#include "kprintf.h"
#include "string.h"
#include "spinlock.h"
#include "sched.h"
#include "timer.h"
#include "net_internal.h"
#include "process.h"

static udp_socket_t sockets[UDP_MAX_SOCKETS];
static spinlock_t udp_lock = SPINLOCK_INIT;
static udp_stats_t stats;

int udp_init(void) {
    memset(sockets, 0, sizeof(sockets));
    memset(&stats, 0, sizeof(stats));
    kprintf("udp: init %d sockets\n", UDP_MAX_SOCKETS);
    return 0;
}

int udp_socket_create(void) {
    spin_lock(&udp_lock);
    for (int i = 0; i < UDP_MAX_SOCKETS; i++) {
        if (!sockets[i].in_use) {
            memset(&sockets[i], 0, sizeof(udp_socket_t));
            sockets[i].id = i;
            sockets[i].in_use = 1;
            spin_unlock(&udp_lock);
            return i;
        }
    }
    spin_unlock(&udp_lock);
    return -1;
}

int udp_socket_close(int fd) {
    if (fd < 0 || fd >= UDP_MAX_SOCKETS) return -1;
    spin_lock(&udp_lock);
    sockets[fd].in_use = 0;
    sockets[fd].bound = 0;
    sockets[fd].connected = 0;
    spin_unlock(&udp_lock);
    return 0;
}

int udp_get_local_port(int fd) {
    if (fd < 0 || fd >= UDP_MAX_SOCKETS) return -1;
    spin_lock(&udp_lock);
    if (!sockets[fd].in_use) { spin_unlock(&udp_lock); return -1; }
    uint16_t port = sockets[fd].local_port;
    spin_unlock(&udp_lock);
    if (!port) port = 49152 + (uint16_t)(fd * 137 + 42);
    return port;
}

int udp_bind(int fd, uint32_t addr, uint16_t port) {
    if (fd < 0 || fd >= UDP_MAX_SOCKETS) return -1;
    spin_lock(&udp_lock);
    if (!sockets[fd].in_use) { spin_unlock(&udp_lock); return -1; }
    for (int i = 0; i < UDP_MAX_SOCKETS; i++) {
        if (i != fd && sockets[i].in_use && sockets[i].bound && sockets[i].local_port == port) {
            spin_unlock(&udp_lock);
            return -1;
        }
    }
    sockets[fd].local_port = port;
    sockets[fd].local_addr = addr;
    sockets[fd].bound = 1;
    spin_unlock(&udp_lock);
    return 0;
}

int udp_connect(int fd, uint32_t addr, uint16_t port) {
    if (fd < 0 || fd >= UDP_MAX_SOCKETS) return -1;
    spin_lock(&udp_lock);
    if (!sockets[fd].in_use) { spin_unlock(&udp_lock); return -1; }
    sockets[fd].remote_ip = addr;
    sockets[fd].remote_port = port;
    sockets[fd].connected = 1;
    if (!sockets[fd].bound) {
        sockets[fd].local_port = 49152 + (uint16_t)(fd * 137 + 42);
        sockets[fd].local_addr = net_get_ip();
        sockets[fd].bound = 1;
    }
    spin_unlock(&udp_lock);
    return 0;
}

int udp_send(int fd, const void *data, int len) {
    if (fd < 0 || fd >= UDP_MAX_SOCKETS) return -1;
    spin_lock(&udp_lock);
    if (!sockets[fd].in_use || !sockets[fd].connected) { spin_unlock(&udp_lock); return -1; }
    uint32_t dip = sockets[fd].remote_ip;
    uint16_t dport = sockets[fd].remote_port;
    spin_unlock(&udp_lock);
    return udp_sendto(fd, data, len, dip, dport);
}

int udp_sendto(int fd, const void *data, int len, uint32_t dst_ip, uint16_t dst_port) {
    if (fd < 0 || fd >= UDP_MAX_SOCKETS) return -1;
    if (!data || len <= 0 || len > UDP_MAX_DATA) return -1;
    spin_lock(&udp_lock);
    if (!sockets[fd].in_use) { spin_unlock(&udp_lock); return -1; }
    uint16_t sport = sockets[fd].local_port;
    spin_unlock(&udp_lock);
    if (!sport) sport = 49152 + (uint16_t)(fd * 137 + 42);
    uint8_t pkt[2048];
    udp_header_t *udp = (udp_header_t *)pkt;
    uint16_t total_len = UDP_HDR_LEN + len;
    udp->src_port = __builtin_bswap16(sport);
    udp->dst_port = __builtin_bswap16(dst_port);
    udp->length = __builtin_bswap16(total_len);
    udp->checksum = 0;
    memcpy(pkt + UDP_HDR_LEN, data, len);
    udp->checksum = udp_checksum(net_get_ip(), dst_ip, sport, dst_port, total_len, data, len);
    spin_lock(&udp_lock);
    stats.tx_packets++;
    stats.tx_bytes += len;
    spin_unlock(&udp_lock);
    return ip_send(dst_ip, IP_PROTO_UDP, pkt, total_len);
}

int udp_recv(int fd, void *buf, int len, uint32_t *src_ip, uint16_t *src_port) {
    return udp_recv_timeout(fd, buf, len, src_ip, src_port, 5000);
}

int udp_recv_timeout(int fd, void *buf, int len, uint32_t *src_ip, uint16_t *src_port, uint64_t timeout_ms) {
    if (fd < 0 || fd >= UDP_MAX_SOCKETS) return -1;
    if (!buf || len <= 0) return -1;
    spin_lock(&udp_lock);
    if (!sockets[fd].in_use) { spin_unlock(&udp_lock); return -1; }
    /* timeout_ms == 0 means "poll once" (non-blocking): return immediately
     * instead of entering the sleep loop, which can stall for the full
     * timeout when no datagram ever arrives. */
    if (sockets[fd].rx_count <= 0 && timeout_ms == 0) {
        spin_unlock(&udp_lock);
        return -1;
    }
    uint64_t start = timer_get_milliseconds();
    while (sockets[fd].rx_count <= 0) {
        if (!sockets[fd].in_use) { spin_unlock(&udp_lock); return -1; }
        spin_unlock(&udp_lock);
        sched_sleep_ms(1);
        if (timer_get_milliseconds() - start > timeout_ms) return -1;
        spin_lock(&udp_lock);
        if (!sockets[fd].in_use) { spin_unlock(&udp_lock); return -1; }
    }
    int avail = sockets[fd].rx_count;
    int copy = avail < len ? avail : len;
    for (int i = 0; i < copy; i++) {
        ((uint8_t *)buf)[i] = sockets[fd].rx_buf[(sockets[fd].rx_head + i) % UDP_BUF_SIZE];
    }
    sockets[fd].rx_head = (sockets[fd].rx_head + copy) % UDP_BUF_SIZE;
    sockets[fd].rx_count -= copy;
    if (src_ip) *src_ip = sockets[fd].last_src_ip;
    if (src_port) *src_port = sockets[fd].last_src_port;
    spin_unlock(&udp_lock);
    return copy;
}

int udp_recvfrom(int fd, void *buf, int len, uint32_t *src_ip, uint16_t *src_port) {
    if (!src_ip || !src_port) return udp_recv(fd, buf, len, 0, 0);
    return udp_recv(fd, buf, len, src_ip, src_port);
}

void udp_recv_packet(uint32_t src_ip, uint32_t dst_ip, const uint8_t *data, int len) {
    if (!data || len < UDP_HDR_LEN) return;
    const udp_header_t *udp = (const udp_header_t *)data;
    int packet_len = __builtin_bswap16(udp->length);
    if (packet_len < UDP_HDR_LEN || packet_len > len) return;
    uint16_t dport = __builtin_bswap16(udp->dst_port);
    uint16_t sport = __builtin_bswap16(udp->src_port);
    int payload_len = packet_len - UDP_HDR_LEN;
    if (udp->checksum) {
        uint32_t chk_dst = dst_ip;
        uint16_t calc = udp_checksum(src_ip, chk_dst, sport, dport,
                                     (uint16_t)packet_len,
                                     data + UDP_HDR_LEN, payload_len);
        if (calc != udp->checksum && dst_ip == 0xFFFFFFFF) {
            chk_dst = net_get_ip();
            calc = udp_checksum(src_ip, chk_dst, sport, dport,
                                (uint16_t)packet_len,
                                data + UDP_HDR_LEN, payload_len);
        }
        if (calc != udp->checksum) {
            spin_lock(&udp_lock); stats.checksum_err++; stats.rx_errors++; spin_unlock(&udp_lock);
            return;
        }
    }
    spin_lock(&udp_lock);
    stats.rx_packets++;
    stats.rx_bytes += payload_len;
    int best = -1;
    int best_score = -1;
    for (int i = 0; i < UDP_MAX_SOCKETS; i++) {
        if (!sockets[i].in_use || !sockets[i].bound) continue;
        if (sockets[i].local_port != dport) continue;
        if (sockets[i].local_addr && sockets[i].local_addr != net_get_ip()) continue;
        if (sockets[i].connected && (sockets[i].remote_ip != src_ip || sockets[i].remote_port != sport)) continue;
        int score = sockets[i].connected ? 3 : (sockets[i].local_addr ? 2 : 1);
        if (score > best_score) { best = i; best_score = score; }
    }
    if (best >= 0) {
        int space = UDP_BUF_SIZE - sockets[best].rx_count;
        int copy = payload_len < space ? payload_len : space;
        if (copy > 0) {
            const uint8_t *payload = data + UDP_HDR_LEN;
            for (int j = 0; j < copy; j++)
                sockets[best].rx_buf[(sockets[best].rx_tail + j) % UDP_BUF_SIZE] = payload[j];
            sockets[best].rx_tail = (sockets[best].rx_tail + copy) % UDP_BUF_SIZE;
            sockets[best].rx_count += copy;
            sockets[best].last_src_ip = src_ip;
            sockets[best].last_src_port = sport;
        }
        spin_unlock(&udp_lock);
        return;
    }
    stats.no_port++;
    spin_unlock(&udp_lock);
}

uint16_t udp_checksum(uint32_t src_ip, uint32_t dst_ip, uint16_t sport, uint16_t dport, uint16_t total_len, const void *data, int data_len) {
    uint8_t pseudo[12];
    pseudo[0] = (uint8_t)(src_ip & 0xFF);
    pseudo[1] = (uint8_t)(src_ip >> 8);
    pseudo[2] = (uint8_t)(src_ip >> 16);
    pseudo[3] = (uint8_t)(src_ip >> 24);
    pseudo[4] = (uint8_t)(dst_ip & 0xFF);
    pseudo[5] = (uint8_t)(dst_ip >> 8);
    pseudo[6] = (uint8_t)(dst_ip >> 16);
    pseudo[7] = (uint8_t)(dst_ip >> 24);
    pseudo[8] = 0;
    pseudo[9] = IP_PROTO_UDP;
    pseudo[10] = (uint8_t)(total_len >> 8);
    pseudo[11] = (uint8_t)(total_len & 0xFF);
    uint32_t sum = 0;
    for (int i = 0; i < 12; i += 2) sum += (uint32_t)(pseudo[i] | (pseudo[i+1] << 8));
    sum += (uint32_t)__builtin_bswap16(sport);
    sum += (uint32_t)__builtin_bswap16(dport);
    sum += (uint32_t)__builtin_bswap16(total_len);
    const uint8_t *p = (const uint8_t *)data;
    for (int i = 0; i + 1 < data_len; i += 2) sum += (uint32_t)(p[i] | (p[i+1] << 8));
    if (data_len & 1) sum += p[data_len-1];
    while (sum >> 16) sum = (sum & 0xFFFF) + (sum >> 16);
    return (uint16_t)(~sum & 0xFFFF);
}

void udp_get_stats(udp_stats_t *s) { if (s) { spin_lock(&udp_lock); *s = stats; spin_unlock(&udp_lock); } }
void udp_reset_stats(void) { spin_lock(&udp_lock); memset(&stats, 0, sizeof(stats)); spin_unlock(&udp_lock); }

void udp_dump_sockets(void) {
    kprintf("UDP Sockets:\n");
    for (int i = 0; i < UDP_MAX_SOCKETS; i++) {
        if (!sockets[i].in_use) continue;
        kprintf("  [%d] port=%d bound=%d conn=%d rx=%d\n",
                i, sockets[i].local_port, sockets[i].bound, sockets[i].connected, sockets[i].rx_count);
    }
}

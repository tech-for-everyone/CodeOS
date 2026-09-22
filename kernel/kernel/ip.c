#include "ip.h"
#include "ethernet.h"
#include "arp.h"
#include "icmp.h"
#include "udp.h"
#include "tcp.h"
#include "kprintf.h"
#include "string.h"
#include "spinlock.h"
#include "sched.h"
#include "timer.h"
#include "net_internal.h"
#include "../drivers/nic.h"

static uint32_t my_ip_addr;
static uint32_t my_netmask = 0x00FFFFFF;
static uint32_t my_gateway;
static ip_route_t routes[RT_TABLE_SIZE];
static int route_count;
static ip_stats_t stats;
static spinlock_t ip_lock = SPINLOCK_INIT;
static uint16_t id_counter;

int ip_init(void) {
    memset(&stats, 0, sizeof(stats));
    memset(routes, 0, sizeof(routes));
    route_count = 0;
    id_counter = 0;
    ip_route_default(my_gateway, 0);
    kprintf("ip: init addr=%08x gw=%08x\n", my_ip_addr, my_gateway);
    return 0;
}

uint16_t ip_checksum(const void *data, int len) {
    const uint8_t *b = (const uint8_t *)data;
    uint32_t sum = 0;
    for (int i = 0; i + 1 < len; i += 2)
        sum += (uint32_t)(b[i] | (b[i+1] << 8));
    if (len & 1) sum += b[len-1];
    while (sum >> 16) sum = (sum & 0xFFFF) + (sum >> 16);
    return (uint16_t)(~sum & 0xFFFF);
}

uint16_t ip_csum(const void *data, int len) { return ip_checksum(data, len); }

/* Resolve a neighbour MAC, briefly waiting for ARP so a first packet is not
 * silently dropped while an ARP exchange is in flight. The wait must YIELD
 * (sched_sleep_ms, not timer_sleep_ms): timer_sleep_ms busy-loops on hlt
 * without giving netd a chance to process ARP replies, turning a cold-cache
 * miss into a several-second storm of unanswered requests. Bounded, since the
 * gateway/peers are normally pre-seeded and TCP/UDP callers have their own
 * retransmit backstop. */
static int arp_wait(uint8_t *mac, uint32_t ip) {
    for (int i = 0; i < 60; i++) {   /* ~0.3-3s for an ARP reply to arrive */
        if (arp_resolve(ip, mac) == 0) return 0;
        /* Poll-driven: do NOT rely on the background netd thread to drain the
         * NIC and feed our arp.c cache.  We must consume any inbound ARP reply
         * ourselves, otherwise (cold cache, e.g. in VirtualBox) resolution
         * never completes even though the peer answered. */
        {
            uint8_t b[2048];
            int l;
            while ((l = nic_recv(b, sizeof(b))) > 0) {
                if (l < (int)(sizeof(eth_frame_t) + sizeof(arp_packet_t))) continue;
                eth_frame_t *ethf = (eth_frame_t *)b;
                if (__builtin_bswap16(ethf->type) != ETH_P_ARP) continue;
                arp_packet_t *ar = (arp_packet_t *)(b + sizeof(eth_frame_t));
                if (ar->spa) {
                    static const uint8_t zero_mac[ETH_ALEN] = {0,0,0,0,0,0};
                    if (memcmp(ar->sha, zero_mac, ETH_ALEN) != 0)
                        arp_update(ar->spa, ar->sha);
                }
            }
        }
        sched_sleep_ms(5);
    }
    return -1;
}

static int send_raw(uint32_t src, uint32_t dst, uint8_t proto, const void *data, uint16_t len) {
    if (len > 2048 - (int)sizeof(ip_packet_t)) return -1;
    uint8_t frame[2048];
    ip_packet_t *ip = (ip_packet_t *)frame;
    memset(ip, 0, sizeof(ip_packet_t));
    ip->ver_ihl = 0x45;
    ip->total_len = __builtin_bswap16(sizeof(ip_packet_t) + len);
    ip->id = __builtin_bswap16(id_counter++);
    ip->flags_frag = __builtin_bswap16(IP_DF);
    ip->ttl = IP_TTL_DEFAULT;
    ip->protocol = proto;
    ip->src = src;
    ip->dst = dst;
    ip->checksum = 0;
    ip->checksum = ip_checksum(ip, sizeof(ip_packet_t));
    memcpy(frame + sizeof(ip_packet_t), data, len);
    uint8_t mac[6];
    if (dst == 0xFFFFFFFF) {
        memset(mac, 0xFF, 6);
    } else if ((dst & my_netmask) != (src & my_netmask) && my_gateway) {
        if (arp_wait(mac, my_gateway) < 0) { spin_lock(&ip_lock); stats.tx_errors++; spin_unlock(&ip_lock); return -1; }
    } else {
        if (arp_wait(mac, dst) < 0) { spin_lock(&ip_lock); stats.tx_errors++; spin_unlock(&ip_lock); return -1; }
    }
    spin_lock(&ip_lock);
    stats.tx_packets++;
    stats.tx_bytes += sizeof(ip_packet_t) + len;
    spin_unlock(&ip_lock);
    return eth_send(mac, ETH_P_IP, frame, sizeof(ip_packet_t) + len);
}

int ip_send(uint32_t dst, uint8_t proto, const void *data, uint16_t len) {
    spin_lock(&ip_lock);
    uint32_t src = my_ip_addr;
    spin_unlock(&ip_lock);
    if (!src) return -1;
    return send_raw(src, dst, proto, data, len);
}

int ip_send_to(uint32_t src, uint32_t dst, uint8_t proto, const void *data, uint16_t len) {
    if (!src || !data) return -1;
    spin_lock(&ip_lock);
    uint32_t current = my_ip_addr;
    spin_unlock(&ip_lock);
    if (!current) return -1;
    return send_raw(src, dst, proto, data, len);
}

int ip_send_fragmented(uint32_t dst, uint8_t proto, const void *data, uint16_t len) {
    int frag_size = (ETH_MTU - sizeof(ip_packet_t)) & ~7;
    if (len <= (uint16_t)frag_size) return ip_send(dst, proto, data, len);
    const uint8_t *p = (const uint8_t *)data;
    int offset = 0;
    while (offset < len) {
        int chunk = len - offset;
        if (chunk > frag_size) chunk = frag_size;
        uint16_t flags = (offset + chunk < len) ? IP_MF : 0;
        flags |= (uint16_t)(offset >> 3);
        uint8_t frame[2048];
        ip_packet_t *ip = (ip_packet_t *)frame;
        memset(ip, 0, sizeof(ip_packet_t));
        ip->ver_ihl = 0x45;
        ip->total_len = __builtin_bswap16(sizeof(ip_packet_t) + chunk);
        ip->id = __builtin_bswap16(id_counter);
        ip->flags_frag = __builtin_bswap16(flags);
        ip->ttl = IP_TTL_DEFAULT;
        ip->protocol = proto;
        ip->src = my_ip_addr;
        ip->dst = dst;
        ip->checksum = 0;
        ip->checksum = ip_checksum(ip, sizeof(ip_packet_t));
        memcpy(frame + sizeof(ip_packet_t), p + offset, chunk);
        uint8_t mac[6];
        if ((dst & my_netmask) != (my_ip_addr & my_netmask) && my_gateway) {
            if (arp_resolve(my_gateway, mac) < 0) return -1;
        } else if (arp_resolve(dst, mac) < 0) return -1;
        int r = eth_send(mac, ETH_P_IP, frame, sizeof(ip_packet_t) + chunk);
        if (r < 0) return -1;
        spin_lock(&ip_lock);
        stats.fragmented++;
        spin_unlock(&ip_lock);
        offset += chunk;
    }
    return len;
}

void ip_recv(const uint8_t *pkt, int len, uint32_t src_mac_ip) {
    (void)src_mac_ip;
    if (len < (int)sizeof(ip_packet_t)) { spin_lock(&ip_lock); stats.bad_header++; spin_unlock(&ip_lock); return; }
    const ip_packet_t *ip = (const ip_packet_t *)pkt;
    int ihl = (ip->ver_ihl & 0x0F) * 4;
    if (ihl < 20 || ihl > len) { spin_lock(&ip_lock); stats.bad_header++; spin_unlock(&ip_lock); return; }
    int total_len = __builtin_bswap16(ip->total_len);
    if (total_len < ihl || total_len > len) { spin_lock(&ip_lock); stats.bad_header++; spin_unlock(&ip_lock); return; }
    if (ip_checksum(ip, ihl) != 0) { spin_lock(&ip_lock); stats.bad_checksum++; stats.rx_errors++; spin_unlock(&ip_lock); return; }
    spin_lock(&ip_lock);
    stats.rx_packets++;
    stats.rx_bytes += total_len;
    if (ip->dst != my_ip_addr && ip->dst != 0xFFFFFFFF) {
        if (my_gateway && my_ip_addr) { stats.forwarded++; spin_unlock(&ip_lock); }
        else { stats.rx_dropped++; spin_unlock(&ip_lock); }
        return;
    }
    stats.delivered++;
    spin_unlock(&ip_lock);
    const uint8_t *payload = pkt + ihl;
    int plen = total_len - ihl;
    if (ip->protocol == IP_PROTO_ICMP) {
        icmp_recv(payload, plen, ip->src);
    } else if (ip->protocol == IP_PROTO_UDP) {
        udp_recv_packet(ip->src, ip->dst, payload, plen);
    } else if (ip->protocol == IP_PROTO_TCP) {
        tcp_process_packet(ip->src, payload, plen);
    } else {
        spin_lock(&ip_lock); stats.unknown_proto++; spin_unlock(&ip_lock);
    }
}

int ip_route_add(uint32_t dst, uint32_t gw, uint32_t mask, int metric, int iface) {
    spin_lock(&ip_lock);
    for (int i = 0; i < RT_TABLE_SIZE; i++) {
        if (!routes[i].dst && !routes[i].gateway) {
            routes[i].dst = dst; routes[i].gateway = gw; routes[i].netmask = mask;
            routes[i].metric = metric; routes[i].interface = iface;
            routes[i].type = mask ? RT_LOCAL : RT_DEFAULT;
            route_count++;
            spin_unlock(&ip_lock);
            return 0;
        }
    }
    spin_unlock(&ip_lock);
    return -1;
}

int ip_route_del(uint32_t dst, uint32_t mask) {
    spin_lock(&ip_lock);
    for (int i = 0; i < RT_TABLE_SIZE; i++) {
        if (routes[i].dst == dst && routes[i].netmask == mask) {
            memset(&routes[i], 0, sizeof(ip_route_t));
            route_count--;
            spin_unlock(&ip_lock);
            return 0;
        }
    }
    spin_unlock(&ip_lock);
    return -1;
}

int ip_route_lookup(uint32_t dst, ip_route_t *route) {
    if (!route) return -1;
    spin_lock(&ip_lock);
    int best = -1;
    int best_metric = 9999;
    for (int i = 0; i < RT_TABLE_SIZE; i++) {
        if (!routes[i].dst && !routes[i].gateway) continue;
        if ((dst & routes[i].netmask) == (routes[i].dst & routes[i].netmask)) {
            if (routes[i].metric < best_metric) {
                best_metric = routes[i].metric;
                best = i;
            }
        }
    }
    if (best >= 0) { *route = routes[best]; spin_unlock(&ip_lock); return 0; }
    spin_unlock(&ip_lock);
    return -1;
}

void ip_route_default(uint32_t gw, int iface) {
    spin_lock(&ip_lock);
    for (int i = 0; i < RT_TABLE_SIZE; i++) {
        if (routes[i].type == RT_DEFAULT && routes[i].gateway) { memset(&routes[i], 0, sizeof(ip_route_t)); }
    }
    routes[0].dst = 0; routes[0].gateway = gw; routes[0].netmask = 0;
    routes[0].type = RT_DEFAULT; routes[0].interface = iface;
    spin_unlock(&ip_lock);
}

int ip_route_table(ip_route_t *table, int max) {
    if (!table) return 0;
    spin_lock(&ip_lock);
    int n = 0;
    for (int i = 0; i < RT_TABLE_SIZE && n < max; i++)
        if (routes[i].dst || routes[i].gateway) table[n++] = routes[i];
    spin_unlock(&ip_lock);
    return n;
}

void ip_set_addr(uint32_t ip) { my_ip_addr = ip; }
void ip_set_netmask(uint32_t mask) { my_netmask = mask; }
void ip_set_gateway(uint32_t gw) { my_gateway = gw; ip_route_default(gw, 0); }
uint32_t ip_get_addr(void) { return my_ip_addr; }
uint32_t ip_get_netmask(void) { return my_netmask; }
uint32_t ip_get_gateway(void) { return my_gateway; }
int ip_is_local(uint32_t ip) { return (ip & my_netmask) == (my_ip_addr & my_netmask); }
int ip_is_broadcast(uint32_t ip) { return ip == 0xFFFFFFFF || ip == (my_ip_addr | ~my_netmask); }

void ip_get_stats(ip_stats_t *s) { if (s) { spin_lock(&ip_lock); *s = stats; spin_unlock(&ip_lock); } }
void ip_reset_stats(void) { spin_lock(&ip_lock); memset(&stats, 0, sizeof(stats)); spin_unlock(&ip_lock); }

void ip_dump_routes(void) {
    kprintf("IP Routing Table:\n");
    ip_route_t t[RT_TABLE_SIZE];
    int n = ip_route_table(t, RT_TABLE_SIZE);
    for (int i = 0; i < n; i++) {
        kprintf("  %d.%d.%d.%d/%d via %d.%d.%d.%d metric=%d iface=%d\n",
                t[i].dst & 0xFF, (t[i].dst >> 8) & 0xFF,
                (t[i].dst >> 16) & 0xFF, (t[i].dst >> 24) & 0xFF,
                t[i].netmask,
                t[i].gateway & 0xFF, (t[i].gateway >> 8) & 0xFF,
                (t[i].gateway >> 16) & 0xFF, (t[i].gateway >> 24) & 0xFF,
                t[i].metric, t[i].interface);
    }
}

void ip_periodic(void) { arp_purge_expired(); }

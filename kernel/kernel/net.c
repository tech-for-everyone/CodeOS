#include "net.h"
#include "ip.h"
#include "arp.h"
extern void arp_purge_expired(void);
#include "dns.h"
#include "udp.h"
#include "icmp.h"
#include "kprintf.h"
#include "string.h"
#include "security.h"
#include "ad_block.h"
#include "nic.h"
#include "timer.h"
#include "vmm.h"
#include "sched.h"
#include "ws.h"
#include "tcp.h"
#include "https_certs.h"

#if HTTPS_USE_OPENSSL
/* Genuine OpenSSL TLS backend (pkgs/core/openssl). */
int ossl_https_get(int fd, const char *host, const char *path,
                   void *buf, uint16_t max_len);
#endif

static uint8_t  our_mac[6];
static uint32_t our_ip;
static uint32_t gateway_ip;
static uint32_t dns_ip;
static uint8_t  gateway_mac[6];
static int      gateway_mac_valid;
static int      net_up;

/* Optional static IP override supplied via `ip=` on the boot command line
 * (host-order "logical" value; used for multi-instance VM testing).
 * 0 = unset → default 10.0.2.15 fallback. */
int net_override_ip;  /* non-static: set by main.c cmdline parser */

static struct {
    uint32_t ip;
    uint8_t  mac[6];
    int      valid;
    uint64_t last_access;
} arp_cache[ARP_CACHE_SIZE]; /* ARP_CACHE_SIZE from arp.h (64) */

/* ── DNS resolver state ── */
static uint16_t dns_next_id;                    /* auto-incrementing query ID */
static dns_server_t dns_servers[DNS_MAX_SERVERS]; /* configured servers */
static int dns_server_count;
static dns_cache_entry_t dns_cache[DNS_CACHE_SIZE];

static int      tcp_state;
static uint32_t tcp_dst_ip;
static uint16_t tcp_src_port, tcp_dst_port;
static uint32_t tcp_seq, tcp_ack;
static int      tcp_rx_len;
static uint16_t ip_id;

/* Receive window (scaled by TCP_RWND_SHIFT on the wire). The e1000 RX ring
 * buffers at most E1000_NUM_RX × 2048 = 128 KB, so advertising a 128 KB
 * effective window (65535 × 2, scale 1) is the true line-rate ceiling:
 * 128 KB / RTT ≈ 1 Gbps on a local link, with zero overflow risk. */
#define TCP_RWND         65535
#define TCP_RWND_SHIFT   1

static uint64_t dhcp_lease_expires;   /* ms timestamp of lease expiry (0 = no lease) */
static uint32_t dhcp_lease_seconds;

enum { TCP_FIN_SENT = 11 }; /* legacy single-socket marker; other states via tcp.h */

static inline uint16_t bswap16(uint16_t x) { return __builtin_bswap16(x); }
static inline uint32_t bswap32(uint32_t x) { return __builtin_bswap32(x); }

static uint16_t ones_sum(const void *data, int len) {
    uint32_t sum = 0;
    const uint8_t *b = (const uint8_t *)data;
    for (int i = 0; i + 1 < len; i += 2)
        sum += (uint32_t)(b[i] | (b[i + 1] << 8));
    if (len & 1)
        sum += b[len - 1];
    while (sum >> 16)
        sum = (sum & 0xFFFF) + (sum >> 16);
    return ~sum & 0xFFFF;
}

static void fix_page_tables(void) {
    uint64_t *pml4 = vmm_current_pml4();
    if (pml4) {
        asm volatile("mov %0, %%cr3" : : "r"(pml4) : "memory");
    }
}

static int eth_send(const uint8_t *dst_mac, uint16_t type, const void *payload, uint16_t plen) {
    uint8_t buf[2048];
    struct eth_hdr *eth = (struct eth_hdr *)buf;
    memcpy(eth->dst_mac, dst_mac, 6);
    memcpy(eth->src_mac, our_mac, 6);
    eth->type = bswap16(type);
    memcpy(buf + sizeof(struct eth_hdr), payload, plen);
    int total = sizeof(struct eth_hdr) + plen;
    if (total < 60) { memset(buf + total, 0, 60 - total); total = 60; }
    return nic_send(buf, total);
}

static int send_arp_request(uint32_t target_ip) {
    struct arp_hdr arp;
    memset(&arp, 0, sizeof(arp));
    arp.htype = bswap16(1);
    arp.ptype = bswap16(0x0800);
    arp.hlen = 6;
    arp.plen = 4;
    arp.oper = bswap16(1);
    memcpy(arp.sha, our_mac, 6);
    arp.spa = our_ip;
    memset(arp.tha, 0, 6);
    arp.tpa = target_ip;
    uint8_t bcast[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    return eth_send(bcast, 0x0806, &arp, sizeof(arp));
}

static void arp_cache_add(uint32_t ip, const uint8_t *mac) {
    int oldest = 0;
    uint64_t oldest_time = (uint64_t)-1;
    for (int i = 0; i < ARP_CACHE_SIZE; i++) {
        if (!arp_cache[i].valid) { oldest = i; break; }
        if (arp_cache[i].ip == ip) { oldest = i; break; }
        if (arp_cache[i].last_access < oldest_time) {
            oldest_time = arp_cache[i].last_access;
            oldest = i;
        }
    }
    arp_cache[oldest].ip = ip;
    memcpy(arp_cache[oldest].mac, mac, 6);
    arp_cache[oldest].valid = 1;
    arp_cache[oldest].last_access = timer_get_milliseconds();
}

static int arp_cache_lookup(uint32_t ip, uint8_t *mac) {
    for (int i = 0; i < ARP_CACHE_SIZE; i++) {
        if (arp_cache[i].valid && arp_cache[i].ip == ip) {
            memcpy(mac, arp_cache[i].mac, 6);
            arp_cache[i].last_access = timer_get_milliseconds();
            return 0;
        }
    }
    return -1;
}

static int legacy_arp_resolve(uint32_t ip, uint8_t *mac) {
    if (arp_cache_lookup(ip, mac) == 0) return 0;
    if (gateway_mac_valid && ip == gateway_ip) {
        memcpy(mac, gateway_mac, 6);
        return 0;
    }
    send_arp_request(ip);
    for (int tries = 0; tries < 50; tries++) {
        timer_sleep_ms(2);
        uint8_t buf[2048];
        int len = nic_recv(buf, sizeof(buf));
        if (len < (int)sizeof(struct eth_hdr)) continue;
        struct eth_hdr *eth = (struct eth_hdr *)buf;
        if (bswap16(eth->type) == 0x0806 && len >= (int)(sizeof(struct eth_hdr) + sizeof(struct arp_hdr))) {
            struct arp_hdr *arp = (struct arp_hdr *)(buf + sizeof(struct eth_hdr));
            if (bswap16(arp->oper) == 2 && arp->spa == ip) {
                memcpy(mac, arp->sha, 6);
                arp_cache_add(ip, mac);
                if (ip == gateway_ip) {
                    memcpy(gateway_mac, mac, 6);
                    gateway_mac_valid = 1;
                }
                return 0;
            }
        }
    }
    return -1;
}

static inline int ip_on_same_subnet(uint32_t a, uint32_t b) {
    uint32_t mask = bswap32(0xFFFFFF00);
    return (a & mask) == (b & mask);
}

static int send_ip_packet(uint32_t dst_ip, uint8_t proto, const void *data, uint16_t dlen) {
    if (!sec_firewall_check(0, dst_ip, proto, 0, 0)) return -1;
    if ((uint32_t)sizeof(struct ip_hdr) + dlen > 2048) return -1;

    struct ip_hdr ip;
    memset(&ip, 0, sizeof(ip));
    ip.ver_ihl = 0x45;
    ip.total_len = bswap16(sizeof(ip) + dlen);
    ip.id = bswap16(ip_id++);
    ip.flags_frag = bswap16(0x4000);
    ip.ttl = 64;
    ip.protocol = proto;
    ip.src_ip = our_ip;
    ip.dst_ip = dst_ip;
    ip.checksum = 0;
    ip.checksum = ones_sum(&ip, sizeof(ip));

    uint8_t mac[6];
    if (dst_ip == 0xFFFFFFFF) {
        memset(mac, 0xFF, 6);
    } else if (!ip_on_same_subnet(dst_ip, our_ip) && gateway_ip) {
        if (legacy_arp_resolve(gateway_ip, mac) < 0) return -1;
    } else if (legacy_arp_resolve(dst_ip, mac) < 0) {
        return -1;
    }

    uint8_t buf[2048];
    memcpy(buf, &ip, sizeof(ip));
    memcpy(buf + sizeof(ip), data, dlen);
    return eth_send(mac, 0x0800, buf, sizeof(ip) + dlen);
}

static uint16_t tcp_csum(uint32_t src_ip, uint32_t dst_ip, const void *seg, uint16_t seg_len) {
    /* IPv4 pseudo-header: src(4) dst(4) zero(1) proto(1) length(2), in network
     * byte order.  src_ip/dst_ip are host-order integers; emitting their bytes
     * least-significant-first places them in network (wire) order.
     *
     * Summation is done word-by-word in little-endian host order, padding a
     * trailing odd byte in the low position (consistent with ones_sum() and
     * RFC 1071) so the result's native uint16_t store lands in network order.
     *
     * Two bugs are fixed here (both surfaced as silently-dropped TCP data):
     *  - The previous code wrote the pseudo buffer through uint32_t/uint16_t
     *    aliases and read it back via uint16_t*; under -O2 strict-aliasing
     *    that is miscompiled, corrupting every checksum.  Byte access removes
     *    the UB.
     *  - The old odd-byte handling used "<<8" (high byte), the big-endian pad
     *    convention, which is inconsistent with the little-endian word reads
     *    and corrupted checksums for odd-length segments. */
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
    pseudo[9] = 6;
    pseudo[10] = (uint8_t)(seg_len >> 8);
    pseudo[11] = (uint8_t)seg_len;

    const uint8_t *s = (const uint8_t *)seg;
    uint32_t sum = 0;
    for (int i = 0; i < (int)sizeof(pseudo) - 1; i += 2)
        sum += (uint32_t)(pseudo[i] | (pseudo[i + 1] << 8));
    for (uint16_t i = 0; i + 1 < seg_len; i += 2)
        sum += (uint32_t)(s[i] | (s[i + 1] << 8));
    if (seg_len & 1)
        sum += s[seg_len - 1];
    while (sum >> 16)
        sum = (sum & 0xFFFF) + (sum >> 16);
    return (uint16_t)(~sum & 0xFFFF);
}

static int send_tcp_seg(uint8_t flags, const void *data, uint16_t dlen, int with_mss) {
    uint8_t seg[2048];
    struct tcp_hdr *tcp = (struct tcp_hdr *)seg;
    memset(tcp, 0, sizeof(*tcp));
    tcp->src_port = bswap16(tcp_src_port);
    tcp->dst_port = bswap16(tcp_dst_port);
    tcp->seq = bswap32(tcp_seq);
    tcp->ack = bswap32(tcp_ack);
    tcp->flags = flags;
    tcp->window = bswap16(TCP_RWND);

    int hdr_len = sizeof(struct tcp_hdr);
    tcp->offset = 0x50;
    if (with_mss) {
        /* Modern TCP options: MSS (kind 2), SACK-permitted (kind 4),
         * window scale (kind 3, shift 4 → up to 1 MB in flight).
         * 4+2+3 = 9 bytes, padded with NOPs to a 4-byte boundary so the
         * header is exactly 32 bytes (data offset 0x80) and consistent with
         * the IP total length. */
        uint8_t opts[] = {
            2, 4, 0x05, 0xB4,          /* MSS 1460 */
            4, 2,                       /* SACK-permitted */
            3, 3, TCP_RWND_SHIFT,       /* window scale shift */
            1, 1, 1,                    /* NOP padding */
        };
        memcpy(seg + hdr_len, opts, 12);
        hdr_len += 12;                 /* 20 + 12 = 32 (8 words) */
        tcp->offset = 0x80;
    }

    int total = hdr_len + dlen;
    if (total > 2048) return -1;
    if (data && dlen > 0) memcpy(seg + hdr_len, data, dlen);
    tcp->checksum = 0;
    tcp->checksum = tcp_csum(our_ip, tcp_dst_ip, seg, total);

    if (send_ip_packet(tcp_dst_ip, 6, seg, total) < 0) return -1;

    if (flags & 2) tcp_seq++;
    if (dlen > 0 || (flags & 1)) tcp_seq += dlen + ((flags & 1) ? 1 : 0);
    return 0;
}

static int legacy_tcp_connect(uint32_t dst_ip, uint16_t dst_port) {
    if (!net_up) return -1;
    tcp_state = TCP_CLOSED;
    tcp_dst_ip = dst_ip;
    tcp_src_port = 49152 + (ip_id % 1284);
    tcp_dst_port = dst_port;
    tcp_seq = 1000;
    tcp_ack = 0;
    tcp_rx_len = 0;

    if (send_tcp_seg(2, NULL, 0, 1) < 0) return -1;
    tcp_state = TCP_SYN_SENT;
    uint32_t syn_seq = tcp_seq;

    for (int tries = 0; tries < 100; tries++) {
        /* Retransmit SYN every ~10 tries so a dropped segment doesn't
         * stall the handshake. send_tcp_seg bumps tcp_seq, so restore
         * the original SYN sequence number first. */
        if (tries > 0 && (tries % 10) == 0) {
            tcp_seq = syn_seq;
            send_tcp_seg(2, NULL, 0, 1);
        }
        /* Yield CPU between polls so the input/mouse thread (and other
         * processes) isn't starved while a connection is being established.
         * nic_recv() below drains the RX ring on each wake. */
        sched_sleep_ms(1);
        {
            uint8_t b[2048];
            int l;
            while ((l = nic_recv(b, sizeof(b))) > 0) {
                if (l < (int)sizeof(struct eth_hdr)) continue;
                struct eth_hdr *eth = (struct eth_hdr *)b;
                if (bswap16(eth->type) != 0x0800) continue;
                struct ip_hdr *ip = (struct ip_hdr *)(b + sizeof(struct eth_hdr));
                if (ip->protocol != 6) continue;
                int ip_hdr_len = (ip->ver_ihl & 0x0F) * 4;
                struct tcp_hdr *tcp = (struct tcp_hdr *)((uint8_t *)ip + ip_hdr_len);
                uint16_t dport = bswap16(tcp->dst_port);
                uint16_t sport = bswap16(tcp->src_port);
                if (dport != tcp_src_port || sport != tcp_dst_port) continue;
                if (ip->src_ip != tcp_dst_ip) continue;
                uint32_t ack = bswap32(tcp->ack);
                if ((tcp->flags & 0x12) == 0x12) {
                    tcp_ack = bswap32(tcp->seq) + 1;
                    tcp_seq = ack;
                    send_tcp_seg(0x10, NULL, 0, 0);
                    tcp_state = TCP_ESTABLISHED;
                    return 0;
                }
            }
        }
    }
    tcp_state = TCP_CLOSED;
    return -1;
}

static int legacy_tcp_send_data(const void *data, uint16_t len) {
    if (tcp_state != TCP_ESTABLISHED) return -1;
    return send_tcp_seg(0x18, data, len, 0) < 0 ? -1 : len;
}

static void legacy_tcp_close_conn(void) {
    if (tcp_state == TCP_ESTABLISHED) {
        send_tcp_seg(0x11, NULL, 0, 0);
        tcp_state = TCP_FIN_SENT;
        for (int tries = 0; tries < 20; tries++) {
            timer_sleep_ms(2);
            uint8_t b[2048]; int l;
            while ((l = nic_recv(b, sizeof(b))) > 0) {
                if (l < (int)sizeof(struct eth_hdr)) continue;
                struct eth_hdr *eth = (struct eth_hdr *)b;
                if (bswap16(eth->type) != 0x0800) continue;
                struct ip_hdr *ip = (struct ip_hdr *)(b + sizeof(struct eth_hdr));
                if (ip->protocol != 6) continue;
                int ip_hdr_len = (ip->ver_ihl & 0x0F) * 4;
                struct tcp_hdr *tcp = (struct tcp_hdr *)((uint8_t *)ip + ip_hdr_len);
                uint16_t dport = bswap16(tcp->dst_port);
                uint16_t sport = bswap16(tcp->src_port);
                if (dport != tcp_src_port || sport != tcp_dst_port) continue;
                if (ip->src_ip != tcp_dst_ip) continue;
                if (tcp->flags & 0x10) {
                    uint32_t seq = bswap32(tcp->seq);
                    tcp_ack = seq + 1;
                    send_tcp_seg(0x10, NULL, 0, 0);
                    tcp_state = TCP_CLOSED;
                    return;
                }
            }
        }
    }
    tcp_state = TCP_CLOSED;
}

static void handle_arp(uint8_t *buf, int len) {
    (void)len;
    struct eth_hdr *eth = (struct eth_hdr *)buf;
    struct arp_hdr *arp = (struct arp_hdr *)(buf + sizeof(struct eth_hdr));
    /* Learn any peer seen on the wire into the shared arp.c cache: the
     * structured tcp.c/ip.c stack resolves neighbours through it, while the
     * legacy stack below keeps its own private table. */
    if (arp->spa) {
        static const uint8_t zero_mac[6] = {0,0,0,0,0,0};
        if (memcmp(arp->sha, zero_mac, 6) != 0)
            arp_update(arp->spa, arp->sha);
    }
    if (bswap16(arp->oper) == 1 && arp->tpa == our_ip) {
        struct arp_hdr reply;
        memset(&reply, 0, sizeof(reply));
        reply.htype = bswap16(1);
        reply.ptype = bswap16(0x0800);
        reply.hlen = 6;
        reply.plen = 4;
        reply.oper = bswap16(2);
        memcpy(reply.sha, our_mac, 6);
        reply.spa = our_ip;
        memcpy(reply.tha, arp->sha, 6);
        reply.tpa = arp->spa;
        eth_send(eth->src_mac, 0x0806, &reply, sizeof(reply));
    }
}

void net_poll(void) {
    static uint64_t last_renew_check;
    uint8_t buf[2048];
    int len;
    while ((len = nic_recv(buf, sizeof(buf))) > 0) {
        if (len < (int)sizeof(struct eth_hdr)) continue;
        struct eth_hdr *eth = (struct eth_hdr *)buf;
        uint16_t type = bswap16(eth->type);
        if (type == 0x0806) handle_arp(buf, len);
        else if (type == 0x0800) {
            if (len < (int)(sizeof(struct eth_hdr) + sizeof(struct ip_hdr))) continue;
            struct ip_hdr *ip = (struct ip_hdr *)(buf + sizeof(struct eth_hdr));
            if (!sec_firewall_check(ip->src_ip, ip->dst_ip, ip->protocol, 0, 0))
                continue;
            /* Hand into the structured receive stack: ip_recv validates and
               dispatches ICMP (echo reply incl.), UDP and TCP segments.
               This is what makes tcp_process_packet()/udp_recv_packet()
               reachable — without it TCP/UDP sockets never receive. */
            ip_recv((const uint8_t *)(buf + sizeof(struct eth_hdr)),
                    len - (int)sizeof(struct eth_hdr), 0);
        }
    }

    /* DHCP lease renewal: at 50% of lease, request a fresh lease so the
     * IP doesn't expire mid-session. Check at most once per second. */
    arp_purge_expired();

    if (dhcp_lease_expires && net_up) {
        uint64_t now = timer_get_milliseconds();
        if (now - last_renew_check >= 1000) {
            last_renew_check = now;
            if (now >= dhcp_lease_expires) {
                kprintf("Net: DHCP lease expiring, renewing...\n");
                dhcp_lease_expires = 0;
                if (dhcp_configure() < 0) {
                    kprintf("Net: DHCP renew failed, keeping current IP\n");
                }
            }
        }
    }
}

/* ── Utility: IP to dotted-decimal string ── */
static void ip_to_str(uint32_t ip, char *buf) {
    buf[0] = 0;
    int p = 0;
    /* IPs are stored in network byte order (memory bytes = wire bytes) */
    for (int i = 0; i < 4; i++) {
        int octet = (ip >> (i * 8)) & 0xFF;
        if (i > 0) buf[p++] = '.';
        if (octet >= 100) buf[p++] = '0' + octet / 100;
        if (octet >= 10) buf[p++] = '0' + (octet / 10) % 10;
        buf[p++] = '0' + octet % 10;
    }
    buf[p] = 0;
}

/* ── DNS name encoding: "www.example.com" → wire format ── */
static int dns_encode_name(const char *hostname, uint8_t *out, int max_out) {
    int pos = 0;
    while (*hostname && pos < max_out - 1) {
        const char *dot = hostname;
        int len = 0;
        while (*dot && *dot != '.') { dot++; len++; }
        if (len > DNS_MAX_LABEL) return -1;
        out[pos++] = (uint8_t)len;
        memcpy(out + pos, hostname, len);
        pos += len;
        hostname = *dot ? dot + 1 : dot;
    }
    if (pos >= max_out) return -1;
    out[pos++] = 0; /* root label */
    return pos;
}

/* ── DNS name decoding with compression pointer support ── */
static int dns_decode_name(const uint8_t *resp, int resp_len, const uint8_t *src,
                           char *out, int max_out) {
    int out_pos = 0;
    int src_pos = (int)(src - resp);
    int jumped = 0;
    int iterations = 0;

    while (src_pos < resp_len && out_pos < max_out - 1) {
        uint8_t c = resp[src_pos];
        if (c == 0) {
            if (!jumped) src_pos++;
            break;
        }
        if ((c & 0xC0) == 0xC0) {
            /* Compression pointer */
            if (src_pos + 1 >= resp_len) return -1;
            if (!jumped) src_pos += 2;
            src_pos = ((c & 0x3F) << 8) | resp[src_pos + 1];
            jumped = 1;
            continue;
        }
        if (++iterations > 128) return -1; /* prevent infinite loops */
        src_pos++;
        if (src_pos + c > resp_len) return -1;
        if (out_pos + 1 + (int)c > max_out) {
            /* label (+ separator) would not fit: truncate safely */
            out[out_pos] = 0;
            return jumped ? src_pos : src_pos;
        }
        if (out_pos > 0) out[out_pos++] = '.';
        memcpy(out + out_pos, resp + src_pos, c);
        out_pos += c;
        src_pos += c;
    }
    out[out_pos] = 0;
    return jumped ? src_pos : src_pos;
}

/* ── DNS cache operations ── */
static void dns_cache_init(void) {
    memset(dns_cache, 0, sizeof(dns_cache));
    dns_next_id = 0;
}

static int __attribute__((unused)) dns_cache_lookup(const char *name, uint32_t *ip) {
    uint64_t now = timer_get_milliseconds();
    for (int i = 0; i < DNS_CACHE_SIZE; i++) {
        if (!dns_cache[i].valid) continue;
        if (dns_cache[i].expires <= now) { dns_cache[i].valid = 0; continue; }
        if (strcmp(dns_cache[i].name, name) == 0) {
            dns_cache[i].last_access = now;
            if (dns_cache[i].ip == 0) return -1;
            *ip = dns_cache[i].ip;
            return 0;
        }
    }
    return -1;
}

static int __attribute__((unused)) dns_cache_lookup_ipv6(const char *name, uint8_t *ipv6) {
    uint64_t now = timer_get_milliseconds();
    for (int i = 0; i < DNS_CACHE_SIZE; i++) {
        if (!dns_cache[i].valid) continue;
        if (dns_cache[i].expires <= now) { dns_cache[i].valid = 0; continue; }
        if (strcmp(dns_cache[i].name, name) == 0) {
            dns_cache[i].last_access = now;
            if (!dns_cache[i].has_ipv6) return -1;
            memcpy(ipv6, dns_cache[i].ipv6, 16);
            return 0;
        }
    }
    return -1;
}

static void __attribute__((unused)) dns_cache_store(const char *name, uint32_t ip, int rcode) {
    /* Find existing entry or LRU victim (oldest last_access) */
    int oldest = -1;
    uint64_t oldest_access = 0xFFFFFFFFFFFFFFFFULL;
    for (int i = 0; i < DNS_CACHE_SIZE; i++) {
        if (!dns_cache[i].valid) { oldest = i; break; }
        if (strcmp(dns_cache[i].name, name) == 0) { oldest = i; break; }
        if (dns_cache[i].last_access < oldest_access) {
            oldest_access = dns_cache[i].last_access;
            oldest = i;
        }
    }
    if (oldest < 0) oldest = 0;

    uint64_t now = timer_get_milliseconds();
    strlcpy(dns_cache[oldest].name, name, sizeof(dns_cache[oldest].name));
    dns_cache[oldest].ip = ip;
    dns_cache[oldest].rcode = (int8_t)rcode;
    dns_cache[oldest].query_id = 0;
    dns_cache[oldest].last_access = now;
    uint64_t ttl = (ip != 0) ? DNS_DEFAULT_TTL : DNS_NEG_TTL;
    dns_cache[oldest].expires = now + ttl;
    dns_cache[oldest].valid = 1;
}

static void __attribute__((unused)) dns_cache_store_ipv6(const char *name, const uint8_t *ipv6) {
    /* Find existing entry or LRU victim */
    int oldest = -1;
    uint64_t oldest_access = 0xFFFFFFFFFFFFFFFFULL;
    for (int i = 0; i < DNS_CACHE_SIZE; i++) {
        if (!dns_cache[i].valid) { oldest = i; break; }
        if (strcmp(dns_cache[i].name, name) == 0) { oldest = i; break; }
        if (dns_cache[i].last_access < oldest_access) {
            oldest_access = dns_cache[i].last_access;
            oldest = i;
        }
    }
    if (oldest < 0) oldest = 0;

    uint64_t now = timer_get_milliseconds();
    strlcpy(dns_cache[oldest].name, name, sizeof(dns_cache[oldest].name));
    memcpy(dns_cache[oldest].ipv6, ipv6, 16);
    dns_cache[oldest].has_ipv6 = 1;
    dns_cache[oldest].last_access = now;
    dns_cache[oldest].expires = now + DNS_DEFAULT_TTL;
    dns_cache[oldest].valid = 1;
}

void dns_cache_flush(void) {
    for (int i = 0; i < DNS_CACHE_SIZE; i++) dns_cache[i].valid = 0;
}

void dns_remove(const char *hostname) {
    if (!hostname) return;
    for (int i = 0; i < DNS_CACHE_SIZE; i++) {
        if (dns_cache[i].valid && strcmp(dns_cache[i].name, hostname) == 0) {
            dns_cache[i].valid = 0;
        }
    }
}

int dns_cache_count(void) {
    int n = 0;
    for (int i = 0; i < DNS_CACHE_SIZE; i++)
        if (dns_cache[i].valid) n++;
    return n;
}

int dns_cache_size(void) { return DNS_CACHE_SIZE; }

void dns_cache_dump(void) {
    uint64_t now = timer_get_milliseconds();
    kprintf("DNS Cache (%d/%d entries):\n", dns_cache_count(), DNS_CACHE_SIZE);
    for (int i = 0; i < DNS_CACHE_SIZE; i++) {
        if (!dns_cache[i].valid) continue;
        dns_cache_entry_t *e = &dns_cache[i];
        if (e->ip) {
            kprintf("  [%d] %s -> %d.%d.%d.%d (expires in %llu ms)\n", i,
                    e->name,
                    (e->ip >> 24) & 0xFF, (e->ip >> 16) & 0xFF,
                    (e->ip >> 8) & 0xFF, e->ip & 0xFF,
                    (e->expires > now) ? (e->expires - now) : 0);
        } else if (e->has_ipv6) {
            kprintf("  [%d] %s -> [%02x%02x:%02x%02x:%02x%02x:%02x%02x:%02x%02x:%02x%02x:%02x%02x:%02x%02x] (expires in %llu ms)\n", i,
                    e->name,
                    e->ipv6[0], e->ipv6[1], e->ipv6[2], e->ipv6[3],
                    e->ipv6[4], e->ipv6[5], e->ipv6[6], e->ipv6[7],
                    e->ipv6[8], e->ipv6[9], e->ipv6[10], e->ipv6[11],
                    e->ipv6[12], e->ipv6[13], e->ipv6[14], e->ipv6[15],
                    (e->expires > now) ? (e->expires - now) : 0);
        } else {
            kprintf("  [%d] %s -> NEGATIVE (rcode=%d, expires in %llu ms)\n", i,
                    e->name, e->rcode,
                    (e->expires > now) ? (e->expires - now) : 0);
        }
    }
}

/* ── DNS server management ── */
static void dns_init_servers(void) {
    dns_server_count = 0;
    memset(dns_servers, 0, sizeof(dns_servers));
}

void dns_set_server(uint32_t ip) {
    if (!ip) return;
    /* Replace existing or add new */
    for (int i = 0; i < dns_server_count; i++) {
        if (dns_servers[i].ip == ip) return; /* already present */
    }
    if (dns_server_count < DNS_MAX_SERVERS) {
        dns_servers[dns_server_count].ip = ip;
        dns_servers[dns_server_count].active = 1;
        dns_server_count++;
    }
    /* Also update legacy dns_ip for backward compat */
    dns_ip = ip;
}

uint32_t dns_get_server(void) { return dns_ip; }

const char *dns_get_server_str(void) {
    static char ipstr[16];
    ip_to_str(dns_ip, ipstr);
    return ipstr;
}

void dns_init(void) {
    dns_cache_init();
    dns_init_servers();
    /* dns_ip default set in net_init, add it to server list too */
}

/* ── Build a DNS query packet (supports A and AAAA types) ── */
static int dns_build_query_typed(const char *hostname, uint16_t rtype,
                                 uint8_t *buf, int bufsize, uint16_t query_id) {
    if (bufsize < (int)sizeof(dns_header_t) + DNS_MAX_NAME + 4) return -1;

    dns_header_t *dns = (dns_header_t *)buf;
    memset(dns, 0, sizeof(*dns));
    dns->id = bswap16(query_id);
    dns->flags = bswap16(DNS_FLAG_RD);
    dns->qdcount = bswap16(1);

    uint8_t *qp = buf + sizeof(dns_header_t);
    int name_len = dns_encode_name(hostname, qp, bufsize - sizeof(dns_header_t) - 4);
    if (name_len < 0) return -1;
    qp += name_len;

    *(uint16_t *)qp = bswap16(rtype);       qp += 2;
    *(uint16_t *)qp = bswap16(DNS_CLASS_IN); qp += 2;

    return (int)(qp - buf);
}

static int __attribute__((unused)) dns_build_query(const char *hostname, uint8_t *buf, int bufsize, uint16_t query_id) {
    return dns_build_query_typed(hostname, DNS_TYPE_A, buf, bufsize, query_id);
}

/* ── Parse a DNS response and extract A/AAAA record ── */
/* Returns: 0 = found A record, 1 = found CNAME (name set), 2 = found AAAA, <0 = error */
static int __attribute__((unused)) dns_parse_response(const uint8_t *resp, int resp_len, uint16_t expected_id,
                              uint32_t *out_ip, uint8_t *out_ipv6,
                              char *cname_out, int cname_max) {
    if (resp_len < (int)sizeof(dns_header_t)) return -1;

    const dns_header_t *dns = (const dns_header_t *)resp;
    uint16_t id = bswap16(dns->id);
    if (id != expected_id) return -1;

    uint16_t flags = bswap16(dns->flags);
    int rcode = flags & DNS_RCODE_MASK;
    if (rcode != DNS_RCODE_OK) return -2 - rcode;

    int tc = (flags & DNS_FLAG_TC) != 0;
    if (tc) return -10; /* caller should retry over TCP */

    uint16_t ancount = bswap16(dns->ancount);
    uint16_t qdcount = bswap16(dns->qdcount);

    /* Skip question section */
    const uint8_t *p = resp + sizeof(dns_header_t);
    const uint8_t *end = resp + resp_len;
    for (int q = 0; q < qdcount; q++) {
        while (p < end) {
            if (*p == 0) { p++; break; }
            if ((*p & 0xC0) == 0xC0) { p += 2; break; }
            p += *p + 1;
        }
        p += 4; /* type + class */
    }

    int found_a = 0, found_aaaa = 0;

    /* Parse answer section */
    for (int a = 0; a < ancount; a++) {
        if (p >= end) break;

        /* Skip name (handle compression) */
        if ((*p & 0xC0) == 0xC0) {
            p += 2;
        } else {
            while (p < end && *p != 0) {
                if ((*p & 0xC0) == 0xC0) { p += 2; break; }
                p += *p + 1;
            }
            if (p < end && *p == 0) p++;
        }

        if (p + 10 > end) break;
        uint16_t atype  = bswap16(*(uint16_t *)p); p += 2;
        /* skip class */ p += 2;
        /* skip ttl */   p += 4;
        uint16_t rdlen  = bswap16(*(uint16_t *)p); p += 2;

        if (p + rdlen > end) break;

        if (atype == DNS_TYPE_A && rdlen == 4 && !found_a) {
            *out_ip = *(uint32_t *)p;
            found_a = 1;
        }

        if (atype == DNS_TYPE_AAAA && rdlen == 16 && !found_aaaa && out_ipv6) {
            memcpy(out_ipv6, p, 16);
            found_aaaa = 1;
        }

        if (atype == DNS_TYPE_CNAME && cname_out && rdlen > 0 && !found_a) {
            dns_decode_name(resp, resp_len, p, cname_out, cname_max);
            return 1; /* CNAME found, caller should re-query */
        }

        p += rdlen;
    }

    if (found_a) return 0;
    if (found_aaaa) return 2;
    return -1; /* no answer found */
}

/* ── Send a DNS query and wait for response ── */
static int __attribute__((unused)) dns_send_and_wait(const uint8_t *query, int qlen, uint16_t query_id __attribute__((unused)),
                             uint32_t server_ip, uint8_t *resp_out, int resp_max) {
    /* Route the query through the UDP socket stack: the reply is delivered to
     * this socket by netd (net_poll -> ip_recv -> udp_recv_packet). The old
     * implementation polled nic_recv() directly, racing netd for the same RX
     * ring — whichever thread drained first consumed the reply, so hostname
     * resolution would silently time out under traffic. */
    int fd = udp_socket_create();
    if (fd < 0) return -1;
    if (udp_connect(fd, server_ip, DNS_PORT) < 0) { udp_socket_close(fd); return -1; }
    if (udp_send(fd, query, qlen) < 0) { udp_socket_close(fd); return -1; }
    int rlen = udp_recv_timeout(fd, resp_out, resp_max, 0, 0, 800);
    udp_socket_close(fd);
    return rlen;
}

/* ── DNS query over TCP (for truncated responses) ── */
static int __attribute__((unused)) dns_tcp_query(const uint8_t *query, int qlen, uint16_t query_id __attribute__((unused)),
                         uint32_t server_ip, uint8_t *resp_out, int resp_max) {
    /* DNS over TCP: 2-byte length prefix + DNS message */
    if (legacy_tcp_connect(server_ip, DNS_PORT) < 0) return -1;

    /* Build TCP payload: 2-byte big-endian length + query */
    uint8_t tcp_payload[DNS_MAX_RESP + 2];
    tcp_payload[0] = (uint8_t)(qlen >> 8);
    tcp_payload[1] = (uint8_t)(qlen & 0xFF);
    memcpy(tcp_payload + 2, query, qlen);

    if (legacy_tcp_send_data(tcp_payload, qlen + 2) < 0) {
        legacy_tcp_close_conn();
        return -1;
    }

    /* Wait for response */
    int total = 0;
    int got_len = 0;
    int expected_len = 0;

    for (int tries = 0; tries < 100; tries++) {
        sched_sleep_ms(1);
        uint8_t b[2048]; int l;
        while ((l = nic_recv(b, sizeof(b))) > 0) {
            if (l < (int)(sizeof(struct eth_hdr) + sizeof(struct ip_hdr) + sizeof(struct tcp_hdr)))
                continue;
            struct eth_hdr *eth = (struct eth_hdr *)b;
            if (bswap16(eth->type) != 0x0800) continue;
            struct ip_hdr *rip = (struct ip_hdr *)(b + sizeof(struct eth_hdr));
            if (rip->protocol != 6) continue;
            int ip_hdr_len = (rip->ver_ihl & 0x0F) * 4;
            struct tcp_hdr *tcp = (struct tcp_hdr *)((uint8_t *)rip + ip_hdr_len);
            uint16_t sport = bswap16(tcp->src_port);
            if (sport != DNS_PORT) continue;

            int tcp_hdr_len = ((tcp->offset >> 4) & 0x0F) * 4;
            int tcp_data_len = bswap16(rip->total_len) - ip_hdr_len - tcp_hdr_len;
            if (tcp_data_len <= 0) continue;

            uint8_t *data = (uint8_t *)tcp + tcp_hdr_len;

            if (!got_len) {
                /* First 2 bytes are the length prefix */
                if (tcp_data_len >= 2) {
                    expected_len = (data[0] << 8) | data[1];
                    got_len = 1;
                    if (tcp_data_len > 2) {
                        int copy = tcp_data_len - 2;
                        if (copy > resp_max) copy = resp_max;
                        memcpy(resp_out, data + 2, copy);
                        total = copy;
                    }
                }
            } else {
                int copy = tcp_data_len;
                if (total + copy > resp_max) copy = resp_max - total;
                if (copy > 0) {
                    memcpy(resp_out + total, data, copy);
                    total += copy;
                }
            }

            if (total >= expected_len && expected_len > 0) {
                legacy_tcp_close_conn();
                return total;
            }

            if (tcp->flags & TCP_FIN) {
                legacy_tcp_close_conn();
                return total > 0 ? total : -1;
            }
        }
    }
    legacy_tcp_close_conn();
    return total > 0 ? total : -1;
}

/* ── Main DNS resolve function ── */
/* Parse a dotted-quad IPv4 literal ("a.b.c.d") into the host-order uint32
 * used throughout net.c. Returns 0 on success, -1 if not a literal. */
static int ipv4_literal_parse(const char *s, uint32_t *out) {
    uint32_t p[4]; int cnt = 0;
    while (cnt < 4) {
        if (*s < '0' || *s > '9') return -1;
        unsigned v = 0;
        while (*s >= '0' && *s <= '9') {
            v = v * 10u + (unsigned)(*s - '0');
            if (v > 255) return -1;
            s++;
        }
        p[cnt++] = v;
        if (cnt < 4) { if (*s != '.') return -1; s++; }
        else         { if (*s != 0) return -1; }
    }
    *out = p[0] | (p[1] << 8) | (p[2] << 16) | (p[3] << 24);
    return 0;
}

int dns_resolve(const char *hostname, uint32_t *ip) {
    if (!hostname || !ip) return -1;

    /* Fast-path: dotted-quad IP literals bypass DNS */
    if (ipv4_literal_parse(hostname, ip) == 0)
        return 0;
    /* Check cache first */
    if (dns_cache_lookup(hostname, ip) == 0) {
        return 0;
    }

    /* AdBlock check */
    if (adblock_is_enabled() && adblock_check_host(hostname)) {
        adblock_record_dns_block();
        return -1;
    }

    /* Determine which servers to try */
    int server_order[DNS_MAX_SERVERS];
    int n_servers = 0;
    if (dns_ip) server_order[n_servers++] = -1;
    for (int i = 0; i < dns_server_count && n_servers < DNS_MAX_SERVERS; i++) {
        if (dns_servers[i].ip != dns_ip)
            server_order[n_servers++] = i;
    }
    if (n_servers == 0) {
        dns_ip = bswap32((10 << 24) | (0 << 16) | (2 << 8) | 3);
        server_order[n_servers++] = -1;
    }

    char current_name[DNS_MAX_NAME];
    strlcpy(current_name, hostname, sizeof(current_name));

    /* CNAME following loop */
    for (int cname_depth = 0; cname_depth < DNS_MAX_CNAME; cname_depth++) {
        uint16_t query_id = ++dns_next_id;

        uint8_t query[DNS_MAX_RESP];
        int qlen = dns_build_query(current_name, query, sizeof(query), query_id);
        if (qlen < 0) return -1;

        for (int si = 0; si < n_servers; si++) {
            uint32_t server = (server_order[si] == -1) ? dns_ip : dns_servers[server_order[si]].ip;
            if (!server) continue;

            for (int retry = 0; retry < DNS_RETRIES; retry++) {
                uint8_t resp[DNS_MAX_RESP];
                int rlen = dns_send_and_wait(query, qlen, query_id, server, resp, sizeof(resp));
                if (rlen < (int)sizeof(dns_header_t)) continue;

                uint32_t resolved_ip = 0;
                uint8_t resolved_ipv6[16] = {0};
                char cname_target[DNS_MAX_NAME] = {0};
                int rc = dns_parse_response(resp, rlen, query_id, &resolved_ip,
                                           resolved_ipv6, cname_target, sizeof(cname_target));

                if (rc == 0) {
                    dns_cache_store(hostname, resolved_ip, DNS_RCODE_OK);
                    *ip = resolved_ip;
                    return 0;
                }

                if (rc == 1 && cname_target[0]) {
                    strlcpy(current_name, cname_target, sizeof(current_name));
                    break;
                }

                if (rc == -10) {
                    /* Truncated — retry over TCP */
                    rlen = dns_tcp_query(query, qlen, query_id, server, resp, sizeof(resp));
                    if (rlen >= (int)sizeof(dns_header_t)) {
                        resolved_ip = 0;
                        memset(resolved_ipv6, 0, 16);
                        cname_target[0] = 0;
                        rc = dns_parse_response(resp, rlen, query_id, &resolved_ip,
                                               resolved_ipv6, cname_target, sizeof(cname_target));
                        if (rc == 0) {
                            dns_cache_store(hostname, resolved_ip, DNS_RCODE_OK);
                            *ip = resolved_ip;
                            return 0;
                        }
                    }
                    continue;
                }

                if (rc <= -2 && rc >= -5) {
                    int rcode = -2 - rc;
                    dns_cache_store(hostname, 0, rcode);
                    return -1;
                }
            }
        }
        break;
    }

    return -1;
}

/* ── Resolve hostname to IPv6 address ── */
int dns_resolve_ipv6(const char *hostname, uint8_t *ipv6) {
    if (!hostname || !ipv6) return -1;

    /* Check cache first */
    if (dns_cache_lookup_ipv6(hostname, ipv6) == 0) {
        return 0;
    }

    /* AdBlock check */
    if (adblock_is_enabled() && adblock_check_host(hostname)) {
        adblock_record_dns_block();
        return -1;
    }

    int server_order[DNS_MAX_SERVERS];
    int n_servers = 0;
    if (dns_ip) server_order[n_servers++] = -1;
    for (int i = 0; i < dns_server_count && n_servers < DNS_MAX_SERVERS; i++) {
        if (dns_servers[i].ip != dns_ip)
            server_order[n_servers++] = i;
    }
    if (n_servers == 0) {
        dns_ip = bswap32((10 << 24) | (0 << 16) | (2 << 8) | 3);
        server_order[n_servers++] = -1;
    }

    char current_name[DNS_MAX_NAME];
    strlcpy(current_name, hostname, sizeof(current_name));

    for (int cname_depth = 0; cname_depth < DNS_MAX_CNAME; cname_depth++) {
        uint16_t query_id = ++dns_next_id;

        uint8_t query[DNS_MAX_RESP];
        int qlen = dns_build_query_typed(current_name, DNS_TYPE_AAAA,
                                         query, sizeof(query), query_id);
        if (qlen < 0) return -1;

        for (int si = 0; si < n_servers; si++) {
            uint32_t server = (server_order[si] == -1) ? dns_ip : dns_servers[server_order[si]].ip;
            if (!server) continue;

            for (int retry = 0; retry < DNS_RETRIES; retry++) {
                uint8_t resp[DNS_MAX_RESP];
                int rlen = dns_send_and_wait(query, qlen, query_id, server, resp, sizeof(resp));
                if (rlen < (int)sizeof(dns_header_t)) continue;

                uint32_t resolved_ip = 0;
                uint8_t resolved_ipv6[16] = {0};
                char cname_target[DNS_MAX_NAME] = {0};
                int rc = dns_parse_response(resp, rlen, query_id, &resolved_ip,
                                           resolved_ipv6, cname_target, sizeof(cname_target));

                if (rc == 2) {
                    /* AAAA record found */
                    dns_cache_store_ipv6(hostname, resolved_ipv6);
                    memcpy(ipv6, resolved_ipv6, 16);
                    return 0;
                }

                if (rc == 0) {
                    /* Got A record, store it but keep looking for AAAA */
                    dns_cache_store(hostname, resolved_ip, DNS_RCODE_OK);
                }

                if (rc == 1 && cname_target[0]) {
                    strlcpy(current_name, cname_target, sizeof(current_name));
                    break;
                }

                if (rc == -10) {
                    /* Truncated — retry over TCP */
                    rlen = dns_tcp_query(query, qlen, query_id, server, resp, sizeof(resp));
                    if (rlen >= (int)sizeof(dns_header_t)) {
                        resolved_ip = 0;
                        memset(resolved_ipv6, 0, 16);
                        cname_target[0] = 0;
                        rc = dns_parse_response(resp, rlen, query_id, &resolved_ip,
                                               resolved_ipv6, cname_target, sizeof(cname_target));
                        if (rc == 2) {
                            dns_cache_store_ipv6(hostname, resolved_ipv6);
                            memcpy(ipv6, resolved_ipv6, 16);
                            return 0;
                        }
                    }
                    continue;
                }

                if (rc <= -2 && rc >= -5) {
                    int rcode = -2 - rc;
                    dns_cache_store(hostname, 0, rcode);
                    return -1;
                }
            }
        }
        break;
    }

    return -1;
}

void dns_status(void) {
    char ipstr[16];
    kprintf("DNS Servers:\n");
    if (dns_ip) {
        ip_to_str(dns_ip, ipstr);
        kprintf("  Primary: %s\n", ipstr);
    }
    for (int i = 0; i < dns_server_count; i++) {
        if (dns_servers[i].ip && dns_servers[i].ip != dns_ip) {
            ip_to_str(dns_servers[i].ip, ipstr);
            kprintf("  Alt[%d]: %s\n", i, ipstr);
        }
    }
    dns_cache_dump();
}

/* ---- DHCP client ---- */

#define DHCP_SERVER_PORT 67
#define DHCP_CLIENT_PORT 68
#define DHCP_MAGIC_COOKIE 0x63825363

enum { DHCP_DISCOVER = 1, DHCP_OFFER, DHCP_REQUEST, DHCP_DECLINE, DHCP_ACK, DHCP_NAK, DHCP_RELEASE };

struct dhcp_packet {
    uint8_t  op;
    uint8_t  htype;
    uint8_t  hlen;
    uint8_t  hops;
    uint32_t xid;
    uint16_t secs;
    uint16_t flags;
    uint32_t ciaddr;
    uint32_t yiaddr;
    uint32_t siaddr;
    uint32_t giaddr;
    uint8_t  chaddr[16];
    char     sname[64];
    char     file[128];
    uint32_t magic;
    uint8_t  options[312];
} __attribute__((packed));

static uint32_t dhcp_xid;

static int dhcp_send(int type, uint32_t req_ip, uint32_t server_ip) {
    struct dhcp_packet dhcp;
    memset(&dhcp, 0, sizeof(dhcp));
    dhcp.op = 1;
    dhcp.htype = 1;
    dhcp.hlen = 6;
    dhcp.xid = dhcp_xid;
    dhcp.flags = bswap16(0x8000);
    memcpy(dhcp.chaddr, our_mac, 6);
    dhcp.magic = bswap32(DHCP_MAGIC_COOKIE);

    int opt = 0;
    dhcp.options[opt++] = 53; dhcp.options[opt++] = 1; dhcp.options[opt++] = type;
    dhcp.options[opt++] = 55; dhcp.options[opt++] = 3;
    dhcp.options[opt++] = 1;  /* subnet mask */
    dhcp.options[opt++] = 3;  /* router */
    dhcp.options[opt++] = 6;  /* dns */
    if (type == DHCP_REQUEST && req_ip) {
        dhcp.options[opt++] = 50; dhcp.options[opt++] = 4;
        *(uint32_t *)(dhcp.options + opt) = req_ip; opt += 4;
    }
    if (server_ip) {
        dhcp.options[opt++] = 54; dhcp.options[opt++] = 4;
        *(uint32_t *)(dhcp.options + opt) = server_ip; opt += 4;
    }
    dhcp.options[opt++] = 255; /* end */

    struct udp_hdr udp;
    udp.src_port = bswap16(DHCP_CLIENT_PORT);
    udp.dst_port = bswap16(DHCP_SERVER_PORT);
    udp.len = bswap16(sizeof(udp) + sizeof(struct dhcp_packet));
    udp.checksum = 0;

    uint8_t buf[2048];
    memcpy(buf, &udp, sizeof(udp));
    memcpy(buf + sizeof(udp), &dhcp, sizeof(dhcp));

    uint32_t bcast = 0xFFFFFFFF;
    /* temporarily send from 0.0.0.0 */
    uint32_t saved_ip = our_ip;
    our_ip = 0;
    int ret = send_ip_packet(bcast, 17, buf, sizeof(udp) + sizeof(dhcp));
    our_ip = saved_ip;
    return ret;
}

static int dhcp_recv_offer(uint32_t *yiaddr, uint32_t *server_ip, uint32_t *dns, uint32_t *gateway) {
    for (int tries = 0; tries < 2500; tries++) {
        timer_sleep_ms(2);
        uint8_t rbuf[2048]; int rlen;
        while ((rlen = nic_recv(rbuf, sizeof(rbuf))) > 0) {
            if (rlen < (int)(sizeof(struct eth_hdr) + sizeof(struct ip_hdr) + sizeof(struct udp_hdr))) continue;
            struct eth_hdr *eth = (struct eth_hdr *)rbuf;
            if (bswap16(eth->type) != 0x0800) continue;
            struct ip_hdr *rip = (struct ip_hdr *)(rbuf + sizeof(struct eth_hdr));
            if (rip->protocol != 17) continue;
            int ip_hdr_len = (rip->ver_ihl & 0x0F) * 4;
            struct udp_hdr *rudp = (struct udp_hdr *)((uint8_t *)rip + ip_hdr_len);
            int udp_len = bswap16(rudp->len);
            /* DHCP packet must at least cover fixed fields (236) + magic (4) */
            if (udp_len < (int)(sizeof(struct udp_hdr) + 240)) continue;
            struct dhcp_packet *dhcp = (struct dhcp_packet *)((uint8_t *)rudp + sizeof(struct udp_hdr));
            if (bswap32(dhcp->magic) != DHCP_MAGIC_COOKIE) continue;
            if (dhcp->xid != dhcp_xid) continue;

            /* parse options for message type */
            int opt = 0;
            int msg_type = 0;
            while (opt < 312) {
                if (dhcp->options[opt] == 255) break;
                if (dhcp->options[opt] == 0) { opt++; continue; }
                int len = dhcp->options[opt + 1];
                if (dhcp->options[opt] == 53 && len == 1)
                    msg_type = dhcp->options[opt + 2];
                else if (dhcp->options[opt] == 1 && len == 4)
                    { /* subnet mask */ }
                else if (dhcp->options[opt] == 3 && len == 4 && gateway)
                    *gateway = *(uint32_t *)(dhcp->options + opt + 2);
                else if (dhcp->options[opt] == 6 && len >= 4 && dns)
                    *dns = *(uint32_t *)(dhcp->options + opt + 2);
                else if (dhcp->options[opt] == 54 && len == 4 && server_ip)
                    *server_ip = *(uint32_t *)(dhcp->options + opt + 2);
                else if (dhcp->options[opt] == 51 && len == 4)
                    dhcp_lease_seconds = *(uint32_t *)(dhcp->options + opt + 2);
                opt += 2 + len;
            }
            if (msg_type == DHCP_OFFER && yiaddr) {
                *yiaddr = dhcp->yiaddr;
                return 0;
            }
            if (msg_type == DHCP_ACK && yiaddr) {
                *yiaddr = dhcp->yiaddr;
                return 1;
            }
        }
    }
    return -1;
}

int dhcp_configure(void) {
    if (!net_up) return -1;
    dhcp_xid = (ip_id << 16) | (uint32_t)((ip_id + 1) & 0xFFFF);
    ip_id += 2;

    if (dhcp_send(DHCP_DISCOVER, 0, 0) < 0) return -1;

    uint32_t yiaddr = 0, server_ip = 0, dns = 0, gateway = 0;
    dhcp_recv_offer(&yiaddr, &server_ip, &dns, &gateway);
    if (yiaddr == 0) {
        kprintf("DHCP: no offer received\n");
        return -1;
    }

    kprintf("DHCP: offered %d.%d.%d.%d\n",
            yiaddr & 0xFF, (yiaddr >> 8) & 0xFF,
            (yiaddr >> 16) & 0xFF, (yiaddr >> 24) & 0xFF);

    if (dhcp_send(DHCP_REQUEST, yiaddr, server_ip) < 0) return -1;

    uint32_t ack_ip = 0;
    int ret = dhcp_recv_offer(&ack_ip, NULL, &dns, &gateway);
    if (ret == 1 && ack_ip) {
        our_ip = ack_ip;
        if (gateway) gateway_ip = gateway;
        if (dns) dns_set_server(dns);
        /* lease time is big-endian seconds; renew at 50% to stay safe */
        uint32_t lease = bswap32(dhcp_lease_seconds);
        if (lease < 60) lease = 86400;
        dhcp_lease_expires = timer_get_milliseconds() + (uint64_t)lease * 500;
        kprintf("DHCP: lease %u sec, renew at %lld ms\n", lease, dhcp_lease_expires);
        {
            char s1[16], s2[16], s3[16];
            ip_to_str(our_ip, s1); ip_to_str(gateway_ip, s2); ip_to_str(dns_ip, s3);
            kprintf("DHCP: IP %s GW %s DNS %s\n", s1, s2, s3);
        }
        return 0;
    }

    kprintf("DHCP: failed (no ACK)\n");
    return -1;
}

/* HTTP/1.1 response body de-chunker. Servers commonly use chunked transfer
 * encoding; without stripping the framing, consumers (OpenWeb, the shell
 * download command) would see hex size lines and trailing CRLFs mixed into
 * the body. The filter is a byte-stream state machine so framing split
 * across packets is handled. Headers always pass through untouched; only
 * chunk framing after the header terminator is removed. */
#define HTTP_FILTER_HDR    0
#define HTTP_FILTER_SIZE   1
#define HTTP_FILTER_DATA   2
#define HTTP_FILTER_CRLF   3
#define HTTP_FILTER_TAIL   4
#define HTTP_FILTER_FLUSH  5
#define HTTP_FILTER_PASS   99

struct http_filter {
    int      state;
    int      chunked;                 /* "chunked" seen in headers */
    long     remain;                  /* bytes left in current chunk */
    int      crlf_need;               /* CRLF expected after chunk data */
    uint8_t  hdr[1024];               /* header staging until terminator */
    int      hdr_len, hdr_emit;
    uint8_t  line[64];                /* chunk-size line staging */
    int      line_len;
    uint8_t  pend;                    /* input byte deferred while flushing */
    int      pend_valid;
    int      need_retry;              /* caller should re-feed the same byte */
};

static int http_hexval(uint8_t c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

/* Feed one response byte through the filter.
 * Returns:  1 = emitted a byte (header or body), *out is set
 *           0 = consumed framing byte, nothing emitted
 *          -1 = need retry (same input byte not consumed yet) */
static int http_filter_put(struct http_filter *f, uint8_t in, uint8_t *out) {
    /* If we are in the dedicated flush state, emit one staged header byte
     * and defer the input byte for later. Return -1 to signal the caller
     * to re-feed the same input byte. */
    if (f->state == HTTP_FILTER_FLUSH) {
        if (f->hdr_emit < f->hdr_len) {
            *out = f->hdr[f->hdr_emit++];
            return -1;  /* need retry: input byte not consumed */
        }
        f->state = f->chunked ? HTTP_FILTER_SIZE : HTTP_FILTER_PASS;
        /* fall through to process the deferred input byte in the new state */
    } else if (f->state != HTTP_FILTER_HDR && f->hdr_emit < f->hdr_len) {
        /* Back-compat flush for states entered before the FLUSH rework. */
        *out = f->hdr[f->hdr_emit++];
        return 1;
    }
    switch (f->state) {
    case HTTP_FILTER_HDR:
        if (f->hdr_len >= (int)sizeof(f->hdr)) {
            /* No terminator found in a huge header block: emit everything
             * raw rather than corrupt the response. */
            f->state = HTTP_FILTER_FLUSH;
            f->hdr_emit = 0;
            return http_filter_put(f, in, out);
        }
        f->hdr[f->hdr_len++] = in;
        if (!f->chunked && f->hdr_len >= 7) {
            int i = f->hdr_len - 7, m = 1;
            for (int j = 0; j < 7; j++) {
                uint8_t a = f->hdr[i + j];
                if (a >= 'A' && a <= 'Z') a += 32;
                if (a != (uint8_t)"chunked"[j]) { m = 0; break; }
            }
            if (m) f->chunked = 1;
        }
        {
            int n = f->hdr_len;
            if (n >= 4 && f->hdr[n-1] == '\n' && f->hdr[n-2] == '\r' &&
                f->hdr[n-3] == '\n' && f->hdr[n-4] == '\r') {
                f->state = HTTP_FILTER_FLUSH;
                f->hdr_emit = 0;
            }
        }
        return 0;

    case HTTP_FILTER_SIZE:
        if (f->line_len >= (int)sizeof(f->line)) {
            f->state = HTTP_FILTER_PASS;
            f->hdr_emit = f->hdr_len;
            return http_filter_put(f, in, out);
        }
        f->line[f->line_len++] = in;
        if (f->line_len >= 2 && f->line[f->line_len-1] == '\n' &&
            f->line[f->line_len-2] == '\r') {
            long sz = 0;
            for (int i = 0; i < f->line_len - 2; i++) {
                int v = http_hexval(f->line[i]);
                if (v < 0) break;
                sz = sz * 16 + v;
            }
            f->line_len = 0;
            if (sz <= 0) f->state = HTTP_FILTER_TAIL;
            else { f->remain = sz; f->state = HTTP_FILTER_DATA; }
        }
        return 0;

    case HTTP_FILTER_DATA:
        if (f->remain > 0) { f->remain--; *out = in; return 1; }
        f->state = HTTP_FILTER_CRLF;
        f->crlf_need = 2;
        return http_filter_put(f, in, out);

    case HTTP_FILTER_CRLF:
        if (f->crlf_need == 2) {
            if (in == '\r') f->crlf_need = 1;
        } else if (in == '\n') {
            f->state = HTTP_FILTER_SIZE;
            f->crlf_need = 2;
        } else {
            f->crlf_need = 2;          /* tolerate stray bytes */
        }
        return 0;

    case HTTP_FILTER_TAIL:
        return 0;                      /* discard trailing chunk + trailers */

    default: /* HTTP_FILTER_PASS */
        *out = in;
        return 1;
    }
}

/* Drain any staged header bytes that haven't been emitted yet.
 * Call after feeding a complete segment, or at the end of the exchange. */
static int http_filter_flush(struct http_filter *f, uint8_t *out) {
    if (f->hdr_emit < f->hdr_len) {
        *out = f->hdr[f->hdr_emit++];
        return 1;
    }
    return 0;
}

/* Feed a received segment through the shared de-chunker, writing decoded
 * body bytes to `buf`. Mirrors the loop inside http_exchange() so TLS and
 * plain HTTP responses are treated identically (header passes through raw,
 * chunk framing stripped). */
static int http_filter_feed(struct http_filter *f, const uint8_t *seg, int n,
                            uint8_t *buf, uint16_t max_len, int total) {
    for (int k = 0; k < n; k++) {
        uint8_t ob;
        int fr = http_filter_put(f, seg[k], &ob);
        if (fr > 0) {
            if (total < max_len) buf[total++] = ob;
        } else if (fr < 0) {
            k--;
        }
    }
    uint8_t ob;
    while (http_filter_flush(f, &ob)) {
        if (total < max_len) buf[total++] = ob;
    }
    return total;
}

/* One shared HTTP exchange path for GET and POST: build the request, open the
 * TCP connection, send it, then drain the response through the de-chunker.
 * The caller handles policy (ad-blocking) and picks the method/body. */
static int http_exchange(const char *host, uint16_t port,
                         const char *method, const char *path,
                         const void *body, uint16_t body_len,
                         void *buf, uint16_t max_len, http_progress_cb cb) {
    if (!net_up) return -1;

    uint32_t ip;
    if (dns_resolve(host, &ip) < 0) return -1;

    int fd = tcp_connect(ip, port);
    if (fd < 0) return -1;

    char req[1024];
    int pos = 0;
    const char *parts[] = {method, " ", path, " HTTP/1.1\r\nHost: ", host,
                           "\r\nUser-Agent: codeos-kernel/1.0\r\n",
                           "Connection: close\r\nAccept: */*\r\n"
                           "Accept-Encoding: identity\r\n", NULL};
    for (int i = 0; parts[i]; i++) {
        const char *s = parts[i];
        while (*s && pos < (int)sizeof(req) - 1) req[pos++] = *s++;
    }
    if (body && body_len > 0) {
        const char *lenhdr = "Content-Length: ";
        while (*lenhdr && pos < (int)sizeof(req) - 1) req[pos++] = *lenhdr++;
        unsigned int v = body_len;
        char digits[10];
        int nd = 0;
        do { digits[nd++] = (char)('0' + (v % 10)); v /= 10; } while (v && nd < 10);
        while (nd > 0 && pos < (int)sizeof(req) - 1) req[pos++] = digits[--nd];
        if (pos < (int)sizeof(req) - 1) req[pos++] = '\r';
        if (pos < (int)sizeof(req) - 1) req[pos++] = '\n';
    }
    {
        const char *tail = "\r\n";
        while (*tail && pos < (int)sizeof(req) - 1) req[pos++] = *tail++;
    }
    if (body && body_len > 0) {
        int cp = body_len;
        if (pos + cp > (int)sizeof(req)) cp = (int)sizeof(req) - pos;
        memcpy(req + pos, body, cp);
        pos += cp;
    }

    if (tcp_send(fd, req, pos) < 0) { tcp_close(fd); return -1; }

    struct http_filter hf;
    memset(&hf, 0, sizeof(hf));
    hf.state = HTTP_FILTER_HDR;

    /* Receive via the multi-socket tcp.c layer: the netd thread pumps the NIC
     * ring and tcp_process_packet() feeds this socket's recv_buf, so several
     * http exchanges (OpenWeb, updater, pkg sync) can run concurrently
     * without trampling one shared connection's state. */
    int total = 0;
    for (int tries = 0; tries < 200 && total < max_len; tries++) {
        if (cb && tries % 4 == 0) cb(total, max_len);
        uint8_t chunk[TCP_MSS];
        int r = tcp_recv(fd, chunk, sizeof(chunk));
        if (r == 0) { tcp_close(fd); return total; }  /* peer FIN: clean EOF */
        if (r < 0) break;
        total = http_filter_feed(&hf, chunk, r, (uint8_t *)buf, max_len, total);
    }
    tcp_close(fd);
    return total;
}

int http_get_with_progress(const char *host, uint16_t port, const char *path, void *buf, uint16_t max_len, http_progress_cb cb) {
    if (!net_up) return -1;

    if (adblock_is_enabled() && adblock_check_host(host)) {
        adblock_record_http_block();
        kprintf("AdBlock: blocked HTTP request to %s\n", host);
        return -1;
    }

    return http_exchange(host, port, "GET", path, NULL, 0, buf, max_len, cb);
}

int http_get(const char *host, uint16_t port, const char *path, void *buf, uint16_t max_len) {
    return http_get_with_progress(host, port, path, buf, max_len, NULL);
}

int http_post(const char *host, uint16_t port, const char *path, const void *body, uint16_t body_len, void *buf, uint16_t max_len) {
    if (!net_up) return -1;

    if (adblock_is_enabled() && adblock_check_host(host)) {
        adblock_record_http_block();
        kprintf("AdBlock: blocked HTTP POST to %s\n", host);
        return -1;
    }

    return http_exchange(host, port, "POST", path, body, body_len, buf, max_len, NULL);
}

/* Retry wrapper: user-mode SLIRP occasionally drops a SYN; retry before
 * we blame the HTTP/de-chunk path. */
static int fsmoke_get(const char *host, uint16_t port, const char *path,
                      char *buf, uint16_t max) {
    for (int a = 0; a < 3; a++) {
        int n = http_get(host, port, path, buf, max);
        if (n > 0) return n;
        kprintf("FORMTEST: retry %d for %s (n=%d)\n", a, path, n);
        sched_sleep_ms(500);
    }
    return -1;
}

/* ── OpenWeb form/HTTP smoke test (drives the same http_get/http_post/
 * chunked paths the browser uses) against a host demo server (10.0.2.2).
 * Evidence is captured in the serial log. ─────────────────────────── */
void net_form_smoke(void) {
    char buf[65535];
    int n;

    kprintf("FORMTEST: begin\n");

    /* 1) Plain GET of a form page (http_get + de-chunker). */
    n = fsmoke_get("10.0.2.2", 9001, "/form", buf, sizeof(buf) - 1);
    if (n <= 0) { kprintf("FORMTEST: GET /form FAILED n=%d\n", n); }
    else {
        buf[n] = 0;
        kprintf("FORMTEST: GET /form %d bytes\n", n);
        if (strstr(buf, "Login form"))   kprintf("FORMTEST:   + title 'Login form'\n");
        if (strstr(buf, "method=\"post\"")) kprintf("FORMTEST:   + POST form action /echo\n");
        if (strstr(buf, "name=\"user\""))   kprintf("FORMTEST:   + field name='user'\n");
        if (strstr(buf, "type=\"password\"")) kprintf("FORMTEST:   + field type='password'\n");
        if (strstr(buf, "form-smoke-ok")) kprintf("FORMTEST:   + marker 'form-smoke-ok'\n");
    }

    /* 2) POST a url-encoded form body to /echo; server echoes it back. */
    const char *body = "user=alice&pass=hunter2&submit=Go";
    n = http_post("10.0.2.2", 9001, "/echo", body, (uint16_t)strlen(body),
                  buf, sizeof(buf) - 1);
    if (n <= 0) { kprintf("FORMTEST: POST /echo FAILED n=%d\n", n); }
    else {
        buf[n] = 0;
        kprintf("FORMTEST: POST /echo %d bytes\n", n);
        if (strstr(buf, "echo-received")) kprintf("FORMTEST:   + server echoed headline\n");
        if (strstr(buf, "user=alice&pass=hunter2")) {
            kprintf("FORMTEST:   + POST body preserved: user=alice&pass=hunter2\n");
        } else {
            kprintf("FORMTEST:   + WARN post body not echoed\n");
        }
    }

    /* 3) Chunked transfer-encoding GET (de-chunker strips hex framing). */
    n = fsmoke_get("10.0.2.2", 9001, "/chunked", buf, sizeof(buf) - 1);
    if (n <= 0) { kprintf("FORMTEST: GET /chunked FAILED n=%d\n", n); }
    else {
        buf[n] = 0;
        kprintf("FORMTEST: GET /chunked %d bytes\n", n);
        if (strstr(buf, "chunked-body-marker-12345"))
            kprintf("FORMTEST:   + chunked body decoded cleanly\n");
        if (strstr(buf, "6f\r\n") || strstr(buf, "\r\n\r\n2\r\n"))
            kprintf("FORMTEST:   + WARN stray chunk framing leaked\n");
    }

    kprintf("FORMTEST: done\n");
}

/* TEMPORARY self-test harness - remove after verification */
void net_selftest(void) {
    char buf[8192];
    int n;
    uint32_t ip;
    uint8_t mac[6];
    kprintf("SELFTEST: begin\n");
    kprintf("SELFTEST: net_up=%d\n", net_up);
    n = dns_resolve("10.0.2.2", &ip);
    kprintf("SELFTEST: dns_resolve=0x%08x -> %d\n", ip, n);
    n = legacy_arp_resolve(ip, mac);
    kprintf("SELFTEST: arp_resolve(0x%08x)=%d mac=%02x:%02x:%02x:%02x:%02x:%02x\n",
            ip, n, mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    n = legacy_tcp_connect(ip, 9001);
    kprintf("SELFTEST: tcp_connect=%d\n", n);
    n = http_get("10.0.2.2", 9001, "/chunked", buf, sizeof(buf) - 1);
    if (n < 0) kprintf("SELFTEST: chunked GET FAILED\n");
    else { buf[n] = 0; kprintf("SELFTEST: chunked GET %d bytes: [%s]\n", n, buf); }
    n = http_post("10.0.2.2", 9001, "/echo", "hello-selftest", 14, buf, sizeof(buf) - 1);
    if (n < 0) kprintf("SELFTEST: POST FAILED\n");
    else { buf[n] = 0; kprintf("SELFTEST: POST %d bytes: [%s]\n", n, buf); }
    {
        ws_client_t w;
        n = ws_connect(&w, "10.0.2.2", 9001, 0, "/ws");
        kprintf("SELFTEST: ws_connect=%d\n", n);
        if (n == 0) {
            n = ws_send_text(&w, "ws-selftest");
            kprintf("SELFTEST: ws_send=%d\n", n);
            n = ws_recv(&w, buf, sizeof(buf) - 1, 5000);
            if (n < 0) kprintf("SELFTEST: ws_recv FAILED\n");
            else { buf[n] = 0; kprintf("SELFTEST: ws_recv %d bytes: [%s]\n", n, buf); }
            ws_close(&w);
        }
    }
    kprintf("SELFTEST: done\n");
}

int icmp_ping(uint32_t ip, int timeout_ms) {
    if (!net_up) return -1;
    static uint16_t ping_id;
    uint16_t id = ++ping_id;
    uint16_t seq = 1;

    /* Drain any stale replies for this id from the queue BEFORE sending */
    { uint32_t dummy; while (icmp_reply_dequeue(id, seq, &dummy) == 0) {} }

    /* Use icmp_send_echo → ip_send (the same proven path that
     * icmp_echo_seq/HTTPSBOOT uses) instead of the legacy
     * send_ip_packet path which has subtle IP header bugs. */
    icmp_send_echo(ip, id, seq);

    /* Use sched_sleep_ms (not timer_sleep_ms) so the scheduler yields to
     * the netd thread which must call net_poll() → nic_recv() → ip_recv()
     * → icmp_recv() to enqueue the reply into the queue we check below. */
    uint64_t start = timer_get_milliseconds();
    while ((int)(timer_get_milliseconds() - start) < timeout_ms) {
        sched_sleep_ms(1);
        uint32_t src;
        if (icmp_reply_dequeue(id, seq, &src) == 0)
            return (int)(timer_get_milliseconds() - start);
    }
    return -1;
}

void net_set_ip(uint32_t ip) { our_ip = ip; ip_set_addr(ip); }
void net_set_gateway(uint32_t ip) { gateway_ip = ip; ip_set_gateway(ip); }
void net_set_dns(uint32_t ip) { dns_ip = ip; dns_set_server(ip); }

int net_ready(void) {
    return net_up;
}

static void net_poll_thread(void) {
    for (;;) {
        net_poll();
        sched_sleep_ms(50);
    }
}

int net_init(void) {
    fix_page_tables();
    dns_init();
    tcp_init();

    if (nic_init() < 0) { return -1; }
    nic_get_mac(our_mac);

    our_ip = bswap32((10 << 24) | (0 << 16) | (2 << 8) | 15);
    if (net_override_ip) our_ip = bswap32(net_override_ip);
    gateway_ip = bswap32((10 << 24) | (0 << 16) | (2 << 8) | 1);
    dns_ip = bswap32((10 << 24) | (0 << 16) | (2 << 8) | 3);
    ip_id = 0;
    gateway_mac_valid = 0;
    net_up = 1;

    /* Drain any stale packets and wait for link (timer-based, non-blocking) */
    for (int i = 0; i < 50; i++) {
        timer_sleep_ms(2);
        uint8_t b[2048]; while (nic_recv(b, sizeof(b)) > 0);
    }

    net_poll();

    /* Try DHCP with multiple attempts; fall back to static IP */
    int dhcp_ok = 0;
    for (int attempt = 0; attempt < 3; attempt++) {
        if (dhcp_configure() == 0) { dhcp_ok = 1; break; }
        timer_sleep_ms(20);
        net_poll();
    }
    if (!dhcp_ok) {
        kprintf("Net: using static IP 10.0.2.15\n");
    }

    /* Keep the structured ip.c stack (used by tcp.c/udp.c socket TX/RX and
     * ARP) in sync with whichever address the legacy path ended up with. */
    ip_set_addr(our_ip);
    ip_set_gateway(gateway_ip);

    legacy_arp_resolve(gateway_ip, gateway_mac);
    if (gateway_mac_valid)
        arp_update(gateway_ip, gateway_mac);

    /* Background RX pump: without periodic net_poll() no inbound packet is
       ever processed after init (TCP data, later DHCP renewals, ARP...). */
    sched_create_thread("netd", net_poll_thread);

    return 0;
}

/* ── TCP socket abstraction ── */

static int tcp_sock_send_syn(tcp_sock_t *s) {
    uint8_t seg[2048];
    struct tcp_hdr *tcp = (struct tcp_hdr *)seg;
    memset(tcp, 0, sizeof(*tcp));
    tcp->src_port = bswap16(s->src_port);
    tcp->dst_port = bswap16(s->dst_port);
    tcp->seq = bswap32(s->seq);
    tcp->ack = bswap32(s->ack);
    tcp->flags = TCP_SYN;
    tcp->window = bswap16(65535);
    tcp->offset = 0x60;
    uint8_t opts[] = {2, 4, 0x05, 0xB4};
    memcpy(seg + sizeof(*tcp), opts, 4);
    tcp->checksum = 0;
    tcp->checksum = tcp_csum(our_ip, s->dst_ip, seg, sizeof(*tcp) + 4);
    return send_ip_packet(s->dst_ip, 6, seg, sizeof(*tcp) + 4);
}

int tcp_sock_connect(tcp_sock_t *s, uint32_t dst_ip, uint16_t dst_port) {
    if (!net_up) return -1;
    s->state = TCP_SOCK_CLOSED;
    s->dst_ip = dst_ip;
    s->src_port = 49152 + (ip_id % 1284);
    s->dst_port = dst_port;
    s->seq = 1000;
    s->ack = 0;
    s->rx_len = 0;
    ip_id += 2;

    if (tcp_sock_send_syn(s) < 0) return -1;
    s->state = TCP_SOCK_SYN_SENT;
    uint32_t syn_seq = s->seq;

    /* Retransmit the SYN so a dropped segment can't stall the handshake:
     * one attempt every ~250ms, ~3s overall. send_ip_packet never touches
     * s->seq on the retransmit path (we restore it first). */
    for (int tries = 0; tries < 600; tries++) {
        timer_sleep_ms(5);
        if (tries > 0 && (tries % 50) == 0) {
            s->seq = syn_seq;
            tcp_sock_send_syn(s);
        }
        uint8_t b[2048];
        int l;
        while ((l = nic_recv(b, sizeof(b))) > 0) {
            if (l < (int)sizeof(struct eth_hdr)) continue;
            struct eth_hdr *eth = (struct eth_hdr *)b;
            if (bswap16(eth->type) != 0x0800) continue;
            struct ip_hdr *ip = (struct ip_hdr *)(b + sizeof(struct eth_hdr));
            if (ip->protocol != 6) continue;
            int ip_hdr_len = (ip->ver_ihl & 0x0F) * 4;
            struct tcp_hdr *t = (struct tcp_hdr *)((uint8_t *)ip + ip_hdr_len);
            uint16_t dport = bswap16(t->dst_port);
            uint16_t sport = bswap16(t->src_port);
            if (dport != s->src_port || sport != s->dst_port) continue;
            if (ip->src_ip != s->dst_ip) continue;
            if ((t->flags & (TCP_SYN | TCP_ACK)) == (TCP_SYN | TCP_ACK)) {
                s->ack = bswap32(t->seq) + 1;
                s->seq = bswap32(t->ack);

                uint8_t ack_seg[2048];
                struct tcp_hdr *ack_tcp = (struct tcp_hdr *)ack_seg;
                memset(ack_tcp, 0, sizeof(*ack_tcp));
                ack_tcp->src_port = bswap16(s->src_port);
                ack_tcp->dst_port = bswap16(s->dst_port);
                ack_tcp->seq = bswap32(s->seq);
                ack_tcp->ack = bswap32(s->ack);
                ack_tcp->flags = TCP_ACK;
                ack_tcp->window = bswap16(65535);
                ack_tcp->offset = 0x50;
                ack_tcp->checksum = 0;
                ack_tcp->checksum = tcp_csum(our_ip, s->dst_ip, ack_seg, sizeof(*ack_tcp));
                send_ip_packet(s->dst_ip, 6, ack_seg, sizeof(*ack_tcp));

                s->state = TCP_SOCK_ESTAB;
                return 0;
            }
        }
    }
    s->state = TCP_SOCK_CLOSED;
    return -1;
}

int tcp_sock_send(tcp_sock_t *s, const void *data, int len) {
    if (s->state != TCP_SOCK_ESTAB) return -1;
    if (!data || len <= 0) return -1;
    if (len > 1024) len = 1024;

    uint8_t seg[2048];
    struct tcp_hdr *tcp = (struct tcp_hdr *)seg;
    memset(tcp, 0, sizeof(*tcp));
    tcp->src_port = bswap16(s->src_port);
    tcp->dst_port = bswap16(s->dst_port);
    tcp->seq = bswap32(s->seq);
    tcp->ack = bswap32(s->ack);
    tcp->flags = TCP_PSH | TCP_ACK;
    tcp->window = bswap16(65535);
    tcp->offset = 0x50;
    int total = sizeof(*tcp) + len;
    memcpy(seg + sizeof(*tcp), data, len);
    tcp->checksum = 0;
    tcp->checksum = tcp_csum(our_ip, s->dst_ip, seg, total);

    if (send_ip_packet(s->dst_ip, 6, seg, total) < 0) return -1;
    s->seq += len;

    timer_sleep_ms(3);
    return len;
}

int tcp_sock_recv(tcp_sock_t *s, void *buf, int max_len, int timeout_ms) {
    if (s->state != TCP_SOCK_ESTAB || !buf || max_len <= 0) return -1;
    int tries = timeout_ms < 10 ? 1 : timeout_ms / 10;
    for (int i = 0; i < tries; i++) {
        timer_sleep_ms(1);
        uint8_t b[2048];
        int l;
        while ((l = nic_recv(b, sizeof(b))) > 0) {
            if (l < (int)sizeof(struct eth_hdr)) continue;
            struct eth_hdr *eth = (struct eth_hdr *)b;
            if (bswap16(eth->type) != 0x0800) continue;
            struct ip_hdr *ip = (struct ip_hdr *)(b + sizeof(struct eth_hdr));
            if (ip->protocol != 6) continue;
            int ip_hdr_len = (ip->ver_ihl & 0x0F) * 4;
            struct tcp_hdr *t = (struct tcp_hdr *)((uint8_t *)ip + ip_hdr_len);
            uint16_t dport = bswap16(t->dst_port);
            uint16_t sport = bswap16(t->src_port);
            if (dport != s->src_port || sport != s->dst_port) continue;
            if (ip->src_ip != s->dst_ip) continue;
            int tcp_header_len = ((t->offset >> 4) & 0x0F) * 4;
            int tcp_data_len = bswap16(ip->total_len) - ip_hdr_len - tcp_header_len;
            if (t->flags & TCP_RST) {
                /* Peer reset: connection is gone. */
                s->state = TCP_SOCK_CLOSED;
                return -1;
            }
            if (tcp_data_len > 0) {
                uint32_t seg_seq = bswap32(t->seq);
                /* Only accept contiguous, in-order data; anything else is a
                 * duplicate/retransmission or arrived while we were ACKing a
                 * gap. Re-ACK our position so the peer retransmits from it. */
                if (seg_seq != s->ack) {
                    uint8_t ack_seg[2048];
                    struct tcp_hdr *ack_tcp = (struct tcp_hdr *)ack_seg;
                    memset(ack_tcp, 0, sizeof(*ack_tcp));
                    ack_tcp->src_port = bswap16(s->src_port);
                    ack_tcp->dst_port = bswap16(s->dst_port);
                    ack_tcp->seq = bswap32(s->seq);
                    ack_tcp->ack = bswap32(s->ack);
                    ack_tcp->flags = TCP_ACK;
                    ack_tcp->window = bswap16(65535);
                    ack_tcp->offset = 0x50;
                    ack_tcp->checksum = 0;
                    ack_tcp->checksum = tcp_csum(our_ip, s->dst_ip, ack_seg, sizeof(*ack_tcp));
                    send_ip_packet(s->dst_ip, 6, ack_seg, sizeof(*ack_tcp));
                    continue;
                }
                uint8_t *tcp_data = (uint8_t *)t + tcp_header_len;
                int copy = tcp_data_len;
                if (copy > max_len) copy = max_len;
                s->ack = seg_seq + tcp_data_len;

                uint8_t ack_seg[2048];
                struct tcp_hdr *ack_tcp = (struct tcp_hdr *)ack_seg;
                memset(ack_tcp, 0, sizeof(*ack_tcp));
                ack_tcp->src_port = bswap16(s->src_port);
                ack_tcp->dst_port = bswap16(s->dst_port);
                ack_tcp->seq = bswap32(s->seq);
                ack_tcp->ack = bswap32(s->ack);
                ack_tcp->flags = TCP_ACK;
                ack_tcp->window = bswap16(65535);
                ack_tcp->offset = 0x50;
                ack_tcp->checksum = 0;
                ack_tcp->checksum = tcp_csum(our_ip, s->dst_ip, ack_seg, sizeof(*ack_tcp));
                send_ip_packet(s->dst_ip, 6, ack_seg, sizeof(*ack_tcp));

                memcpy(buf, tcp_data, copy);
                return copy;
            }
            if (t->flags & TCP_FIN) {
                uint8_t fin_ack[2048];
                struct tcp_hdr *fa = (struct tcp_hdr *)fin_ack;
                memset(fa, 0, sizeof(*fa));
                fa->src_port = bswap16(s->src_port);
                fa->dst_port = bswap16(s->dst_port);
                fa->seq = bswap32(s->seq);
                fa->ack = bswap32(bswap32(t->seq) + 1);  /* ack the FIN byte */
                fa->flags = TCP_ACK;
                fa->window = bswap16(65535);
                fa->offset = 0x50;
                fa->checksum = 0;
                fa->checksum = tcp_csum(our_ip, s->dst_ip, fin_ack, sizeof(*fa));
                send_ip_packet(s->dst_ip, 6, fin_ack, sizeof(*fa));
                s->state = TCP_SOCK_CLOSED;
                return -1;
            }
        }
    }
    return 0;
}

void tcp_sock_close(tcp_sock_t *s) {
    if (s->state != TCP_SOCK_ESTAB) { s->state = TCP_SOCK_CLOSED; return; }
    uint8_t seg[2048];
    struct tcp_hdr *tcp = (struct tcp_hdr *)seg;
    memset(tcp, 0, sizeof(*tcp));
    tcp->src_port = bswap16(s->src_port);
    tcp->dst_port = bswap16(s->dst_port);
    tcp->seq = bswap32(s->seq);
    tcp->ack = bswap32(s->ack);
    tcp->flags = TCP_FIN | TCP_ACK;
    tcp->window = bswap16(65535);
    tcp->offset = 0x50;
    tcp->checksum = 0;
    tcp->checksum = tcp_csum(our_ip, s->dst_ip, seg, sizeof(*tcp));
    send_ip_packet(s->dst_ip, 6, seg, sizeof(*tcp));
    s->seq++;
    timer_sleep_ms(3);
    s->state = TCP_SOCK_CLOSED;
}

/* ──────────────────────────────────────────────────────────────
 * HTTPS / TLS 1.2 over the single-connection TCP stack above.
 *
 * The TCP model is a single outstanding connection (module globals), the same
 * one http_get() uses.  We therefore map Mbed TLS's BIO send/recv onto the
 * global legacy_tcp_send_data()/nic_recv() state and run the handshake from https_get.
 * ── */
#include "mbedtls/build_info.h"
#include "mbedtls/ssl.h"
#include "mbedtls/entropy.h"
#include "mbedtls/ctr_drbg.h"

extern void mbedtls_codeos_platform_init(void);   /* mbedtls_codeos_platform.c */

/* ── HTTPS certificate verification ─────────────────────────
 * Default policy is fail-closed (VERIFY_REQUIRED): the server's chain must
 * verify against the embedded Mozilla root store and match the requested
 * hostname.  https_set_insecure(1) downgrades to accept-anything (dev). */
static int codeos_verify_cb(void *ctx, mbedtls_x509_crt *crt, int depth, uint32_t *flags) {
    (void)crt;
    uint32_t f = *flags;
    if (f == 0) return 0;
    if (https_insecure()) { *flags = 0; return 0; }
    const char *host = (const char *)ctx;
    kprintf("https: cert verify FAILED host=%s depth=%d flags=0x%x\n",
            host ? host : "?", depth, (unsigned)f);
    if (f & MBEDTLS_X509_BADCERT_NOT_TRUSTED) kprintf("   no trusted root for chain\n");
    if (f & MBEDTLS_X509_BADCERT_CN_MISMATCH) kprintf("   hostname mismatch\n");
    if (f & MBEDTLS_X509_BADCERT_EXPIRED)     kprintf("   certificate expired\n");
    if (f & MBEDTLS_X509_BADCERT_FUTURE)      kprintf("   not yet valid\n");
    if (f & MBEDTLS_X509_BADCERT_BAD_KEY)     kprintf("   key usage violation\n");
    if (f & MBEDTLS_X509_BADCERT_REVOKED)     kprintf("   certificate revoked\n");
    if (f & MBEDTLS_X509_BADCERT_BAD_MD)      kprintf("   weak signature algorithm\n");
    return MBEDTLS_ERR_X509_CERT_VERIFY_FAILED;
}

/* receive ring: buffers TCP payload so mbedtls's BIO recv can return
 * arbitrary chunk sizes (a single TCP segment may be smaller than what TLS
 * asks for, or a segment may carry more than requested). */
static uint8_t tls_rx_buf[8192];
static size_t tls_rx_head, tls_rx_tail;
static int tls_rx_closed;

static uint64_t tls_ms_now(void) {
    uint64_t t = timer_get_ticks();
    uint32_t hz = timer_get_frequency();
    return hz ? (t * 1000ULL) / (uint64_t)hz : (t * 1000ULL);
}

static void tls_rx_reset(void) {
    tls_rx_head = 0; tls_rx_tail = 0; tls_rx_closed = 0;
}

/* append a parsed TCP payload chunk into the ring (drop oldest if full) */
static void tls_rx_push(const uint8_t *data, int len) {
    for (int i = 0; i < len; i++) {
        size_t next = (tls_rx_tail + 1) % sizeof(tls_rx_buf);
        if (next == tls_rx_head)                /* full: discard oldest byte */
            tls_rx_head = (tls_rx_head + 1) % sizeof(tls_rx_buf);
        tls_rx_buf[tls_rx_tail] = data[i];
        tls_rx_tail = next;
    }
}

/* read `want` bytes (up to timeout) from the TCP stream into out.
 * returns >0 bytes, 0 on timeout-without-data, -2 on peer close. */
static int tls_tcp_recv(uint8_t *out, int want, int timeout_ms) {
    int got = 0;
    uint64_t deadline = tls_ms_now() + (uint64_t)timeout_ms;
    for (;;) {
        while (got < want && tls_rx_head != tls_rx_tail) {
            out[got++] = tls_rx_buf[tls_rx_head];
            tls_rx_head = (tls_rx_head + 1) % sizeof(tls_rx_buf);
        }
        if (got > 0) return got;
        if (tls_rx_closed || tcp_state != TCP_ESTABLISHED) return -2;
        if (timeout_ms > 0 && tls_ms_now() >= deadline) return -1;
        sched_sleep_ms(1);
        uint8_t b[2048]; int l;
        while ((l = nic_recv(b, sizeof(b))) > 0) {
            if (l < (int)sizeof(struct eth_hdr)) continue;
            struct eth_hdr *eth = (struct eth_hdr *)b;
            if (bswap16(eth->type) != 0x0800) continue;
            struct ip_hdr *ip = (struct ip_hdr *)(b + sizeof(struct eth_hdr));
            if (ip->protocol != 6) continue;
            int ihl = (ip->ver_ihl & 0x0F) * 4;
            struct tcp_hdr *tcp = (struct tcp_hdr *)((uint8_t *)ip + ihl);
            if (bswap16(tcp->dst_port) != tcp_src_port ||
                bswap16(tcp->src_port) != tcp_dst_port) continue;
            if (ip->src_ip != tcp_dst_ip) continue;
            int thl = ((tcp->offset >> 4) & 0x0F) * 4;
            int ip_len = (int)bswap16(ip->total_len);
            int dlen = ip_len - ihl - thl;
            if (dlen > 0) {
                tls_rx_push((uint8_t *)tcp + thl, dlen);
                tcp_ack = bswap32(tcp->seq) + (uint32_t)dlen;
                send_tcp_seg(0x10, NULL, 0, 0);
                if (got > 0) return got;     /* hand back what we have now */
            }
            if (tcp->flags & 0x01) {          /* FIN -> peer closed */
                tls_rx_closed = 1;
                tcp_state = TCP_CLOSED;
            }
        }
    }
}

static int tls_bio_send(void *ctx, const unsigned char *buf, size_t len) {
    (void)ctx;
    size_t off = 0;
    while (off < len) {
        size_t chunk = len - off;
        if (chunk > 1400) chunk = 1400;       /* fit one TCP segment */
        if (legacy_tcp_send_data(buf + off, (uint16_t)chunk) < 0) return -1;
        off += chunk;
    }
    return (int)off;
}

static int tls_bio_recv(void *ctx, unsigned char *buf, size_t len) {
    (void)ctx;
    int cap = (len > 32767) ? 32767 : (int)len;
    int r = tls_tcp_recv(buf, cap, 2000);
    if (r > 0) return r;
    if (r == -2) return MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY;  /* orderly close */
    return MBEDTLS_ERR_SSL_WANT_READ;                        /* retry */
}

/* Handshake-retransmit timer (drives mbedtls SSL flight retransmission so a
 * dropped handshake segment is recovered without a kernel clock/time.h). */
static void tls_timer_set(void *data, uint32_t int_ms, uint32_t fin_ms) {
    (void)int_ms;
    uint64_t *deadline = (uint64_t *)data;
    if (fin_ms == 0) {
        *deadline = 0;                        /* mbedtls disarm: get returns -1 */
        return;
    }
    *deadline = tls_ms_now() + (uint64_t)fin_ms;
}

static int tls_timer_get(void *data) {
    uint64_t *deadline = (uint64_t *)data;
    if (*deadline == 0) return -1;            /* timer not armed */
    if (tls_ms_now() >= *deadline) return 2;   /* final timeout -> retransmit */
    return 0;                                   /* running */
}

/* ── mbedtls BIO over the tcp.c socket stack (https_get/https_post) ──
 * Unlike the legacy single-connection path above (which races nic_recv
 * against netd), this BIO runs on a tcp.c fd: netd feeds the socket's
 * recv_buf, so handshake/response flights are never stolen. */
static int tls_fd = -1;

static int tls_bio_send_fd(void *ctx, const unsigned char *buf, size_t len) {
    (void)ctx;
    if (tls_fd < 0) return -1;
    size_t off = 0;
    while (off < len) {
        size_t chunk = len - off;
        if (chunk > TCP_MSS) chunk = TCP_MSS;
        if (tcp_send(tls_fd, buf + off, (int)chunk) < 0) return -1;
        off += chunk;
    }
    return (int)off;
}

static int tls_bio_recv_fd(void *ctx, unsigned char *buf, size_t len) {
    (void)ctx;
    if (tls_fd < 0) return MBEDTLS_ERR_SSL_WANT_READ;
    int avail = tcp_poll_recv(tls_fd, 500);
    if (avail < 0) return MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY;
    if (avail == 0) return MBEDTLS_ERR_SSL_WANT_READ;
    int cap = (len > 32767) ? 32767 : (int)len;
    if (avail < cap) cap = avail;
    int r = tcp_recv(tls_fd, buf, cap);
    if (r > 0) return r;
    if (r == 0) return MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY;
    return MBEDTLS_ERR_SSL_WANT_READ;
}

/* dotted-quad literal -> host uint32 matching dns_resolve()'s wire-byte view */
static int tls_parse_ipv4(const char *h, uint32_t *out) {
    const char *s = h; uint32_t p[4]; int cnt = 0;
    while (cnt < 4) {
        if (*s < '0' || *s > '9') return -1;
        unsigned v = 0;
        while (*s >= '0' && *s <= '9') { v = v * 10u + (unsigned)(*s - '0'); s++; }
        if (v > 255) return -1;
        p[cnt++] = v;
        if (cnt < 4) { if (*s != '.') return -1; s++; }
        else         { if (*s != 0) return -1; }
    }
    *out = p[0] | (p[1] << 8) | (p[2] << 16) | (p[3] << 24);
    return 0;
}

int https_get(const char *host, uint16_t port, const char *path,
              void *buf, uint16_t max_len) {
    if (!net_up || !host || !path || !buf) return -1;

    if (adblock_is_enabled() && adblock_check_host(host)) {
        adblock_record_http_block();
        kprintf("AdBlock: blocked HTTPS request to %s\n", host);
        return -1;
    }

    mbedtls_codeos_platform_init();

    uint32_t ip;
    if (tls_parse_ipv4(host, &ip) != 0) {
        if (dns_resolve(host, &ip) < 0) {
            kprintf("https: dns %s FAILED\n", host);
            return -1;
        }
    }

    tls_fd = tcp_connect(ip, port);
    if (tls_fd < 0) {
        kprintf("https: tcp_connect %s:%u FAILED\n", host, port);
        return -1;
    }

#if HTTPS_USE_OPENSSL
    /* Genuine OpenSSL backend: TLS handshake + HTTP over the tcp.c socket,
     * then close and hand the received bytes straight back. */
    {
        int n = ossl_https_get(tls_fd, host, path, buf, max_len);
        tcp_close(tls_fd);
        tls_fd = -1;
        return n;
    }
#endif

    mbedtls_ssl_config conf;
    mbedtls_ssl_context ssl;
    mbedtls_ssl_config_init(&conf);
    mbedtls_ssl_init(&ssl);
    mbedtls_ssl_config_defaults(&conf, MBEDTLS_SSL_IS_CLIENT,
                                MBEDTLS_SSL_TRANSPORT_STREAM,
                                MBEDTLS_SSL_PRESET_DEFAULT);

    mbedtls_entropy_context entropy;
    mbedtls_ctr_drbg_context drbg;
    static const char *pers = "codeos-https/1.0";
    mbedtls_entropy_init(&entropy);
    mbedtls_ctr_drbg_init(&drbg);
    if (mbedtls_ctr_drbg_seed(&drbg, mbedtls_entropy_func, &entropy,
                              (const unsigned char *)pers, strlen(pers)) != 0) {
        kprintf("https: CTR-DRBG seed FAILED\n");
        goto fail;
    }
    mbedtls_ssl_conf_rng(&conf, mbedtls_ctr_drbg_random, &drbg);

    if (!https_insecure()) {
        mbedtls_x509_crt *ca = https_ca_get();
        if (!ca) {
            kprintf("https: no trust store for %s (try tlsinsecure 1 or catrust)\n", host);
            goto fail;
        }
        mbedtls_ssl_conf_ca_chain(&conf, ca, NULL);
        mbedtls_ssl_conf_authmode(&conf, MBEDTLS_SSL_VERIFY_REQUIRED);
        mbedtls_ssl_conf_verify(&conf, codeos_verify_cb, (void *)host);
    } else {
        mbedtls_ssl_conf_authmode(&conf, MBEDTLS_SSL_VERIFY_NONE);
    }

    uint64_t timer_deadline = 0;
    mbedtls_ssl_set_timer_cb(&ssl, &timer_deadline, tls_timer_set, tls_timer_get);

    if (mbedtls_ssl_setup(&ssl, &conf) != 0) {
        kprintf("https: ssl_setup FAILED\n");
        goto fail;
    }
    mbedtls_ssl_set_bio(&ssl, NULL, tls_bio_send_fd, tls_bio_recv_fd, NULL);
    mbedtls_ssl_set_hostname(&ssl, host);

    int ret;
    while ((ret = mbedtls_ssl_handshake(&ssl)) != 0) {
        if (ret != MBEDTLS_ERR_SSL_WANT_READ && ret != MBEDTLS_ERR_SSL_WANT_WRITE) {
            kprintf("https: handshake FAILED (0x%04x) now=%llu dl=%llu fd=%d\n",
                    (unsigned)(-ret), tls_ms_now(), timer_deadline, tls_fd);
            goto fail;
        }
        /* WANT_READ/WANT_WRITE: timer cb drives flight retransmit; just retry. */
    }

    /* build GET */
    char req[512]; int pos = 0;
    const char *parts[] = {"GET ", path, " HTTP/1.1\r\nHost: ", host,
                           "\r\nUser-Agent: codeos-kernel/1.0\r\nConnection: close\r\nAccept: */*\r\nAccept-Encoding: identity\r\n\r\n", NULL};
    for (int i = 0; parts[i]; i++) {
        const char *s = parts[i];
        while (*s && pos < (int)sizeof(req) - 1) req[pos++] = *s++;
    }
    req[pos] = 0;

    size_t woff = 0;
    while (woff < (size_t)pos) {
        int n = mbedtls_ssl_write(&ssl, (const unsigned char *)req + woff, pos - (int)woff);
        if (n == MBEDTLS_ERR_SSL_WANT_READ || n == MBEDTLS_ERR_SSL_WANT_WRITE) continue;
        if (n < 0) { kprintf("https: write FAILED (0x%04x)\n", (unsigned)(-n)); goto fail; }
        woff += (size_t)n;
    }

    int total = 0;
    uint64_t stall0 = tls_ms_now();
    struct http_filter hf;
    memset(&hf, 0, sizeof(hf));
    hf.state = HTTP_FILTER_HDR;
    for (;;) {
        uint8_t tmp[TCP_MSS];
        int n = mbedtls_ssl_read(&ssl, tmp, sizeof(tmp));
        if (n > 0) {
            total = http_filter_feed(&hf, tmp, n, (uint8_t *)buf, max_len, total);
            stall0 = tls_ms_now();
            if (total >= (int)max_len) break;
        }
        else if (n == 0) break;
        else if (n == MBEDTLS_ERR_SSL_WANT_READ || n == MBEDTLS_ERR_SSL_WANT_WRITE) {
            if (tls_ms_now() - stall0 > 15000u) { kprintf("https: read timed out\n"); break; }
            continue;
        }
        else if (n == MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY) break;
        else { kprintf("https: read FAILED (0x%04x)\n", (unsigned)(-n)); break; }
    }

    mbedtls_ssl_close_notify(&ssl);
    mbedtls_ssl_free(&ssl);
    mbedtls_ssl_config_free(&conf);
    mbedtls_ctr_drbg_free(&drbg);
    mbedtls_entropy_free(&entropy);
    tcp_close(tls_fd);
    tls_fd = -1;
    return total;

fail:
    mbedtls_ssl_free(&ssl);
    mbedtls_ssl_config_free(&conf);
    mbedtls_ctr_drbg_free(&drbg);
    mbedtls_entropy_free(&entropy);
    tcp_close(tls_fd);
    tls_fd = -1;
    return -1;
}

/* ── HTTPS POST (mirror of https_get) ─────────────────────────── */
int https_post(const char *host, uint16_t port, const char *path,
               const void *body, uint16_t body_len,
               void *buf, uint16_t max_len) {
    if (!net_up || !host || !path || !buf) return -1;

    if (adblock_is_enabled() && adblock_check_host(host)) {
        adblock_record_http_block();
        kprintf("AdBlock: blocked HTTPS request to %s\n", host);
        return -1;
    }

    mbedtls_codeos_platform_init();

    uint32_t ip;
    if (tls_parse_ipv4(host, &ip) != 0) {
        if (dns_resolve(host, &ip) < 0) {
            kprintf("https: dns %s FAILED\n", host);
            return -1;
        }
    }

    tls_fd = tcp_connect(ip, port);
    if (tls_fd < 0) {
        kprintf("https: tcp_connect %s:%u FAILED\n", host, port);
        return -1;
    }

    mbedtls_ssl_config conf;
    mbedtls_ssl_context ssl;
    mbedtls_ssl_config_init(&conf);
    mbedtls_ssl_init(&ssl);
    mbedtls_ssl_config_defaults(&conf, MBEDTLS_SSL_IS_CLIENT,
                                MBEDTLS_SSL_TRANSPORT_STREAM,
                                MBEDTLS_SSL_PRESET_DEFAULT);

    mbedtls_entropy_context entropy;
    mbedtls_ctr_drbg_context drbg;
    static const char *pers = "codeos-https/1.0";
    mbedtls_entropy_init(&entropy);
    mbedtls_ctr_drbg_init(&drbg);
    if (mbedtls_ctr_drbg_seed(&drbg, mbedtls_entropy_func, &entropy,
                              (const unsigned char *)pers, strlen(pers)) != 0) {
        kprintf("https: CTR-DRBG seed FAILED\n");
        goto fail;
    }
    mbedtls_ssl_conf_rng(&conf, mbedtls_ctr_drbg_random, &drbg);

    if (!https_insecure()) {
        mbedtls_x509_crt *ca = https_ca_get();
        if (!ca) {
            kprintf("https: no trust store for %s (try tlsinsecure 1 or catrust)\n", host);
            goto fail;
        }
        mbedtls_ssl_conf_ca_chain(&conf, ca, NULL);
        mbedtls_ssl_conf_authmode(&conf, MBEDTLS_SSL_VERIFY_REQUIRED);
        mbedtls_ssl_conf_verify(&conf, codeos_verify_cb, (void *)host);
    } else {
        mbedtls_ssl_conf_authmode(&conf, MBEDTLS_SSL_VERIFY_NONE);
    }

    uint64_t timer_deadline = 0;
    mbedtls_ssl_set_timer_cb(&ssl, &timer_deadline, tls_timer_set, tls_timer_get);

    if (mbedtls_ssl_setup(&ssl, &conf) != 0) {
        kprintf("https: ssl_setup FAILED\n");
        goto fail;
    }
    mbedtls_ssl_set_bio(&ssl, NULL, tls_bio_send_fd, tls_bio_recv_fd, NULL);
    mbedtls_ssl_set_hostname(&ssl, host);

    int ret;
    while ((ret = mbedtls_ssl_handshake(&ssl)) != 0) {
        if (ret != MBEDTLS_ERR_SSL_WANT_READ && ret != MBEDTLS_ERR_SSL_WANT_WRITE) {
            kprintf("https: handshake FAILED (0x%04x)\n", (unsigned)(-ret));
            goto fail;
        }
    }

    /* build POST request + body */
    char req[512]; int pos = 0;
    char hdr[96];
    sprintf(hdr, "%u", (unsigned)body_len);
    const char *parts[] = {"POST ", path, " HTTP/1.1\r\nHost: ", host,
                           "\r\nContent-Length: ", hdr,
                           "\r\nContent-Type: application/octet-stream\r\n"
                           "User-Agent: codeos-kernel/1.0\r\nConnection: close\r\nAccept: */*\r\nAccept-Encoding: identity\r\n\r\n", NULL};
    for (int i = 0; parts[i]; i++) {
        const char *s = parts[i];
        while (*s && pos < (int)sizeof(req) - 1) req[pos++] = *s++;
    }
    req[pos] = 0;

    size_t woff = 0;
    while (woff < (size_t)pos) {
        int n = mbedtls_ssl_write(&ssl, (const unsigned char *)req + woff, pos - (int)woff);
        if (n == MBEDTLS_ERR_SSL_WANT_READ || n == MBEDTLS_ERR_SSL_WANT_WRITE) continue;
        if (n < 0) { kprintf("https: write FAILED (0x%04x)\n", (unsigned)(-n)); goto fail; }
        woff += (size_t)n;
    }
    if (body && body_len) {
        woff = 0;
        while (woff < body_len) {
            int n = mbedtls_ssl_write(&ssl, (const unsigned char *)body + woff, body_len - (uint16_t)woff);
            if (n == MBEDTLS_ERR_SSL_WANT_READ || n == MBEDTLS_ERR_SSL_WANT_WRITE) continue;
            if (n < 0) { kprintf("https: write body FAILED (0x%04x)\n", (unsigned)(-n)); goto fail; }
            woff += (size_t)n;
        }
    }

    int total = 0;
    uint64_t stall0 = tls_ms_now();
    struct http_filter hf;
    memset(&hf, 0, sizeof(hf));
    hf.state = HTTP_FILTER_HDR;
    for (;;) {
        uint8_t tmp[TCP_MSS];
        int n = mbedtls_ssl_read(&ssl, tmp, sizeof(tmp));
        if (n > 0) {
            total = http_filter_feed(&hf, tmp, n, (uint8_t *)buf, max_len, total);
            stall0 = tls_ms_now();
            if (total >= (int)max_len) break;
        }
        else if (n == 0) break;
        else if (n == MBEDTLS_ERR_SSL_WANT_READ || n == MBEDTLS_ERR_SSL_WANT_WRITE) {
            if (tls_ms_now() - stall0 > 15000u) { kprintf("https: read timed out\n"); break; }
            continue;
        }
        else if (n == MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY) break;
        else { kprintf("https: read FAILED (0x%04x)\n", (unsigned)(-n)); break; }
    }

    mbedtls_ssl_close_notify(&ssl);
    mbedtls_ssl_free(&ssl);
    mbedtls_ssl_config_free(&conf);
    mbedtls_ctr_drbg_free(&drbg);
    mbedtls_entropy_free(&entropy);
    tcp_close(tls_fd);
    tls_fd = -1;
    return total;

fail:
    mbedtls_ssl_free(&ssl);
    mbedtls_ssl_config_free(&conf);
    mbedtls_ctr_drbg_free(&drbg);
    mbedtls_entropy_free(&entropy);
    tcp_close(tls_fd);
    tls_fd = -1;
    return -1;
}

/* ── WebSocket client over the single-connection stack ──────────
 * The TCP model is a single outstanding connection with a shared RX
 * ring (tls_rx_*). We reuse that same transport for WS: `use_tls`
 * selects the mbedtls BIO path, otherwise we talk raw TCP through
 * legacy_tcp_send_data()/tls_tcp_recv(). Frames are built/parsed by ws.c. */

static int ws_raw_send(void *ctx, const void *data, int len) {
    (void)ctx;
    size_t off = 0;
    while (off < (size_t)len) {
        size_t chunk = (size_t)len - off;
        if (chunk > 1400) chunk = 1400;
        if (legacy_tcp_send_data((const uint8_t *)data + off, (uint16_t)chunk) < 0) return -1;
        off += chunk;
    }
    return (int)off;
}

static int ws_raw_recv(void *ctx, void *buf, int max_len, int timeout_ms) {
    (void)ctx;
    return tls_tcp_recv((uint8_t *)buf, max_len, timeout_ms);
}

/* persistent TLS state for an open WSS session (single-connection model) */
static struct {
    int active;
    mbedtls_ssl_context ssl;
    mbedtls_ssl_config conf;
    mbedtls_ctr_drbg_context drbg;
    mbedtls_entropy_context entropy;
} ws_tls;

static int ws_tls_send(void *ctx, const void *data, int len) {
    (void)ctx;
    if (!ws_tls.active) return -1;
    const unsigned char *p = (const unsigned char *)data;
    int sent = 0;
    while (sent < len) {
        int n = mbedtls_ssl_write(&ws_tls.ssl, p + sent, len - sent);
        if (n == MBEDTLS_ERR_SSL_WANT_READ || n == MBEDTLS_ERR_SSL_WANT_WRITE) continue;
        if (n < 0) return -1;
        sent += n;
    }
    return sent;
}

static int ws_tls_recv(void *ctx, void *buf, int max_len, int timeout_ms) {
    (void)ctx;
    (void)timeout_ms;
    if (!ws_tls.active) return -1;
    for (;;) {
        int n = mbedtls_ssl_read(&ws_tls.ssl, (unsigned char *)buf, (size_t)max_len);
        if (n > 0) return n;
        if (n == MBEDTLS_ERR_SSL_WANT_READ || n == MBEDTLS_ERR_SSL_WANT_WRITE) continue;
        if (n == MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY) return 0;
        return -1;
    }
}

static int ws_tls_teardown(void) {
    if (!ws_tls.active) return 0;
    mbedtls_ssl_close_notify(&ws_tls.ssl);
    mbedtls_ssl_free(&ws_tls.ssl);
    mbedtls_ssl_config_free(&ws_tls.conf);
    mbedtls_ctr_drbg_free(&ws_tls.drbg);
    mbedtls_entropy_free(&ws_tls.entropy);
    ws_tls.active = 0;
    return 0;
}

int ws_connect(ws_client_t *w, const char *host, uint16_t port, int use_tls, const char *path) {
    if (!w || !host || !path || !net_up) return -1;
    memset(w, 0, sizeof(*w));

    uint32_t ip;
    if (tls_parse_ipv4(host, &ip) != 0) {
        if (dns_resolve(host, &ip) < 0) {
            kprintf("ws: dns %s FAILED\n", host);
            return -1;
        }
    }

    tls_rx_reset();
    if (legacy_tcp_connect(ip, port) < 0) {
        kprintf("ws: tcp_connect %s:%u FAILED\n", host, port);
        return -1;
    }

    if (use_tls) {
        mbedtls_codeos_platform_init();
        mbedtls_ssl_config_init(&ws_tls.conf);
        mbedtls_ssl_init(&ws_tls.ssl);
        mbedtls_ssl_config_defaults(&ws_tls.conf, MBEDTLS_SSL_IS_CLIENT,
                                    MBEDTLS_SSL_TRANSPORT_STREAM,
                                    MBEDTLS_SSL_PRESET_DEFAULT);
        mbedtls_ssl_conf_authmode(&ws_tls.conf, MBEDTLS_SSL_VERIFY_NONE);

        mbedtls_entropy_init(&ws_tls.entropy);
        mbedtls_ctr_drbg_init(&ws_tls.drbg);
        static const char *pers = "codeos-ws/1.0";
        if (mbedtls_ctr_drbg_seed(&ws_tls.drbg, mbedtls_entropy_func, &ws_tls.entropy,
                                  (const unsigned char *)pers, strlen(pers)) != 0) {
            kprintf("ws: CTR-DRBG seed FAILED\n");
            legacy_tcp_close_conn();
            return -1;
        }
        mbedtls_ssl_conf_rng(&ws_tls.conf, mbedtls_ctr_drbg_random, &ws_tls.drbg);

        uint64_t timer_deadline = 0;
        mbedtls_ssl_set_timer_cb(&ws_tls.ssl, &timer_deadline, tls_timer_set, tls_timer_get);

        if (mbedtls_ssl_setup(&ws_tls.ssl, &ws_tls.conf) != 0) {
            kprintf("ws: ssl_setup FAILED\n");
            legacy_tcp_close_conn();
            return -1;
        }
        mbedtls_ssl_set_bio(&ws_tls.ssl, NULL, tls_bio_send, tls_bio_recv, NULL);
        mbedtls_ssl_set_hostname(&ws_tls.ssl, host);

        int ret;
        while ((ret = mbedtls_ssl_handshake(&ws_tls.ssl)) != 0) {
            if (ret != MBEDTLS_ERR_SSL_WANT_READ && ret != MBEDTLS_ERR_SSL_WANT_WRITE) {
                kprintf("ws: handshake FAILED (0x%04x)\n", (unsigned)(-ret));
                legacy_tcp_close_conn();
                return -1;
            }
        }
        ws_tls.active = 1;
        w->use_tls = 1;
        w->send = ws_tls_send;
        w->recv = ws_tls_recv;
    } else {
        w->use_tls = 0;
        w->send = ws_raw_send;
        w->recv = ws_raw_recv;
    }

    w->ctx = NULL;

    if (ws_client_handshake(w, host, path) < 0) {
        kprintf("ws: upgrade handshake FAILED\n");
        ws_tls_teardown();
        legacy_tcp_close_conn();
        tls_rx_closed = 1;
        w->state = WS_CONN_NONE;
        return -1;
    }
    return 0;
}int ws_send_text(ws_client_t *w, const char *text) {
    if (!w || w->state != WS_CONN_OPEN || !w->send) return -1;
    size_t len = text ? strlen(text) : 0;
    unsigned char frame[WS_MAX_FRAME];
    int n = ws_make_frame(frame, sizeof(frame), text, len, WS_OP_TEXT, 1, 1);
    if (n < 0) return -1;
    return w->send(w->ctx, frame, n);
}

int ws_send_binary(ws_client_t *w, const void *data, int len) {
    if (!w || w->state != WS_CONN_OPEN || !w->send) return -1;
    unsigned char frame[WS_MAX_FRAME];
    int n = ws_make_frame(frame, sizeof(frame), data, (size_t)len, WS_OP_BINARY, 1, 1);
    if (n < 0) return -1;
    return w->send(w->ctx, frame, n);
}

int ws_send_ping(ws_client_t *w, const void *data, int len) {
    if (!w || w->state != WS_CONN_OPEN || !w->send) return -1;
    unsigned char frame[WS_MAX_FRAME];
    int n = ws_make_frame(frame, sizeof(frame), data, (size_t)len, WS_OP_PING, 1, 1);
    if (n < 0) return -1;
    return w->send(w->ctx, frame, n);
}

int ws_recv(ws_client_t *w, void *buf, int max_len, int timeout_ms) {
    if (!w || w->state != WS_CONN_OPEN || !w->recv) return -1;

    unsigned char raw[WS_MAX_FRAME];
    for (int attempt = 0; attempt < 3; attempt++) {
        int r = w->recv(w->ctx, raw, (int)sizeof(raw), timeout_ms);
        if (r <= 0) return r;

        ws_frame_t frame;
        int consumed = ws_parse_frame(raw, (size_t)r, &frame);
        if (consumed <= 0) {
            kprintf("ws: short/bad frame (%d bytes)\n", r);
            continue;
        }

        if (frame.opcode == WS_OP_CLOSE) {
            w->state = WS_CONN_NONE;
            return 0;
        }
        if (frame.opcode == WS_OP_PING) {
            unsigned char pong[WS_MAX_FRAME];
            int n = ws_make_frame(pong, sizeof(pong), frame.payload, frame.payload_len, WS_OP_PONG, 1, 1);
            if (n > 0) w->send(w->ctx, pong, n);
            continue;
        }
        if (frame.opcode == WS_OP_PONG) {
            continue;
        }

        /* unmask server->client frames (client->server are unmasked) */
        unsigned char *src = (unsigned char *)frame.payload;
        if (frame.masked) {
            for (size_t i = 0; i < frame.payload_len; i++)
                src[i] ^= frame.mask[i % 4];
        }
        size_t copy = frame.payload_len;
        if (copy > (size_t)max_len) copy = (size_t)max_len;
        memcpy(buf, src, copy);
        return (int)copy;
    }
    return -1;
}

void ws_close(ws_client_t *w) {
    if (!w || w->state != WS_CONN_OPEN) return;
    unsigned char frame[16];
    int n = ws_make_frame(frame, sizeof(frame), NULL, 0, WS_OP_CLOSE, 1, 1);
    if (n > 0 && w->send) w->send(w->ctx, frame, n);
    w->state = WS_CONN_NONE;
    ws_tls_teardown();
    legacy_tcp_close_conn();
    tls_rx_closed = 1;
}

uint32_t net_get_ip(void) { return our_ip; }
uint32_t net_get_gateway(void) { return gateway_ip; }
uint32_t net_get_dns(void) { return dns_ip; }
void net_get_mac(uint8_t *mac) { if (mac) memcpy(mac, our_mac, 6); }


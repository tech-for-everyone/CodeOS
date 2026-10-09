#include "arp.h"
#include "ethernet.h"
#include "net_internal.h"
#include "kprintf.h"
#include "string.h"
#include "spinlock.h"
#include "timer.h"

static arp_entry_t cache[ARP_CACHE_SIZE];
static spinlock_t arp_lock = SPINLOCK_INIT;
static int proxy_enabled;
static uint32_t proxy_ip;

int arp_resolve(uint32_t ip, uint8_t *mac) {
    if (!ip || !mac) return -1;
    spin_lock(&arp_lock);
    uint64_t now = timer_get_milliseconds();
    for (int i = 0; i < ARP_CACHE_SIZE; i++) {
        if (cache[i].valid && cache[i].ip == ip) {
            if (now - cache[i].created > ARP_CACHE_TTL) {
                cache[i].valid = 0;
                break;
            }
            memcpy(mac, cache[i].mac, 6);
            cache[i].last_access = now;
            spin_unlock(&arp_lock);
            return 0;
        }
    }
    spin_unlock(&arp_lock);
    arp_send_request(ip);
    return -1;
}

int arp_init(void) {
    memset(cache, 0, sizeof(cache));
    proxy_enabled = 0;
    proxy_ip = 0;
    kprintf("arp: init %d slots\n", ARP_CACHE_SIZE);
    return 0;
}

void arp_update(uint32_t ip, const uint8_t *mac) {
    if (!ip || !mac) return;
    spin_lock(&arp_lock);
    int slot = -1;
    int oldest = 0;
    uint64_t oldest_t = 0xFFFFFFFFFFFFFFFFULL;
    uint64_t now = timer_get_milliseconds();
    for (int i = 0; i < ARP_CACHE_SIZE; i++) {
        if (!cache[i].valid) { slot = i; break; }
        if (cache[i].ip == ip) { slot = i; break; }
        if (!cache[i].valid || (now - cache[i].created > ARP_CACHE_TTL)) {
            slot = i; break;
        }
        if (cache[i].last_access < oldest_t) { oldest_t = cache[i].last_access; oldest = i; }
    }
    if (slot < 0) slot = oldest;
    cache[slot].ip = ip;
    memcpy(cache[slot].mac, mac, 6);
    cache[slot].valid = 1;
    cache[slot].created = timer_get_milliseconds();
    cache[slot].last_access = cache[slot].created;
    spin_unlock(&arp_lock);
}

void arp_remove(uint32_t ip) {
    spin_lock(&arp_lock);
    for (int i = 0; i < ARP_CACHE_SIZE; i++)
        if (cache[i].valid && cache[i].ip == ip) { cache[i].valid = 0; break; }
    spin_unlock(&arp_lock);
}

void arp_purge_expired(void) {
    uint64_t now = timer_get_milliseconds();
    spin_lock(&arp_lock);
    for (int i = 0; i < ARP_CACHE_SIZE; i++)
        if (cache[i].valid && (now - cache[i].created > ARP_CACHE_TTL)) cache[i].valid = 0;
    spin_unlock(&arp_lock);
}

void arp_send_request(uint32_t target_ip) {
    uint8_t sha[6];
    eth_get_addr(sha);
    arp_packet_t arp;
    memset(&arp, 0, sizeof(arp));
    arp.htype = __builtin_bswap16(ARP_HW_ETHER);
    arp.ptype = __builtin_bswap16(ETH_P_IP);
    arp.hlen = 6; arp.plen = 4;
    arp.oper = __builtin_bswap16(ARP_OP_REQUEST);
    memcpy(arp.sha, sha, 6);
    arp.spa = net_get_ip();
    memset(arp.tha, 0, 6);
    arp.tpa = target_ip;
    uint8_t bcast[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    eth_send(bcast, ETH_P_ARP, &arp, sizeof(arp));
}

void arp_send_reply(uint32_t target_ip, const uint8_t *target_mac) {
    uint8_t sha[6];
    eth_get_addr(sha);
    arp_packet_t arp;
    memset(&arp, 0, sizeof(arp));
    arp.htype = __builtin_bswap16(ARP_HW_ETHER);
    arp.ptype = __builtin_bswap16(ETH_P_IP);
    arp.hlen = 6; arp.plen = 4;
    arp.oper = __builtin_bswap16(ARP_OP_REPLY);
    memcpy(arp.sha, sha, 6);
    arp.spa = net_get_ip();
    memcpy(arp.tha, target_mac, 6);
    arp.tpa = target_ip;
    eth_send(target_mac, ETH_P_ARP, &arp, sizeof(arp));
}

void arp_send_gratuitous(uint32_t ip) {
    uint8_t sha[6];
    eth_get_addr(sha);
    arp_packet_t arp;
    memset(&arp, 0, sizeof(arp));
    arp.htype = __builtin_bswap16(ARP_HW_ETHER);
    arp.ptype = __builtin_bswap16(ETH_P_IP);
    arp.hlen = 6; arp.plen = 4;
    arp.oper = __builtin_bswap16(ARP_OP_REQUEST);
    memcpy(arp.sha, sha, 6);
    arp.spa = ip;
    memcpy(arp.tha, sha, 6);
    arp.tpa = ip;
    uint8_t bcast[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    eth_send(bcast, ETH_P_ARP, &arp, sizeof(arp));
}

void arp_handle_packet(const uint8_t *src_mac, const arp_packet_t *pkt, int len) {
    if (!pkt || !src_mac || len < (int)sizeof(arp_packet_t)) return;
    if (pkt->hlen != 6 || pkt->plen != 4 ||
        __builtin_bswap16(pkt->htype) != ARP_HW_ETHER ||
        __builtin_bswap16(pkt->ptype) != ETH_P_IP) return;
    uint16_t oper = __builtin_bswap16(pkt->oper);
    if (oper == ARP_OP_REQUEST) {
        if (pkt->tpa == net_get_ip()) {
            arp_update(pkt->spa, pkt->sha);
            arp_send_reply(pkt->spa, pkt->sha);
        }
        if (proxy_enabled && pkt->tpa == proxy_ip) {
            arp_send_reply(pkt->spa, src_mac);
        }
    } else if (oper == ARP_OP_REPLY) {
        arp_update(pkt->spa, pkt->sha);
    }
}

void arp_set_proxy(int enabled, uint32_t pi) {
    proxy_enabled = enabled;
    proxy_ip = pi;
}

void arp_get_table(arp_entry_t *table, int *count) {
    if (!table || !count) return;
    int n = 0;
    spin_lock(&arp_lock);
    for (int i = 0; i < ARP_CACHE_SIZE && n < *count; i++)
        if (cache[i].valid) table[n++] = cache[i];
    spin_unlock(&arp_lock);
    *count = n;
}

static inline void ip_to_str(uint32_t ip, char *buf) {
    buf[0] = (char)(ip & 0xFF);
    buf[1] = (char)((ip >> 8) & 0xFF);
    buf[2] = (char)((ip >> 16) & 0xFF);
    buf[3] = (char)((ip >> 24) & 0xFF);
    buf[4] = 0;
}

void arp_dump(void) {
    spin_lock(&arp_lock);
    kprintf("ARP Cache:\n");
    for (int i = 0; i < ARP_CACHE_SIZE; i++) {
        if (!cache[i].valid) continue;
        char ipstr[16];
        ip_to_str(cache[i].ip, ipstr);
        kprintf("  %d.%d.%d.%d -> %02x:%02x:%02x:%02x:%02x:%02x\n",
                cache[i].ip & 0xFF, (cache[i].ip >> 8) & 0xFF,
                (cache[i].ip >> 16) & 0xFF, (cache[i].ip >> 24) & 0xFF,
                cache[i].mac[0], cache[i].mac[1], cache[i].mac[2],
                cache[i].mac[3], cache[i].mac[4], cache[i].mac[5]);
    }
    spin_unlock(&arp_lock);
}

int arp_cache_count(void) {
    int n = 0;
    spin_lock(&arp_lock);
    for (int i = 0; i < ARP_CACHE_SIZE; i++) if (cache[i].valid) n++;
    spin_unlock(&arp_lock);
    return n;
}

int arp_cache_has_ip(uint32_t ip) {
    spin_lock(&arp_lock);
    for (int i = 0; i < ARP_CACHE_SIZE; i++)
        if (cache[i].valid && cache[i].ip == ip) { spin_unlock(&arp_lock); return 1; }
    spin_unlock(&arp_lock);
    return 0;
}

void arp_timer_tick(void) { arp_purge_expired(); }

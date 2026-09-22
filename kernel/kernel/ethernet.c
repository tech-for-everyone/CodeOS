#include "ethernet.h"
#include "nic.h"
#include "kprintf.h"
#include "string.h"
#include "spinlock.h"
#include "mm.h"

static uint8_t  eth_mac[ETH_ALEN];
static int      eth_ready_flag;
static int      eth_mtu = ETH_MTU;
static eth_stats_t eth_stats;
static spinlock_t  eth_lock = SPINLOCK_INIT;
static eth_hook_t *hooks;
static spinlock_t  hook_lock = SPINLOCK_INIT;

int eth_init(void) {
    memset(&eth_stats, 0, sizeof(eth_stats));
    hooks = 0;
    eth_ready_flag = 0;
    eth_mtu = ETH_MTU;
    nic_get_mac(eth_mac);
    eth_ready_flag = 1;
    kprintf("eth: init mac=%02x:%02x:%02x:%02x:%02x:%02x mtu=%d\n",
            eth_mac[0], eth_mac[1], eth_mac[2],
            eth_mac[3], eth_mac[4], eth_mac[5], eth_mtu);
    return 0;
}

int eth_send(const uint8_t *dst, uint16_t type, const void *payload, uint16_t len) {
    if (!eth_ready_flag || !dst || !payload) return -1;
    int total = ETH_HLEN + (int)len;
    if (len > (uint16_t)eth_mtu || total > ETH_FRAME_MAX) {
        spin_lock(&eth_lock); eth_stats.tx_dropped++; eth_stats.tx_errors++; spin_unlock(&eth_lock);
        return -1;
    }
    uint8_t frame[ETH_FRAME_MAX];
    eth_frame_t *eth = (eth_frame_t *)frame;
    memcpy(eth->dst, dst, ETH_ALEN);
    memcpy(eth->src, eth_mac, ETH_ALEN);
    eth->type = __builtin_bswap16(type);
    memcpy(frame + ETH_HLEN, payload, len);
    if (total < ETH_FRAME_MIN) { memset(frame + total, 0, ETH_FRAME_MIN - total); total = ETH_FRAME_MIN; }
    spin_lock(&eth_lock);
    eth_stats.tx_packets++;
    eth_stats.tx_bytes += (uint64_t)total;
    if (dst[0] & 1) eth_stats.tx_broadcast++;
    if ((dst[0] & 1) && !(dst[0] == 0xFF && dst[1] == 0xFF &&
                          dst[2] == 0xFF && dst[3] == 0xFF &&
                          dst[4] == 0xFF && dst[5] == 0xFF))
        eth_stats.tx_multicast++;
    spin_unlock(&eth_lock);
    return nic_send(frame, total);
}

int eth_send_raw(const uint8_t *frame, int len) {
    if (!eth_ready_flag || !frame || len < ETH_FRAME_MIN || len > ETH_FRAME_MAX) {
        spin_lock(&eth_lock); eth_stats.tx_dropped++; eth_stats.tx_errors++; spin_unlock(&eth_lock);
        return -1;
    }
    spin_lock(&eth_lock);
    eth_stats.tx_packets++;
    eth_stats.tx_bytes += (uint64_t)len;
    spin_unlock(&eth_lock);
    return nic_send(frame, len);
}

int eth_send_vlan(uint16_t vlan_id, const uint8_t *dst, uint16_t type, const void *payload, uint16_t len) {
    if (!eth_ready_flag || !dst || !payload) return -1;
    int total = sizeof(eth_vlan_frame_t) + len;
    if (len > (uint16_t)(eth_mtu - 4) || total > ETH_FRAME_MAX) {
        spin_lock(&eth_lock); eth_stats.tx_dropped++; eth_stats.tx_errors++; spin_unlock(&eth_lock);
        return -1;
    }
    uint8_t frame[ETH_FRAME_MAX];
    eth_vlan_frame_t *vf = (eth_vlan_frame_t *)frame;
    memcpy(vf->dst, dst, ETH_ALEN);
    memcpy(vf->src, eth_mac, ETH_ALEN);
    vf->tpid = __builtin_bswap16(ETH_P_8021Q);
    vf->tci = __builtin_bswap16(vlan_id & 0x0FFF);
    vf->inner_type = __builtin_bswap16(type);
    memcpy(frame + sizeof(eth_vlan_frame_t), payload, len);
    if (total < ETH_FRAME_MIN) { memset(frame + total, 0, ETH_FRAME_MIN - total); total = ETH_FRAME_MIN; }
    spin_lock(&eth_lock);
    eth_stats.tx_packets++;
    eth_stats.tx_bytes += (uint64_t)total;
    spin_unlock(&eth_lock);
    return nic_send(frame, total);
}

void eth_recv(const uint8_t *frame, int len) {
    if (!frame || len < ETH_HLEN || len > ETH_FRAME_MAX) {
        spin_lock(&eth_lock);
        eth_stats.rx_dropped++;
        spin_unlock(&eth_lock);
        return;
    }
    spin_lock(&eth_lock);
    eth_stats.rx_packets++;
    eth_stats.rx_bytes += (uint64_t)len;
    const eth_frame_t *eth = (const eth_frame_t *)frame;
    if (eth->dst[0] & 1) eth_stats.rx_broadcast++;
    if ((eth->dst[0] & 1) && !(eth->dst[0] == 0xFF && eth->dst[1] == 0xFF &&
                               eth->dst[2] == 0xFF && eth->dst[3] == 0xFF &&
                               eth->dst[4] == 0xFF && eth->dst[5] == 0xFF))
        eth_stats.rx_multicast++;
    spin_unlock(&eth_lock);
    uint16_t type = __builtin_bswap16(eth->type);
    int paylen = len - ETH_HLEN;
    const uint8_t *payload = frame + ETH_HLEN;
    if (type == ETH_P_8021Q && len >= (int)sizeof(eth_vlan_frame_t)) {
        const eth_vlan_frame_t *vf = (const eth_vlan_frame_t *)frame;
        type = __builtin_bswap16(vf->inner_type);
        payload = frame + sizeof(eth_vlan_frame_t);
        paylen = len - sizeof(eth_vlan_frame_t);
    }
    if (paylen < 0) return;
    struct { uint16_t type; eth_handler_t handler; void *ctx; } calls[32];
    int call_count = 0;
    spin_lock(&hook_lock);
    for (eth_hook_t *h = hooks; h && call_count < 32; h = h->next)
        if (h->type == type || h->type == ETH_P_ALL) {
            calls[call_count].type = h->type;
            calls[call_count].handler = h->handler;
            calls[call_count].ctx = h->ctx;
            call_count++;
        }
    spin_unlock(&hook_lock);
    for (int i = 0; i < call_count; i++)
        calls[i].handler(type, eth->src, payload, paylen, calls[i].ctx);
}

int eth_register_handler(uint16_t type, eth_handler_t handler, void *ctx) {
    if (!handler) return -1;
    eth_hook_t *h = (eth_hook_t *)malloc(sizeof(eth_hook_t));
    if (!h) return -1;
    h->type = type;
    h->handler = handler;
    h->ctx = ctx;
    spin_lock(&hook_lock);
    h->next = hooks;
    hooks = h;
    spin_unlock(&hook_lock);
    return 0;
}

int eth_unregister_handler(uint16_t type) {
    spin_lock(&hook_lock);
    eth_hook_t **pp = &hooks;
    while (*pp) {
        if ((*pp)->type == type) { eth_hook_t *r = *pp; *pp = r->next; free(r); spin_unlock(&hook_lock); return 0; }
        pp = &(*pp)->next;
    }
    spin_unlock(&hook_lock);
    return -1;
}

void eth_get_stats(eth_stats_t *s) { if (s) { spin_lock(&eth_lock); *s = eth_stats; spin_unlock(&eth_lock); } }
void eth_reset_stats(void) { spin_lock(&eth_lock); memset(&eth_stats, 0, sizeof(eth_stats)); spin_unlock(&eth_lock); }
void eth_get_addr(uint8_t *mac) { if (mac) memcpy(mac, eth_mac, ETH_ALEN); }
void eth_set_addr(const uint8_t *mac) { if (mac) memcpy(eth_mac, mac, ETH_ALEN); }
int  eth_ready(void) { return eth_ready_flag; }

int  eth_set_mtu(int mtu) {
    if (mtu >= 576 && mtu <= ETH_MTU) { eth_mtu = mtu; return 0; }
    return -1;
}

void eth_dump_frame(const uint8_t *frame, int len) {
    if (!frame || len < ETH_HLEN) return;
    const eth_frame_t *eth = (const eth_frame_t *)frame;
    kprintf("eth: %02x:%02x:%02x:%02x:%02x:%02x -> %02x:%02x:%02x:%02x:%02x:%02x type=0x%04x len=%d\n",
            eth->src[0],eth->src[1],eth->src[2],eth->src[3],eth->src[4],eth->src[5],
            eth->dst[0],eth->dst[1],eth->dst[2],eth->dst[3],eth->dst[4],eth->dst[5],
            __builtin_bswap16(eth->type), len);
}

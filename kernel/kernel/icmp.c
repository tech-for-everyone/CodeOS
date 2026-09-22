#include "icmp.h"
#include "ip.h"
#include "ethernet.h"
#include "kprintf.h"
#include "string.h"
#include "spinlock.h"
#include "timer.h"
#include "nic.h"
#include "sched.h"

static icmp_stats_t stats;
static spinlock_t icmp_lock = SPINLOCK_INIT;
static uint16_t next_id;

/* ── ICMP reply queue ──
 * When netd's ip_recv() path dispatches an ICMP ECHO_REPLY through
 * icmp_recv(), we enqueue it here so icmp_echo_seq() can match it
 * without racing nic_recv() against the netd thread.  The queue is
 * small (16 entries) and drained by icmp_reply_dequeue(). */
static icmp_reply_entry_t reply_queue[ICMP_REPLY_QUEUE_SIZE];
static int rq_head, rq_tail;
static spinlock_t rq_lock = SPINLOCK_INIT;

void icmp_reply_queue_init(void) {
    rq_head = rq_tail = 0;
    memset(reply_queue, 0, sizeof(reply_queue));
}

static void reply_enqueue(uint16_t id, uint16_t seq, uint32_t src_ip) {
    spin_lock(&rq_lock);
    int next = (rq_head + 1) % ICMP_REPLY_QUEUE_SIZE;
    if (next == rq_tail) { spin_unlock(&rq_lock); return; } /* full — drop */
    reply_queue[rq_head].id = id;
    reply_queue[rq_head].seq = seq;
    reply_queue[rq_head].src_ip = src_ip;
    reply_queue[rq_head].timestamp = timer_get_milliseconds();
    rq_head = next;
    spin_unlock(&rq_lock);
}

int icmp_reply_dequeue(uint16_t id, uint16_t seq, uint32_t *src_out) {
    spin_lock(&rq_lock);
    while (rq_tail != rq_head) {
        icmp_reply_entry_t *e = &reply_queue[rq_tail];
        if (e->id == id && e->seq == seq) {
            if (src_out) *src_out = e->src_ip;
            rq_tail = (rq_tail + 1) % ICMP_REPLY_QUEUE_SIZE;
            spin_unlock(&rq_lock);
            return 0;
        }
        rq_tail = (rq_tail + 1) % ICMP_REPLY_QUEUE_SIZE;
    }
    spin_unlock(&rq_lock);
    return -1;
}

int icmp_init(void) {
    memset(&stats, 0, sizeof(stats));
    next_id = 1;
    icmp_reply_queue_init();
    kprintf("icmp: init\n");
    return 0;
}

void icmp_send_echo(uint32_t dst, uint16_t id, uint16_t seq) {
    uint8_t pkt[64];
    icmp_echo_t *ic = (icmp_echo_t *)pkt;
    memset(ic, 0, sizeof(icmp_echo_t));
    ic->type = ICMP_ECHO_REQUEST;
    ic->code = 0;
    ic->id = __builtin_bswap16(id);
    ic->seq = __builtin_bswap16(seq);
    for (int i = (int)sizeof(icmp_echo_t); i < 64; i++) pkt[i] = (uint8_t)(i & 0xFF);
    ic->checksum = 0;
    ic->checksum = ip_checksum(pkt, 64);
    ip_send(dst, IP_PROTO_ICMP, pkt, 64);
}

int icmp_echo(uint32_t dst, int timeout_ms) {
    return icmp_echo_seq(dst, 0, timeout_ms);
}

int icmp_echo_seq(uint32_t dst, uint16_t seq, int timeout_ms) {
    uint16_t id = next_id++;
    uint64_t start = timer_get_milliseconds();
    /* Drain any stale replies for this id BEFORE sending to avoid
     * the reply from this echo being caught by a stale drain. */
    { uint32_t dummy; while (icmp_reply_dequeue(id, seq, &dummy) == 0) {} }
    icmp_send_echo(dst, id, seq);

    while ((int)(timer_get_milliseconds() - start) < timeout_ms) {
        sched_sleep_ms(1);
        /* Check the reply queue — icmp_recv() enqueues ECHO_REPLY
         * packets here via the netd/ip_recv path, avoiding the race
         * where netd drains nic_recv() before we can read it. */
        uint32_t src;
        if (icmp_reply_dequeue(id, seq, &src) == 0) {
            return (int)(timer_get_milliseconds() - start);
        }
    }
    spin_lock(&icmp_lock);
    stats.timeout++;
    spin_unlock(&icmp_lock);
    return -1;
}

void icmp_recv(const uint8_t *pkt, int len, uint32_t src_ip) {
    if (len < (int)sizeof(icmp_echo_t)) { spin_lock(&icmp_lock); stats.rx_errors++; spin_unlock(&icmp_lock); return; }
    if (len > 2048) { spin_lock(&icmp_lock); stats.rx_errors++; spin_unlock(&icmp_lock); return; }
    const icmp_echo_t *ic = (const icmp_echo_t *)pkt;
    uint8_t type = ic->type;
    spin_lock(&icmp_lock);
    stats.rx_msgs++;
    spin_unlock(&icmp_lock);
    if (type == ICMP_ECHO_REQUEST) {
        int reply_len = len;
        if (reply_len > 512) reply_len = 512;
        uint8_t reply[512];
        memcpy(reply, pkt, reply_len);
        icmp_echo_t *r = (icmp_echo_t *)reply;
        r->type = ICMP_ECHO_REPLY;
        r->checksum = 0;
        r->checksum = ip_checksum(reply, reply_len);
        if (ip_send(src_ip, IP_PROTO_ICMP, reply, reply_len) < 0) {
            spin_lock(&icmp_lock); stats.tx_errors++; spin_unlock(&icmp_lock);
        }
        spin_lock(&icmp_lock);
        stats.tx_msgs++;
        spin_unlock(&icmp_lock);
    } else if (type == ICMP_ECHO_REPLY) {
        spin_lock(&icmp_lock);
        stats.rx_echo_reply++;
        spin_unlock(&icmp_lock);
        /* Enqueue into the reply queue so icmp_echo_seq() can match it
         * without racing nic_recv() against the netd thread. */
        reply_enqueue(__builtin_bswap16(ic->id), __builtin_bswap16(ic->seq), src_ip);
    } else if (type == ICMP_DEST_UNREACHABLE) {
        spin_lock(&icmp_lock);
        stats.rx_dest_unreach++;
        spin_unlock(&icmp_lock);
    } else if (type == ICMP_TIME_EXCEEDED) {
        spin_lock(&icmp_lock);
        stats.rx_time_exceeded++;
        spin_unlock(&icmp_lock);
    } else if (type == ICMP_REDIRECT) {
        spin_lock(&icmp_lock);
        stats.rx_redirect++;
        spin_unlock(&icmp_lock);
    }
}

void icmp_send_unreachable(uint32_t dst, uint8_t code, const void *orig, int orig_len) {
    if (!orig || orig_len <= 0) return;
    int copy = orig_len;
    if (copy > 556) copy = 556;
    uint8_t pkt[576];
    memset(pkt, 0, sizeof(icmp_error_t) + copy);
    pkt[0] = ICMP_DEST_UNREACHABLE;
    pkt[1] = code;
    memcpy(pkt + sizeof(icmp_error_t), orig, copy);
    icmp_echo_t *ic = (icmp_echo_t *)pkt;
    ic->checksum = 0;
    ic->checksum = ip_checksum(pkt, sizeof(icmp_error_t) + copy);
    ip_send(dst, IP_PROTO_ICMP, pkt, sizeof(icmp_error_t) + copy);
}

void icmp_send_redirect(uint32_t dst, uint32_t gw, const void *orig, int orig_len) {
    if (!orig || orig_len <= 0) return;
    int copy = orig_len;
    if (copy > 548) copy = 548;
    uint8_t pkt[576];
    memset(pkt, 0, sizeof(icmp_error_t) + 8 + copy);
    pkt[0] = ICMP_REDIRECT;
    pkt[1] = 1;
    uint32_t *gw_ptr = (uint32_t *)(pkt + 4);
    *gw_ptr = gw;
    memcpy(pkt + sizeof(icmp_error_t) + 8, orig, copy);
    icmp_echo_t *ic = (icmp_echo_t *)pkt;
    ic->checksum = ip_checksum(pkt, sizeof(icmp_error_t) + 8 + copy);
    ip_send(dst, IP_PROTO_ICMP, pkt, sizeof(icmp_error_t) + 8 + copy);
}

void icmp_send_time_exceeded(uint32_t dst, uint8_t code, const void *orig, int orig_len) {
    if (!orig || orig_len <= 0) return;
    int copy = orig_len;
    if (copy > 556) copy = 556;
    uint8_t pkt[576];
    memset(pkt, 0, sizeof(icmp_error_t) + copy);
    pkt[0] = ICMP_TIME_EXCEEDED;
    pkt[1] = code;
    memcpy(pkt + sizeof(icmp_error_t), orig, copy);
    icmp_echo_t *ic = (icmp_echo_t *)pkt;
    ic->checksum = ip_checksum(pkt, sizeof(icmp_error_t) + copy);
    ip_send(dst, IP_PROTO_ICMP, pkt, sizeof(icmp_error_t) + copy);
}

void icmp_send_quench(uint32_t dst) {
    uint8_t pkt[8];
    memset(pkt, 0, 8);
    pkt[0] = ICMP_SOURCE_QUENCH;
    icmp_echo_t *ic = (icmp_echo_t *)pkt;
    ic->checksum = ip_checksum(pkt, 8);
    ip_send(dst, IP_PROTO_ICMP, pkt, 8);
}

void icmp_send_timestamp(uint32_t dst, uint16_t id) {
    uint8_t pkt[20];
    memset(pkt, 0, 20);
    pkt[0] = ICMP_TIMESTAMP;
    icmp_echo_t *ic = (icmp_echo_t *)pkt;
    ic->id = __builtin_bswap16(id);
    uint32_t now = (uint32_t)(timer_get_milliseconds() & 0xFFFFFFFF);
    uint32_t *ts = (uint32_t *)(pkt + 4);
    ts[0] = __builtin_bswap32(now);
    ic->checksum = ip_checksum(pkt, 20);
    ip_send(dst, IP_PROTO_ICMP, pkt, 20);
}

int icmp_timestamp(uint32_t dst, uint32_t *ts, int timeout_ms) {
    if (!ts) return -1;
    uint16_t id = next_id++;
    icmp_send_timestamp(dst, id);
    uint64_t start = timer_get_milliseconds();
    while ((int)(timer_get_milliseconds() - start) < timeout_ms) {
        sched_sleep_ms(1);
        uint8_t b[2048];
        int l;
        while ((l = nic_recv(b, sizeof(b))) > 0) {
            if (l < (int)(sizeof(eth_frame_t) + sizeof(ip_packet_t) + 20)) continue;
            const eth_frame_t *ethf = (const eth_frame_t *)b;
            if (__builtin_bswap16(ethf->type) != ETH_P_IP) continue;
            const ip_packet_t *ip = (const ip_packet_t *)(b + sizeof(eth_frame_t));
            if (ip->protocol != IP_PROTO_ICMP || ip->src != dst) continue;
            int ihl = (ip->ver_ihl & 0x0F) * 4;
            const icmp_echo_t *ic = (const icmp_echo_t *)((uint8_t *)ip + ihl);
            if (ic->type == ICMP_TIMESTAMP_REPLY && __builtin_bswap16(ic->id) == id) {
                const uint32_t *resp = (const uint32_t *)((uint8_t *)ic + 8);
                *ts = __builtin_bswap32(resp[1]);
                return 0;
            }
        }
    }
    return -1;
}

void icmp_get_stats(icmp_stats_t *s) { if (s) { spin_lock(&icmp_lock); *s = stats; spin_unlock(&icmp_lock); } }
void icmp_reset_stats(void) { spin_lock(&icmp_lock); memset(&stats, 0, sizeof(stats)); spin_unlock(&icmp_lock); }
void icmp_dump(void) {
    kprintf("ICMP: rx=%lu tx=%lu err=%lu timeout=%lu\n",
            stats.rx_msgs, stats.tx_msgs, stats.rx_errors, stats.timeout);
}

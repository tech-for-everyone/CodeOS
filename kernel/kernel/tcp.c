#include "tcp.h"
#include "ip.h"
#include "net_internal.h"
#include "kprintf.h"
#include "string.h"
#include "process.h"
#include "mm.h"
#include "sched.h"
#include "spinlock.h"
#include "timer.h"

void net_poll(void);   /* RX pump (net.c): dispatches inbound TCP to tcp_process_packet */

static tcp_socket_t sockets[TCP_MAX_SOCKETS];
static spinlock_t tcp_lock = SPINLOCK_INIT;
static uint32_t next_isn = 1000;

void tcp_init(void) { memset(sockets, 0, sizeof(sockets)); kprintf("tcp: %d slots\n", TCP_MAX_SOCKETS); }

const char *tcp_get_state_name(tcp_state_t s) {
    static const char *names[] = {"CLOSED","LISTEN","SYN_SENT","SYN_RECEIVED","ESTABLISHED",
        "CLOSE_WAIT","LAST_ACK","FIN_WAIT_1","FIN_WAIT_2","CLOSING","TIME_WAIT"};
    return (s >= 0 && s <= TCP_TIME_WAIT) ? names[s] : "UNKNOWN";
}

int tcp_socket(void) {
    spin_lock(&tcp_lock);
    for (int i = 0; i < TCP_MAX_SOCKETS; i++) {
        if (!sockets[i].in_use) {
            memset(&sockets[i], 0, sizeof(tcp_socket_t));
            sockets[i].id = i; sockets[i].state = TCP_CLOSED;
            sockets[i].recv_window = TCP_DEFAULT_WINDOW;
            sockets[i].send_window = TCP_DEFAULT_WINDOW;
            sockets[i].in_use = 1;
            spin_unlock(&tcp_lock); return i;
        }
    }
    spin_unlock(&tcp_lock); return -1;
}

static uint16_t tcp_csum(uint32_t src, uint32_t dst, const void *seg, uint16_t seg_len) {
    uint8_t pseudo[12];
    pseudo[0] = (uint8_t)(src & 0xFF); pseudo[1] = (uint8_t)(src >> 8);
    pseudo[2] = (uint8_t)(src >> 16); pseudo[3] = (uint8_t)(src >> 24);
    pseudo[4] = (uint8_t)(dst & 0xFF); pseudo[5] = (uint8_t)(dst >> 8);
    pseudo[6] = (uint8_t)(dst >> 16); pseudo[7] = (uint8_t)(dst >> 24);
    pseudo[8] = 0; pseudo[9] = IP_PROTO_TCP;
    pseudo[10] = (uint8_t)(seg_len >> 8); pseudo[11] = (uint8_t)seg_len;
    uint32_t sum = 0;
    for (int i = 0; i < 12; i += 2) sum += (uint32_t)(pseudo[i] | (pseudo[i+1] << 8));
    const uint8_t *p = (const uint8_t *)seg;
    for (int i = 0; i + 1 < seg_len; i += 2) sum += (uint32_t)(p[i] | (p[i+1] << 8));
    if (seg_len & 1) sum += p[seg_len-1];
    while (sum >> 16) sum = (sum & 0xFFFF) + (sum >> 16);
    return (uint16_t)(~sum & 0xFFFF);
}

static void tcp_send_segment(int fd, uint8_t flags, const void *data, int len) {
    tcp_socket_t *s = &sockets[fd];
    if (!s->in_use || len < 0 || len > TCP_MSS) return;
    int total = 20 + len;
    char *pkt = (char *)malloc(total);
    if (!pkt) return;
    tcp_header_t *th = (tcp_header_t *)pkt;
    th->src_port = __builtin_bswap16(s->local_port);
    th->dst_port = __builtin_bswap16(s->remote_port);
    th->seq_num = __builtin_bswap32(s->seq_num);
    th->ack_num = __builtin_bswap32(s->ack_num);
    th->data_offset_flags = (uint8_t)((5<<4)|0);
    th->flags = flags;
    th->window = __builtin_bswap16(s->recv_window);
    th->checksum = 0; th->urgent_ptr = 0;
    if (len > 0 && data) memcpy(pkt+20, data, len);
    th->checksum = tcp_csum(net_get_ip(), s->remote_ip, pkt, total);
    ip_send_to(s->local_ip ? s->local_ip : ip_get_addr(), s->remote_ip,
               IP_PROTO_TCP, pkt, total);
    free(pkt);
    if (flags & TCP_SYN) s->seq_num++;
    if (flags & TCP_FIN) s->seq_num++;
    if (len > 0) s->seq_num += (uint32_t)len;
}

int tcp_connect(uint32_t ip, uint16_t port) {
    int fd = tcp_socket();
    if (fd < 0) return -1;
    tcp_socket_t *s = &sockets[fd];
    s->remote_ip = ip; s->remote_port = port;
    s->local_port = 1024 + (uint16_t)(fd*137+42);
    s->seq_num = next_isn; next_isn += 1000;
    s->state = TCP_SYN_SENT;
    tcp_send_segment(fd, TCP_SYN, 0, 0);
    uint64_t start = timer_get_milliseconds();
    while ((int)(timer_get_milliseconds() - start) < TCP_TIMEOUT_MS) {
        if (s->state == TCP_ESTABLISHED) return fd;
        if (s->state == TCP_CLOSED) { s->in_use = 0; return -1; }
        net_poll();          /* process inbound SYN-ACK regardless of netd */
        sched_sleep_ms(1);
    }
    if (s->retransmit_count < TCP_RETRIES) {
        s->retransmit_count++;
        s->seq_num = next_isn - 1000;
        tcp_send_segment(fd, TCP_SYN, 0, 0);
        start = timer_get_milliseconds();
        while ((int)(timer_get_milliseconds() - start) < TCP_TIMEOUT_MS) {
            if (s->state == TCP_ESTABLISHED) return fd;
            if (s->state == TCP_CLOSED) { s->in_use = 0; return -1; }
            net_poll();
            sched_sleep_ms(1);
        }
    }
    s->state = TCP_CLOSED; s->in_use = 0; return -1;
}

int tcp_listen(int fd, int backlog) {
    if (fd<0||fd>=TCP_MAX_SOCKETS) return -1;
    spin_lock(&tcp_lock);
    if (!sockets[fd].in_use) { spin_unlock(&tcp_lock); return -1; }
    sockets[fd].state = TCP_LISTEN;
    sockets[fd].backlog = backlog > 0 ? backlog : TCP_MAX_BACKLOG;
    sockets[fd].backlog_count = 0;
    spin_unlock(&tcp_lock);
    return 0;
}

int tcp_accept(int fd) {
    if (fd<0||fd>=TCP_MAX_SOCKETS) return -1;
    tcp_socket_t *s = &sockets[fd];
    if (s->state != TCP_LISTEN) return -1;
    uint64_t start = timer_get_milliseconds();
    while (s->backlog_count == 0) {
        sched_sleep_ms(10);
        if ((int)(timer_get_milliseconds() - start) > 30000) return -1;
    }
    spin_lock(&tcp_lock);
    int new_fd = s->backlog_fds[0];
    for (int i = 1; i < s->backlog_count; i++) s->backlog_fds[i-1] = s->backlog_fds[i];
    s->backlog_count--;
    spin_unlock(&tcp_lock);
    return new_fd;
}

/* Non-blocking peek: connection requests waiting in the backlog of a
 * listening socket. 0 = none pending (safe to skip tcp_accept), -1 if fd
 * is not a valid listening socket. Lets poll-driven GUIs avoid the 30 s
 * blocking accept loop. */
int tcp_pending(int fd) {
    if (fd<0||fd>=TCP_MAX_SOCKETS) return -1;
    spin_lock(&tcp_lock);
    tcp_socket_t *s = &sockets[fd];
    int n = (s->in_use && s->state == TCP_LISTEN) ? s->backlog_count : -1;
    spin_unlock(&tcp_lock);
    return n;
}

int tcp_send(int fd, const void *data, int len) {
    if (fd<0||fd>=TCP_MAX_SOCKETS||len<=0) return -1;
    spin_lock(&tcp_lock);
    if (sockets[fd].state != TCP_ESTABLISHED) {
        spin_unlock(&tcp_lock); return -1;
    }
    spin_unlock(&tcp_lock);
    int sent = 0;
    while (sent < len) {
        int chunk = len - sent;
        if (chunk > TCP_MSS) chunk = TCP_MSS;
        tcp_send_segment(fd, TCP_ACK|TCP_PSH, (const char*)data+sent, chunk);
        sent += chunk;
    }
    return sent;
}

int tcp_recv(int fd, void *buf, int len) {
    if (fd<0||fd>=TCP_MAX_SOCKETS||len<=0) return -1;
    tcp_socket_t *s = &sockets[fd];
    if (s->state!=TCP_ESTABLISHED&&s->state!=TCP_CLOSE_WAIT) return -1;
    uint64_t start = timer_get_milliseconds();
    while (s->recv_len <= 0) {
        if (s->state==TCP_CLOSED) return 0;
        if (s->state==TCP_CLOSE_WAIT) return 0; /* peer FIN'd; all data drained */
        if ((int)(timer_get_milliseconds() - start) > 30000) return -1;
        sched_sleep_ms(5);
    }
    spin_lock(&tcp_lock);
    int copy = s->recv_len < len ? s->recv_len : len;
    memcpy(buf, s->recv_buf+s->recv_off, copy);
    if (copy < s->recv_len) memmove(s->recv_buf, s->recv_buf+s->recv_off+copy, s->recv_len-copy);
    s->recv_len -= copy; s->recv_off = 0;
    s->recv_window = (uint16_t)(TCP_BUF_SIZE - s->recv_len);
    spin_unlock(&tcp_lock);
    tcp_send_segment(fd,TCP_ACK,0,0);
    return copy;
}

int tcp_poll_recv(int fd, int timeout_ms) {
    if (fd<0||fd>=TCP_MAX_SOCKETS) return -1;
    tcp_socket_t *s = &sockets[fd];
    uint64_t start = timer_get_milliseconds();
    for (;;) {
        spin_lock(&tcp_lock);
        int n = s->recv_len;
        int st = s->state;
        spin_unlock(&tcp_lock);
        if (n > 0) return n;
        if (st==TCP_CLOSED) return -1;
        if (st==TCP_CLOSE_WAIT && s->recv_len<=0) return -1; /* peer FIN; drained */
        if (timeout_ms>0 && (int)(timer_get_milliseconds()-start)>=timeout_ms) return 0;
        sched_sleep_ms(2);
    }
}

int tcp_bind(int fd, uint32_t ip, uint16_t port) {
    if (fd<0||fd>=TCP_MAX_SOCKETS) return -1;
    spin_lock(&tcp_lock);
    if (!sockets[fd].in_use) { spin_unlock(&tcp_lock); return -1; }
    for (int i = 0; i < TCP_MAX_SOCKETS; i++) {
        if (i != fd && sockets[i].in_use && sockets[i].local_port == port) {
            spin_unlock(&tcp_lock); return -1;
        }
    }
    sockets[fd].local_port = port;
    sockets[fd].local_ip = ip;
    spin_unlock(&tcp_lock);
    return 0;
}

int tcp_close(int fd) {
    if (fd<0||fd>=TCP_MAX_SOCKETS) return -1;
    spin_lock(&tcp_lock);
    tcp_socket_t *s = &sockets[fd];
    if (!s->in_use) { spin_unlock(&tcp_lock); return -1; }
    if (s->state==TCP_ESTABLISHED) { s->state=TCP_FIN_WAIT_1; spin_unlock(&tcp_lock); tcp_send_segment(fd,TCP_FIN|TCP_ACK,0,0); }
    else if (s->state==TCP_CLOSE_WAIT) { s->state=TCP_LAST_ACK; spin_unlock(&tcp_lock); tcp_send_segment(fd,TCP_FIN|TCP_ACK,0,0); }
    else { s->state=TCP_CLOSED; s->in_use=0; spin_unlock(&tcp_lock); }
    return 0;
}

int tcp_reset(int fd) {
    if (fd<0||fd>=TCP_MAX_SOCKETS) return -1;
    tcp_send_segment(fd, TCP_RST, 0, 0);
    spin_lock(&tcp_lock);
    sockets[fd].state=TCP_CLOSED; sockets[fd].in_use=0;
    spin_unlock(&tcp_lock);
    return 0;
}

void tcp_timer_tick(void) {
    spin_lock(&tcp_lock);
    for (int i = 0; i < TCP_MAX_SOCKETS; i++) {
        tcp_socket_t *s = &sockets[i];
        if (!s->in_use) continue;
        if (s->state==TCP_TIME_WAIT) { s->timer++; if (s->timer>60) { s->state=TCP_CLOSED; s->in_use=0; } }
        if (s->state==TCP_SYN_SENT) {
            s->timer++;
            if (s->timer > TCP_TIMEOUT_MS/10) {
                if (s->retransmit_count<TCP_RETRIES) {
                    s->retransmit_count++; s->timer=0;
                    spin_unlock(&tcp_lock);
                    tcp_send_segment(i,TCP_SYN,0,0);
                    spin_lock(&tcp_lock);
                } else { s->state=TCP_CLOSED; s->in_use=0; }
            }
        }
    }
    spin_unlock(&tcp_lock);
}

void tcp_process_packet(uint32_t src_ip, const void *pkt, int len) {
    if (!pkt||len<20) return;
    const tcp_header_t *th = (const tcp_header_t*)pkt;
    uint16_t dp = __builtin_bswap16(th->dst_port);
    uint16_t sp = __builtin_bswap16(th->src_port);
    if (len < (int)sizeof(tcp_header_t)) return;
    int doff = ((th->data_offset_flags >> 4) & 0xF) * 4;
    if (doff < (int)sizeof(tcp_header_t) || doff > len) return;
    if (tcp_csum(src_ip, ip_get_addr(), pkt, (uint16_t)len) != 0) return;
    spin_lock(&tcp_lock);
    for (int i = 0; i < TCP_MAX_SOCKETS; i++) {
        tcp_socket_t *s = &sockets[i];
        if (!s->in_use||s->local_port!=dp) continue;
        if (s->state!=TCP_LISTEN &&
            (s->remote_port!=sp || s->remote_ip!=src_ip)) continue;
        uint8_t f = th->flags;
        s->send_window = __builtin_bswap16(th->window);
        if (s->state==TCP_LISTEN&&(f&TCP_SYN)) {
            if (s->backlog_count >= TCP_MAX_BACKLOG) continue;
            int n = -1;
            for (int j = 0; j < TCP_MAX_SOCKETS; j++) {
                if (!sockets[j].in_use) {
                    memset(&sockets[j], 0, sizeof(tcp_socket_t));
                    sockets[j].id = j;
                    sockets[j].state = TCP_CLOSED;
                    sockets[j].recv_window = TCP_DEFAULT_WINDOW;
                    sockets[j].send_window = TCP_DEFAULT_WINDOW;
                    sockets[j].in_use = 1;
                    n = j;
                    break;
                }
            }
            if (n<0) continue;
            sockets[n].remote_port=sp; sockets[n].local_port=dp;
            sockets[n].local_ip=ip_get_addr();
            sockets[n].remote_ip=src_ip;
            sockets[n].seq_num=next_isn++;
            sockets[n].ack_num=__builtin_bswap32(th->seq_num)+1;
            sockets[n].recv_next_seq=__builtin_bswap32(th->seq_num)+1;
            sockets[n].state=TCP_SYN_RECEIVED;
            spin_unlock(&tcp_lock);
            tcp_send_segment(n,TCP_SYN|TCP_ACK,0,0);
            spin_lock(&tcp_lock);
            continue;
        }
        if (s->state==TCP_SYN_SENT&&(f&TCP_SYN)&&(f&TCP_ACK)) {
            s->ack_num=__builtin_bswap32(th->seq_num)+1;
            s->recv_next_seq=__builtin_bswap32(th->seq_num)+1;
            s->state=TCP_ESTABLISHED;
            spin_unlock(&tcp_lock);
            tcp_send_segment(i,TCP_ACK,0,0);
            spin_lock(&tcp_lock);
            continue;
        }
        if (f&TCP_RST) {
            s->state=TCP_CLOSED; s->in_use=0; continue;
        }
        if (f&TCP_ACK) {
            if (s->state==TCP_SYN_RECEIVED) {
                s->state=TCP_ESTABLISHED;
                for (int j = 0; j < TCP_MAX_SOCKETS; j++) {
                    if (j != i && sockets[j].in_use && sockets[j].state == TCP_LISTEN &&
                        sockets[j].local_port == s->local_port &&
                        sockets[j].backlog_count < TCP_MAX_BACKLOG) {
                        sockets[j].backlog_fds[sockets[j].backlog_count++] = i;
                        break;
                    }
                }
            }
            if (s->state==TCP_LAST_ACK) { s->state=TCP_CLOSED; s->in_use=0; continue; }
            if (s->state==TCP_FIN_WAIT_1) { s->state=TCP_FIN_WAIT_2; continue; }
        }
        /* Buffer payload *before* the FIN/close transitions below: a FIN and
         * data in the same segment must not lose the trailing bytes, which the
         * old ordering did (FIN moved the socket to CLOSE_WAIT, then the data
         * branch required state==ESTABLISHED). CLOSE_WAIT accepts data too. */
        int dlen = len - doff;
        if (dlen>0&&(s->state==TCP_ESTABLISHED||s->state==TCP_CLOSE_WAIT)) {
            uint32_t seg_seq = __builtin_bswap32(th->seq_num);
            int c = dlen;
            if (s->recv_next_seq != 0 && seg_seq != s->recv_next_seq)
                c = 0; /* duplicate or out-of-order: keep stream ordered */
            int space = TCP_BUF_SIZE - s->recv_len;
            if (c > space) c = space;
            if (c>0) {
                memcpy(s->recv_buf+s->recv_len,(const char*)pkt+doff,c);
                s->recv_len+=c;
                s->recv_next_seq = seg_seq + (uint32_t)c;
                /* Ack only what was buffered, advertising the remaining free
                 * window: the peer throttles to the socket buffer so no
                 * in-flight byte is ever dropped on overflow. */
                s->ack_num = s->recv_next_seq;
                s->recv_window = (uint16_t)(TCP_BUF_SIZE - s->recv_len);
                spin_unlock(&tcp_lock);
                tcp_send_segment(i,TCP_ACK,0,0);
                spin_lock(&tcp_lock);
            }
        }
        if (f&TCP_FIN) {
            s->ack_num=__builtin_bswap32(th->seq_num)+1;
            spin_unlock(&tcp_lock);
            tcp_send_segment(i,TCP_ACK,0,0);
            spin_lock(&tcp_lock);
            if (s->state==TCP_ESTABLISHED) s->state=TCP_CLOSE_WAIT;
            else if (s->state==TCP_FIN_WAIT_2) { s->state=TCP_TIME_WAIT; s->timer=0; }
            continue;
        }
    }
    spin_unlock(&tcp_lock);
}

tcp_state_t tcp_get_state(int fd) {
    if (fd<0||fd>=TCP_MAX_SOCKETS) return TCP_CLOSED;
    return sockets[fd].state;
}

int tcp_socket_count(void) {
    int n = 0;
    spin_lock(&tcp_lock);
    for (int i = 0; i < TCP_MAX_SOCKETS; i++) if (sockets[i].in_use) n++;
    spin_unlock(&tcp_lock);
    return n;
}

int tcp_get_peer(int fd, uint32_t *ip, uint16_t *port) {
    if (fd < 0 || fd >= TCP_MAX_SOCKETS) return -1;
    spin_lock(&tcp_lock);
    if (!sockets[fd].in_use) { spin_unlock(&tcp_lock); return -1; }
    if (ip) *ip = sockets[fd].remote_ip;
    if (port) *port = sockets[fd].remote_port;
    spin_unlock(&tcp_lock);
    return 0;
}

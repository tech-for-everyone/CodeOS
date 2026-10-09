/* dnslookup — minimal DNS A-record query over the codeos socket syscalls.
 * Sends a UDP DNS query to the slirp QEMU gateway DNS (10.0.2.3:53) and
 * pretty-prints answers. Exercises socket/bind-connect/send/recvfrom
 * start to finish through userspace.
 */
#include "unistd.h"
#include "stdio.h"
#include "string.h"
#include "stdlib.h"
#include "socket.h"

#define DNS_SERVER "10.0.2.3"

static int dns_name_decode(const uint8_t *msg, int msglen, int off,
                           char *out, int outsz) {
    int n = 0;
    int resume = -1;
    int follow = off;
    for (int guard = 0; guard < 16; guard++) {
        if (follow < 0 || follow >= msglen) return -1;
        uint8_t b = msg[follow];
        if ((b & 0xC0) == 0xC0) {
            if (resume < 0) resume = follow + 2;
            if (follow + 1 >= msglen) return -1;
            follow = ((b & 0x3F) << 8) | msg[follow + 1];
            if (follow >= msglen) return -1;
            continue;
        }
        if (b == 0) return resume >= 0 ? resume : follow + 1;
        if (follow + 1 + b > msglen) return -1;
        if (n) { if (n < outsz - 1) out[n++] = '.'; }
        for (int i = 0; i < b; i++) {
            if (n + 1 < outsz) out[n++] = (char)msg[follow + 1 + i];
        }
        follow += 1 + b;
    }
    return -1;
}

static int build_query(const char *name, uint8_t *q, int qsz) {
    int o = 0;
    q[o++] = 0xC0; q[o++] = 0xDE;
    q[o++] = 0x01; q[o++] = 0x00;
    q[o++] = 0x00; q[o++] = 0x01;
    q[o++] = 0x00; q[o++] = 0x00;
    q[o++] = 0x00; q[o++] = 0x00;
    q[o++] = 0x00; q[o++] = 0x00;
    const char *sp = name;
    while (*sp) {
        const char *dp = sp;
        while (*dp && *dp != '.') dp++;
        int len = (int)(dp - sp);
        if (len == 0 || len > 63) return -1;
        if (o + 1 + len + 5 > qsz) return -1;
        q[o++] = (uint8_t)len;
        for (int i = 0; i < len; i++) q[o++] = (uint8_t)sp[i];
        sp = (*dp ? dp + 1 : dp);
    }
    if (o + 5 > qsz) return -1;
    q[o++] = 0;
    q[o++] = 0x00; q[o++] = 0x01; /* qtype A */
    q[o++] = 0x00; q[o++] = 0x01; /* qclass IN */
    return o;
}

static void print_question_skip(const uint8_t *msg, int msglen, int *pos) {
    char tmp[140];
    int off = dns_name_decode(msg, msglen, *pos, tmp, sizeof(tmp));
    if (off < 0 || off + 4 > msglen) { *pos = -1; return; }
    *pos = off + 4;
}

int main(int argc, char **argv) {
    if (argc < 2) {
        puts("Usage: dnslookup <hostname>");
        puts("  e.g. dnslookup example.com  (uses UDP DNS via socket syscalls)");
        return 1;
    }
    const char *name = argv[1];

    uint8_t q[320];
    int qlen = build_query(name, q, sizeof(q));
    if (qlen < 0) { puts("dnslookup: bad hostname"); return 1; }

    codeos_sockaddr_t sa;
    memset(&sa, 0, sizeof(sa));
    sa.s_addr = cos_inet_addr(DNS_SERVER);
    sa.sin_port = 53;
    sa.sin_family = AF_INET;

    int fd = sock_socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) { puts("dnslookup: socket() failed"); return 1; }
    if (sock_connect(fd, &sa, sizeof(sa)) < 0) {
        puts("dnslookup: connect() failed");
        return 1;
    }

    codeos_sockaddr_t self;
    unsigned int selflen = sizeof(self);
    if (sock_getsockname(fd, &self, &selflen) == 0)
        printf("dnslookup: bound to 10.0.2.15:%u (local source port)\n",
               (unsigned)self.sin_port);

    int n = sock_send(fd, q, qlen, 0);
    if (n < 0) {                       /* send_raw reports wire bytes; any >0 is success */
        printf("dnslookup: send failed (%d)\n", n);
        return 1;
    }

    uint8_t r[512];
    codeos_sockaddr_t from;
    unsigned int fromlen = sizeof(from);
    int rn = sock_recvfrom(fd, r, sizeof(r), 0, &from, &fromlen);
    if (rn < 0) { puts("dnslookup: no response (timeout)"); return 1; }
    if (rn < 12) { puts("dnslookup: short response"); return 1; }

    if (r[0] != 0xC0 || r[1] != 0xDE) {
        printf("dnslookup: bad txn id 0x%02x%02x\n", r[0], r[1]);
        return 1;
    }
    if (!(r[2] & 0x80)) { puts("dnslookup: not a DNS response"); return 1; }

    int rc = r[3] & 0x0F;
    int an = (r[6] << 8) | r[7];
    printf("%s: rcode %d, %d answer(s), %u bytes from %s:%u\n",
           name, rc, an, (unsigned)rn, DNS_SERVER,
           (unsigned)from.sin_port);

    if (rc == 3) { puts("  NXDOMAIN — no such name"); }

    int pos = 12;
    print_question_skip(r, rn, &pos);
    if (pos < 0) { puts("dnslookup: bad question section"); return 1; }

    for (int i = 0; i < an && pos >= 0; i++) {
        char nm[140];
        int off = dns_name_decode(r, rn, pos, nm, sizeof(nm));
        if (off < 0 || off + 10 > rn) break;
        int type = (r[off] << 8) | r[off + 1];
        int rdlen = (r[off + 8] << 8) | r[off + 9];
        int rd = off + 10;
        if (rd + rdlen > rn) break;
        if (type == 1 && rdlen == 4) {
            printf("  %s :: A %d.%d.%d.%d\n", nm,
                   r[rd] & 0xFF, r[rd + 1] & 0xFF,
                   r[rd + 2] & 0xFF, r[rd + 3] & 0xFF);
        } else if (type == 5) {
            char cnm[140];
            dns_name_decode(r, rn, rd, cnm, sizeof(cnm));
            printf("  %s :: CNAME %s\n", nm, cnm);
        } else {
            printf("  %s :: type %d (rdlen %d)\n", nm, type, rdlen);
        }
        pos = rd + rdlen;
    }

    sock_close(fd);
    return rc ? 1 : 0;
}
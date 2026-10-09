/*
 * netbeam.c - NetBeam peer-to-peer file sharing, CodeOS userspace.
 *
 * Userspace port of the NetBeam protocol from the Qt panel
 * (pkgs/extra/qt_apps/netbeam.cpp). The Qt GUI cannot be reused here:
 * netbeam.o has 116 undefined symbols and userspace ELFs provide 6 of
 * them (memcpy/strcmp/strlen/memchr/snprintf/memmove). Everything else the
 * panel used (tcp_*, udp_*, fs_*, sched_*, ip_get_addr, kprintf) is
 * kernel-internal, so the *protocol* is what ports, over syscalls.
 *
 * Wire protocol (NetBeam 1), byte-identical to the Qt panel:
 *
 *   Discovery : UDP broadcast to 255.255.255.255:54917
 *               beacon = "NB1|<device-name>|<transfer-port>\n"
 *               A receiver of a beacon answers with its own, so discovery
 *               completes in both directions immediately.
 *   Transfer  : TCP on 54918. The sender connects and streams
 *               "NB1|<filename>|<size>\n" followed by <size> raw bytes.
 *               The receiver auto-accepts into /NetBeam/Incoming/.
 *
 * There is no MSG_DONTWAIT in this kernel's socket layer (flags are
 * (void)-ed in socket_send/socket_recv), so every recv below is bounded by
 * the kernel's own 30 s timeouts rather than by a non-blocking flag. That
 * is why `discover` bounds its drain loop by iteration count and `serve`
 * runs in its own process rather than trying to multiplex.
 */

#include "unistd.h"
#include "socket.h"
#include "stdio.h"
#include "string.h"
#include "stdlib.h"

/* The host test redirects the inbox and the two ports with -D, because
 * /NetBeam is not writable on Linux and 54918 may be taken. Only the
 * definitions the test actually overrides are guarded -- the protocol
 * constants stay compile-time checked in both builds. */
#ifdef NB_TEST_INBOX
#define NB_INBOX         NB_TEST_INBOX
#else
#define NB_INBOX         "/NetBeam/Incoming"
#endif
#ifdef NB_TEST_TRANSFER_PORT
#define NB_TRANSFER_PORT NB_TEST_TRANSFER_PORT
#else
#define NB_TRANSFER_PORT 54918
#endif
#ifdef NB_TEST_DISCOVER_PORT
#define NB_DISCOVER_PORT NB_TEST_DISCOVER_PORT
#else
#define NB_DISCOVER_PORT 54917
#endif
#define NB_MAGIC         "NB1"
#define NB_MAX_PAYLOAD   1200     /* comfortably under FS_CONTENT_MAX (65536) */
#define NB_BEACON_MS     1000
#define NB_NAME_MAX      31

/* The Qt panel uses 0.FFFFFFFF as "limited broadcast"; the kernel's UDP
 * path special-cases the same value for checksum bypass (udp.c:182). */
#define NB_BROADCAST     0xFFFFFFFFu

#define NB_MAX_PEERS     16

/* memcmp is not in the CodeOS userspace libc (only memset is), so the
 * four-byte magic comparison is written out rather than adding a symbol
 * the rest of userspace cannot link against. */
static int nb_magic_ok(const char *buf, int n) {
    if (n < 5) return 0;
    if (buf[0] != 'N' || buf[1] != 'B' || buf[2] != '1') return 0;
    return buf[3] == '|';
}

static void nb_printf(const char *fmt, ...) {
    char buf[192];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    if (n > 0) sys_write(buf, n);
}

/* ---- address helpers -------------------------------------------------
 * The kernel socket layer is host-order for the port and network-order for
 * the address (syscall_abi.h): s_addr is the raw network-order u32, so
 * 10.0.2.3 arrives as 0x0A000203 and must be printed big-endian.
 */
static void nb_ip_text(uint32_t ip, char *out, int max) {
    unsigned b[4];
    int n = 0, i;
    if (max < 16) { if (max > 0) out[0] = 0; return; }
    b[0] = (unsigned)((ip >> 24) & 0xFF);
    b[1] = (unsigned)((ip >> 16) & 0xFF);
    b[2] = (unsigned)((ip >> 8) & 0xFF);
    b[3] = (unsigned)(ip & 0xFF);
    out[n] = 0;
    for (i = 0; i < 4; i++) {
        int v = (int)b[i];
        char t[4];
        int tn = 0;
        if (i) out[n++] = '.';
        do { t[tn++] = (char)('0' + v % 10); v /= 10; } while (v && tn < 3);
        while (tn) out[n++] = t[--tn];
    }
    out[n] = 0;
}

static codeos_sockaddr_t nb_addr(uint32_t ip, uint16_t port) {
    codeos_sockaddr_t a;
    memset(&a, 0, sizeof(a));
    a.s_addr = ip;                 /* already network order */
    a.sin_port = port;             /* host order, per the ABI */
    a.sin_family = AF_INET;
    return a;
}

/* Strip anything that could escape the inbox directory.
 *
 * The set of rejected characters matches the Qt panel exactly (netbeam.cpp
 * drops '/', '\\' and ' '), because dropping '.' as well silently renamed
 * every real file: "hello.txt" arrived as "hellotxt". Extensions are the
 * common case, so a stricter filter here would not be a safety win, it
 * would just lose data. Traversal is still impossible -- it needs a
 * separator, and both separators are gone by the time this returns.
 *
 * ':' and control characters are additionally dropped because they have no
 * place in a filename and can confuse the host-side VFS. */
static int nb_sanitize(char *dst, const char *src, int src_max) {
    int n = 0, i;
    for (i = 0; i < src_max && src[i] && n < NB_NAME_MAX; i++) {
        char c = src[i];
        if (c == '/' || c == '\\' || c == ' ' || c == ':') continue;
        if ((unsigned char)c < 0x20) continue;
        dst[n++] = c;
    }
    dst[n] = 0;
    return n;
}

/* ---- discovery ------------------------------------------------------ */

struct nb_peer {
    uint32_t ip;
    char     name[NB_NAME_MAX + 1];
    int      replies;
};

static struct nb_peer g_peers[NB_MAX_PEERS];
static int g_peer_cnt;

static struct nb_peer *nb_peer_find(uint32_t ip) {
    int i;
    for (i = 0; i < g_peer_cnt; i++)
        if (g_peers[i].ip == ip) return &g_peers[i];
    return 0;
}

static struct nb_peer *nb_peer_add(uint32_t ip) {
    struct nb_peer *p = nb_peer_find(ip);
    if (p) return p;
    if (g_peer_cnt >= NB_MAX_PEERS) return 0;
    p = &g_peers[g_peer_cnt++];
    p->ip = ip;
    p->name[0] = 0;
    p->replies = 0;
    return p;
}

/* Open the UDP discovery socket. Port 0 lets the stack pick an ephemeral
 * source port, which is what the Qt panel's udp_sendto path effectively
 * got too -- binding 54917 locally would collide with a peer on the same
 * host. */
static int nb_discover_open(void) {
    int fd = sock_socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) return -1;
    /* setsockopt(SO_BROADCAST) is accepted by this kernel but is a no-op
     * (socket.c ignores level/opt/val), so broadcast needs no opt-in. */
    return fd;
}

static int nb_beacon_send(int fd, const char *self_name) {
    char beacon[96];
    codeos_sockaddr_t dst = nb_addr(NB_BROADCAST, NB_DISCOVER_PORT);
    int n = snprintf(beacon, sizeof(beacon), "%s|%s|%d\n",
                     NB_MAGIC, self_name, NB_TRANSFER_PORT);
    if (n <= 0) return -1;
    return sock_sendto(fd, beacon, n, 0, &dst, sizeof(dst));
}

/* Parse "NB1|<name>|<port>\n". Returns the payload length or -1. */
static int nb_beacon_parse(char *buf, int n, char *name_out) {
    int i = 4, nl = 0;
    if (!nb_magic_ok(buf, n)) return -1;
    for (; i < n && nl < NB_NAME_MAX; i++) {
        if (buf[i] == '\n' || buf[i] == '|') break;
        name_out[nl++] = buf[i];
    }
    name_out[nl] = 0;
    return nl;
}

static const char *nb_self_name(void) {
    /* There is no userspace syscall for the interface address (the Qt panel
     * used the kernel-only ip_get_addr), so the advertised identity is a
     * fixed, well-formed name rather than a fabricated hostname. */
    return "netbeam";
}

/* Announce ourselves for `rounds` beacon intervals, printing every peer
 * seen. Bounded so this always terminates. */
static int nb_discover(int rounds, int quiet) {
    int fd = nb_discover_open();
    const char *self = nb_self_name();
    int r;
    if (fd < 0) {
        nb_printf("netbeam: cannot open discovery socket (%d)\n", fd);
        return 1;
    }
    if (!quiet) nb_printf("netbeam: discovering for %d s (Ctrl-C to stop)\n", rounds);
    for (r = 0; r < rounds; r++) {
        int i;
        nb_beacon_send(fd, self);
        /* Drain without blocking the whole interval. */
        for (i = 0; i < 8; i++) {
            char buf[160];
            codeos_sockaddr_t src;
            unsigned int alen = (unsigned int)sizeof(src);
            char name[NB_NAME_MAX + 1];
            int n = sock_recvfrom(fd, buf, (int)sizeof(buf) - 1, 0, &src, &alen);
            char ipbuf[20];
            struct nb_peer *p;
            if (n <= 0) break;
            buf[n] = 0;
            if (nb_beacon_parse(buf, n, name) <= 0) continue;
            /* Reply so the other side sees us without waiting a full
             * interval -- this is what makes discovery bidirectional. */
            nb_beacon_send(fd, self);
            p = nb_peer_add(src.s_addr);
            if (!p) continue;
            memcpy(p->name, name, sizeof(name));
            p->replies++;
            nb_ip_text(src.s_addr, ipbuf, sizeof(ipbuf));
            nb_printf("netbeam: peer %-16s %s\n", ipbuf, p->name);
        }
        sys_sleep(NB_BEACON_MS);
    }
    sock_close(fd);
    nb_printf("netbeam: discovery finished, %d peer(s) seen\n", g_peer_cnt);
    return 0;
}

/* ---- receive -------------------------------------------------------- */

/* Parse "NB1|<name>|<size>\n" out of hdr. */
static int nb_header_parse(char *hdr, char *fname, int *fsize) {
    int i, hl = (int)strlen(hdr);
    int nl = 0, v = 0, got_size = 0;
    if (!nb_magic_ok(hdr, hl)) return 0;
    /* name: from 4 to the next '|' */
    for (i = 4; i < hl && hdr[i] != '|' && hdr[i] != '\n'; i++) { }
    if (i >= hl) return 0;
    nb_sanitize(fname, hdr + 4, i - 4);
    nl = (int)strlen(fname);
    if (!nl) return 0;
    /* size: after the '|' */
    for (i = i + 1; i < hl && hdr[i] != '\n'; i++) {
        if (hdr[i] < '0' || hdr[i] > '9') break;
        v = v * 10 + (hdr[i] - '0');
        got_size = 1;
    }
    if (!got_size || v <= 0 || v > NB_MAX_PAYLOAD) return 0;
    *fsize = v;
    return 1;
}

static int nb_receive_one(int cfd) {
    char hdr[160];
    char fname[NB_NAME_MAX + 1];
    static char body[NB_MAX_PAYLOAD];
    int hl = 0, want = 0, got = 0, fd;
    char path[160];

    while (hl < (int)sizeof(hdr) - 1) {
        char ch;
        if (sock_recv(cfd, &ch, 1, 0) != 1) break;
        hdr[hl++] = ch;
        if (ch == '\n') break;
    }
    hdr[hl] = 0;
    if (!nb_header_parse(hdr, fname, &want)) {
        nb_printf("netbeam: bad handshake from peer\n");
        return -1;
    }
    nb_printf("netbeam: incoming '%s' (%d bytes)\n", fname, want);

    while (got < want) {
        int n = sock_recv(cfd, body + got, want - got, 0);
        if (n <= 0) break;
        got += n;
    }
    if (got != want) {
        nb_printf("netbeam: transfer truncated (%d of %d)\n", got, want);
        return -1;
    }

    /* Create the inbox and any missing parent. The path is walked rather
     * than hardcoding "/NetBeam" so the host test can redirect the inbox
     * into a scratch directory and still exercise this code. mkdir of an
     * existing directory fails harmlessly, so no stat is needed. */
    {
        char dir[sizeof(path)];
        int d;
        snprintf(dir, sizeof(dir), "%s", NB_INBOX);
        for (d = 1; dir[d]; d++) {
            if (dir[d] != '/') continue;
            dir[d] = 0;
            sys_mkdir(dir);
            dir[d] = '/';
        }
        sys_mkdir(dir);
    }

    snprintf(path, sizeof(path), "%s/%s", NB_INBOX, fname);
    fd = sys_open(path, O_WRONLY | O_CREAT | O_TRUNC);
    if (fd < 0) {
        nb_printf("netbeam: cannot create %s\n", path);
        return -1;
    }
    /* sys_write() only ever targets stdout (fd 1); a received file needs
     * its own descriptor, and SYSCALL_PWRITE on a normal fd is exactly
     * sys_write_file(fd, buf, len) in the kernel (syscall.c:2393). */
    if (sys_pwrite(fd, body, got) != got) {
        sys_close(fd);
        nb_printf("netbeam: short write to %s\n", path);
        return -1;
    }
    sys_close(fd);
    nb_printf("netbeam: saved %s (%d bytes)\n", path, got);
    return 0;
}

static int nb_serve(int max_transfers) {
    codeos_sockaddr_t a = nb_addr(0, NB_TRANSFER_PORT);
    int lfd = sock_socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    int served = 0;
    if (lfd < 0) { nb_printf("netbeam: socket() failed (%d)\n", lfd); return 1; }
    if (sock_bind(lfd, &a, sizeof(a)) < 0) {
        nb_printf("netbeam: bind %d failed\n", NB_TRANSFER_PORT);
        sock_close(lfd);
        return 1;
    }
    if (sock_listen(lfd, 4) < 0) {
        nb_printf("netbeam: listen failed\n");
        sock_close(lfd);
        return 1;
    }
    nb_printf("netbeam: serving on port %d, incoming -> %s\n",
              NB_TRANSFER_PORT, NB_INBOX);
    while (served < max_transfers) {
        int cfd = sock_accept(lfd, 0, 0);
        if (cfd < 0) { nb_printf("netbeam: accept failed\n"); continue; }
        nb_receive_one(cfd);
        sock_close(cfd);
        served++;
    }
    sock_close(lfd);
    nb_printf("netbeam: served %d transfer(s)\n", served);
    return 0;
}

/* ---- send ----------------------------------------------------------- */

/* Only the basename goes on the wire. Sending the whole path made the
 * receiver store the file under its full flattened name: sending
 * /tmp/x/payload.bin produced "tmpxpayload.bin" in the inbox, because the
 * receiver's sanitizer strips the separators that would otherwise have made
 * it a nested path. So the sender's path handling, not the receiver's
 * safety check, is where the name has to be narrowed. The Qt panel has the
 * same defect (netbeam.cpp also sends `path`). */
static const char *nb_basename(const char *path) {
    const char *base = path;
    int i;
    for (i = (int)strlen(path) - 1; i >= 0; i--) {
        if (path[i] == '/') { base = path + i + 1; break; }
    }
    return base;
}

static int nb_send_to(uint32_t ip, const char *path) {
    static char body[NB_MAX_PAYLOAD];
    codeos_sockaddr_t a = nb_addr(ip, NB_TRANSFER_PORT);
    char hdr[96];
    char ipbuf[20];
    int fd, hl, got = 0, n;
    /* stat gives us the size without reading it into memory twice. */
    stat_t st;
    if (sys_stat(path, &st) != 0 || st.is_dir) {
        nb_printf("netbeam: cannot stat %s\n", path);
        return 1;
    }
    if (st.size <= 0 || st.size > NB_MAX_PAYLOAD) {
        nb_printf("netbeam: '%s' is %d bytes -- limit is %d\n",
                  path, (int)st.size, NB_MAX_PAYLOAD);
        return 1;
    }
    fd = sys_open(path, O_RDONLY);
    if (fd < 0) { nb_printf("netbeam: cannot open %s\n", path); return 1; }
    while (got < st.size) {
        n = sys_read(fd, body + got, st.size - got);
        if (n <= 0) break;
        got += n;
    }
    sys_close(fd);
    if (got != st.size) { nb_printf("netbeam: short read %d/%d\n", got, st.size); return 1; }

    {
        int s = sock_socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (s < 0) { nb_printf("netbeam: socket() failed\n"); return 1; }
        if (sock_connect(s, &a, sizeof(a)) < 0) {
            nb_printf("netbeam: peer unreachable\n");
            sock_close(s);
            return 1;
        }
        hl = snprintf(hdr, sizeof(hdr), "%s|%s|%d\n", NB_MAGIC,
                         nb_basename(path), got);
        if (sock_send(s, hdr, hl, 0) < 0) {
            nb_printf("netbeam: send failed\n");
            sock_close(s);
            return 1;
        }
        /* 1024 is the kernel's per-syscall cap (syscall.c rejects >1024),
         * so chunk well under it. */
        {
            int off = 0;
            while (off < got) {
                int c = got - off;
                if (c > 512) c = 512;
                if (sock_send(s, body + off, c, 0) < 0) {
                    nb_printf("netbeam: send failed at %d\n", off);
                    sock_close(s);
                    return 1;
                }
                off += c;
            }
        }
        sock_close(s);
    }
    nb_ip_text(ip, ipbuf, sizeof(ipbuf));
    nb_printf("netbeam: sent '%s' (%d bytes) to %s\n", path, got, ipbuf);
    return 0;
}

/* ---- selftest -------------------------------------------------------
 * Runs with no network at all, so it cannot pass by accident because a
 * peer happened to answer. Everything asserted here is either a constant
 * or a property of this process's own state.
 */
static int nb_selftest(void) {
    int fails = 0, i;
    char a[20], b[20];
    char clean[NB_NAME_MAX + 1];
    struct {
        const char *what;
        int ok;
        char got[48];
    } t[16];
    int nt = 0;
    char hdr[64];
    char fn[NB_NAME_MAX + 1];
    int sz = 0;

    /* 1. dotted-quad rendering of a network-order address. */
    nb_ip_text(0x0A000203u, a, sizeof(a));
    t[nt].what = "nb_ip_text(10.0.2.3)"; t[nt].ok = (strcmp(a, "10.0.2.3") == 0);
    snprintf(t[nt].got, sizeof(t[nt].got), "got '%s'", a); nt++;

    nb_ip_text(0xFFFFFFFFu, b, sizeof(b));
    t[nt].what = "nb_ip_text(broadcast)"; t[nt].ok = (strcmp(b, "255.255.255.255") == 0);
    snprintf(t[nt].got, sizeof(t[nt].got), "got '%s'", b); nt++;

    /* 2. Traversal must be stripped, but an extension must SURVIVE --
     * dropping '.' too renamed every file to "hellotxt", so both halves
     * are asserted: the separators go, the dot stays. */
    nb_sanitize(clean, "../../etc/passwd", 15);
    t[nt].what = "sanitize strips separators, keeps dots";
    t[nt].ok = (strchr(clean, '/') == 0 && strchr(clean, '\\') == 0 &&
                strchr(clean, '.') != 0 && clean[0] != 0);
    snprintf(t[nt].got, sizeof(t[nt].got), "got '%s'", clean); nt++;

    nb_sanitize(clean, "hello.txt", 9);
    t[nt].what = "sanitize preserves extensions";
    t[nt].ok = (strcmp(clean, "hello.txt") == 0);
    snprintf(t[nt].got, sizeof(t[nt].got), "got '%s'", clean); nt++;

    /* The sender must put only the basename on the wire, or the receiver's
     * sanitizer flattens the whole path into one long filename. */
    t[nt].what = "basename strips directories";
    t[nt].ok = (strcmp(nb_basename("/tmp/x/payload.bin"), "payload.bin") == 0);
    snprintf(t[nt].got, sizeof(t[nt].got), "got '%s'",
             nb_basename("/tmp/x/payload.bin")); nt++;

    t[nt].what = "basename handles relative + bare names";
    t[nt].ok = (strcmp(nb_basename("payload.bin"), "payload.bin") == 0 &&
                strcmp(nb_basename("a/b/c.txt"), "c.txt") == 0);
    snprintf(t[nt].got, sizeof(t[nt].got), "got '%s' '%s'",
             nb_basename("payload.bin"), nb_basename("a/b/c.txt")); nt++;

    /* 3. handshake parsing, including the rejection of a hostile size. */
    snprintf(hdr, sizeof(hdr), "NB1|hello.txt|42\n");
    t[nt].what = "header parse (valid)";
    t[nt].ok = nb_header_parse(hdr, fn, &sz) && sz == 42 && strcmp(fn, "hello.txt") == 0;
    snprintf(t[nt].got, sizeof(t[nt].got), "sz=%d fn='%s'", sz, fn); nt++;

    /* An oversized declared size must be refused *before* any allocation. */
    snprintf(hdr, sizeof(hdr), "NB1|huge|%d\n", NB_MAX_PAYLOAD + 1);
    sz = 0;
    t[nt].what = "header rejects oversize";
    t[nt].ok = !nb_header_parse(hdr, fn, &sz);
    snprintf(t[nt].got, sizeof(t[nt].got), "sz=%d", sz); nt++;

    snprintf(hdr, sizeof(hdr), "XX1|hello|10\n");
    t[nt].what = "header rejects bad magic";
    t[nt].ok = !nb_header_parse(hdr, fn, &sz);
    snprintf(t[nt].got, sizeof(t[nt].got), "sz=%d", sz); nt++;

    /* 4. peer table: add, find, refresh, and the capacity cap. */
    g_peer_cnt = 0;
    for (i = 0; i < NB_MAX_PEERS + 4; i++) {
        struct nb_peer *p = nb_peer_add(0x0A000200u + (uint32_t)i);
        if (p && i < NB_MAX_PEERS) {
            snprintf(p->name, sizeof(p->name), "peer%d", i);
        }
    }
    t[nt].what = "peer table caps at NB_MAX_PEERS";
    t[nt].ok = (g_peer_cnt == NB_MAX_PEERS);
    snprintf(t[nt].got, sizeof(t[nt].got), "cnt=%d want=%d", g_peer_cnt, NB_MAX_PEERS); nt++;

    t[nt].what = "peer lookup finds known ip";
    t[nt].ok = (nb_peer_find(0x0A000200u) != 0);
    snprintf(t[nt].got, sizeof(t[nt].got), "found=%d", nb_peer_find(0x0A000200u) != 0); nt++;

    /* 5. The ABI constants this binary was built against must be the ones
     * the kernel was built with -- a stale port would otherwise fail only
     * at runtime, on a machine nobody is watching. */
    t[nt].what = "socket ABI matches this build";
    t[nt].ok = (SYSCALL_SOCKET_LISTEN == 78 && SYSCALL_SOCKET_ACCEPT == 79 &&
                CODEOS_SYSCALL_COUNT == 80);
    snprintf(t[nt].got, sizeof(t[nt].got), "listen=%d accept=%d count=%d",
             SYSCALL_SOCKET_LISTEN, SYSCALL_SOCKET_ACCEPT, CODEOS_SYSCALL_COUNT); nt++;

    nb_printf("netbeam: selftest\n");
    for (i = 0; i < nt; i++) {
        if (t[i].ok) {
            nb_printf("netbeam: selftest [ok  ] %s\n", t[i].what);
        } else {
            fails++;
            nb_printf("netbeam: selftest [FAIL] %s -- %s\n", t[i].what, t[i].got);
        }
    }
    nb_printf("netbeam: selftest %s (%d/%d)\n",
              fails ? "FAIL" : "done", nt - fails, nt);
    return fails ? 1 : 0;
}

static void nb_usage(void) {
    nb_printf(
        "NetBeam -- AirDrop-style peer-to-peer file sharing\n"
        "\n"
        "Usage:\n"
        "  netbeam serve [transfers]   accept inbound transfers (default 1)\n"
        "  netbeam send <ip> <file>    send a file to a peer\n"
        "  netbeam discover [seconds]  announce and list nearby peers\n"
        "  netbeam selftest            run offline protocol self-checks\n"
        "\n"
        "Protocol: UDP %d discovery, TCP %d transfer, max %d bytes.\n"
        "Inbound files are saved to %s/.\n",
        NB_DISCOVER_PORT, NB_TRANSFER_PORT, NB_MAX_PAYLOAD, NB_INBOX);
}

int main(int argc, char **argv) {
    if (argc < 2) { nb_usage(); return 1; }
    if (strcmp(argv[1], "selftest") == 0) return nb_selftest();
    if (strcmp(argv[1], "discover") == 0) {
        int rounds = 3;
        if (argc > 2) rounds = atoi(argv[2]);
        if (rounds <= 0) rounds = 1;
        return nb_discover(rounds, 0);
    }
    if (strcmp(argv[1], "serve") == 0) {
        int max = 1;
        if (argc > 2) max = atoi(argv[2]);
        if (max <= 0) max = 1;
        return nb_serve(max);
    }
    if (strcmp(argv[1], "send") == 0) {
        uint32_t ip;
        if (argc < 4) {
            nb_printf("Usage: netbeam send <ip> <file>\n");
            return 1;
        }
        ip = cos_inet_addr(argv[2]);
        if (ip == 0) {
            nb_printf("netbeam: cannot parse ip '%s'\n", argv[2]);
            return 1;
        }
        return nb_send_to(ip, argv[3]);
    }
    nb_usage();
    return 1;
}
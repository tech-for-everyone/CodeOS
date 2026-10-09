/*
 * netbeam_shim.h — POSIX model of the CodeOS syscalls netbeam.c uses.
 *
 * The userspace ELF cannot run on Linux: it issues `int $0x80` with CodeOS
 * numbers, and number 4 (SYSCALL_OPEN) lands on Linux's write(2). So the
 * only way to execute the real netbeam.c off-target is to shadow the CodeOS
 * headers with these definitions and let the host compiler bind them to
 * POSIX. Same approach as pkgs/core/ncvm/tests/codeos_shim.h.
 *
 * WHAT THIS MODELS (the contract):
 *   - open flags: CodeOS mirrors the Linux x86-64 values, so they pass
 *     straight through.
 *   - sockaddr: network-order s_addr, host-order port, 16-byte struct.
 *   - sys_read on a socket fd must behave like recv(2), not read(2).
 *   - sys_pwrite on a normal fd is a plain write(2) to that fd.
 *
 * WHAT IT DOES NOT MODEL -- do not read a host pass as an OS pass:
 *   - The real kernel caps send/sendto/recv/recvfrom at 1024 bytes and
 *     returns -1 above it; POSIX has no such cap. A host test therefore
 *     cannot catch an over-1024 single-syscall send.
 *   - The real socket layer ignores flags entirely (no MSG_DONTWAIT), so
 *     every host recv() here is blocking-by-POSIX and can be given
 *     SO_RCVTIMEO where the guest would instead hit a 30 s kernel
 *     timeout.
 *   - sys_sleep becomes nanosleep; sys_yield becomes sched_yield.
 *   - The kernel's VFS is a fixed 64 KiB per-file node table
 *     (FS_CONTENT_MAX), not a POSIX filesystem.
 */

#ifndef NETBEAM_SHIM_H
#define NETBEAM_SHIM_H

#include <stdint.h>
#include <stddef.h>
#include <stdarg.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sched.h>
#include <time.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <errno.h>

/* ── CodeOS syscall numbers, as compiled into the real ELF ──────────────
 * These must match include/codeos/syscall_abi.h exactly; netbeam's selftest
 * asserts the two socket ones, so a stale copy here is itself a failure. */
#define SYSCALL_SOCKET_LISTEN      78
#define SYSCALL_SOCKET_ACCEPT      79
#define CODEOS_SYSCALL_COUNT       80

/* ── The open flags are the contract, so they are CHECKED, not restated ──
 * sys_open() forwards these straight to POSIX open(), so if CodeOS ever
 * diverged from Linux here a host pass would silently test the wrong
 * behaviour. Asserting equality makes that a compile error instead. */
#define NB_STATIC_ASSERT(cond, tag) \
    typedef char nb_static_assert_##tag[(cond) ? 1 : -1]

/* kernel/userspace/include/unistd.h */
NB_STATIC_ASSERT(O_RDONLY == 0x0,   o_rdonly);
NB_STATIC_ASSERT(O_WRONLY == 0x1,   o_wronly);
NB_STATIC_ASSERT(O_RDWR   == 0x2,   o_rdwr);
NB_STATIC_ASSERT(O_CREAT  == 0x40,  o_creat);
NB_STATIC_ASSERT(O_TRUNC  == 0x200, o_trunc);

typedef struct {
    int size;
    int is_dir;
} stat_t;

typedef struct {
    uint32_t s_addr;
    uint16_t sin_port;
    uint16_t sin_family;
    uint8_t  sin_zero[8];
} codeos_sockaddr_t;

static inline int sys_write(const void *buf, int count) {
    return (int)write(1, buf, (size_t)count);
}

/* The guest reads a file or a socket through one entry point; the host has
 * to be told which. Socket fds are tagged so sys_read on one becomes recv. */
static int shim_fd_is_socket(int fd) {
    int type = 0;
    socklen_t len = sizeof(type);
    if (getsockopt(fd, SOL_SOCKET, SO_TYPE, &type, &len) != 0) return 0;
    return (type == SOCK_STREAM || type == SOCK_DGRAM);
}

static inline int sys_read(int fd, void *buf, int count) {
    if (fd < 0 || count <= 0) return -1;
    if (shim_fd_is_socket(fd))
        return (int)recv(fd, buf, (size_t)count, 0);
    return (int)read(fd, buf, (size_t)count);
}

/* On a normal fd the kernel routes SYSCALL_PWRITE to sys_write_file, i.e. a
 * plain write to that descriptor (see kernel/kernel/syscall.c). */
static inline int sys_pwrite(int fd, const void *buf, int count) {
    if (fd < 0 || count <= 0) return -1;
    return (int)write(fd, buf, (size_t)count);
}

static inline int sys_open(const char *path, int flags) {
    return open(path, flags, 0644);
}

static inline int sys_close(int fd) { return close(fd); }
static inline void sys_exit(int status) { exit(status); }

static inline void sys_sleep(int ms) {
    struct timespec ts;
    if (ms <= 0) return;
    ts.tv_sec = ms / 1000;
    ts.tv_nsec = (long)(ms % 1000) * 1000000L;
    nanosleep(&ts, 0);
}

static inline void sys_yield(void) { sched_yield(); }

static inline int sys_mkdir(const char *path) { return mkdir(path, 0755); }

static inline int sys_stat(const char *path, stat_t *buf) {
    struct stat st;
    if (stat(path, &st) != 0) return -1;
    buf->size = (int)st.st_size;
    buf->is_dir = S_ISDIR(st.st_mode) ? 1 : 0;
    return 0;
}

/* ── sockets ──────────────────────────────────────────────────────────── */

/*
 * BYTE ORDER IS THE WHOLE STORY HERE, and getting it wrong is silent.
 *
 * CodeOS carries an address as a big-endian-packed *integer*: the kernel's
 * addr_to_sock() copies s_addr straight through (kernel/kernel/socket.c:25)
 * and ip.c prints it with `>> 24` as the first octal group. So 10.0.2.3 is
 * the integer 0x0A000203.
 *
 * POSIX sin_addr.s_addr is an array of bytes *in network order* -- the
 * first octet is the lowest address, not the top of the word. Assigning the
 * integer directly therefore targets 1.0.0.130 instead of 10.0.2.3, and on
 * a host that only listens on loopback the connect() then blocks forever
 * rather than failing, which reads exactly like a hang in the code under
 * test. ntohl/htonl are the conversion.
 */
static int shim_to_sockaddr(const codeos_sockaddr_t *in, struct sockaddr_in *out) {
    memset(out, 0, sizeof(*out));
    out->sin_family = AF_INET;
    out->sin_port = htons(in->sin_port);      /* guest port is host order */
    out->sin_addr.s_addr = htonl(in->s_addr); /* big-endian-packed int */
    return (int)sizeof(*out);
}

static void shim_from_sockaddr(struct sockaddr_in *in, codeos_sockaddr_t *out) {
    memset(out, 0, sizeof(*out));
    out->s_addr = ntohl(in->sin_addr.s_addr);
    out->sin_port = ntohs(in->sin_port);
    out->sin_family = AF_INET;
}

static inline int sock_socket(int domain, int type, int proto) {
    return socket(domain, type, proto);
}

static inline int sock_close(int fd) { return close(fd); }

static inline int sock_bind(int fd, const codeos_sockaddr_t *addr, int addrlen) {
    struct sockaddr_in sa;
    (void)addrlen;
    shim_to_sockaddr(addr, &sa);
    return bind(fd, (struct sockaddr *)&sa, sizeof(sa));
}

static inline int sock_connect(int fd, const codeos_sockaddr_t *addr, int addrlen) {
    struct sockaddr_in sa;
    (void)addrlen;
    shim_to_sockaddr(addr, &sa);
    return connect(fd, (struct sockaddr *)&sa, sizeof(sa));
}

static inline int sock_listen(int fd, int backlog) { return listen(fd, backlog); }

static inline int sock_accept(int fd, codeos_sockaddr_t *addr, int *addrlen) {
    struct sockaddr_in sa;
    socklen_t sl = sizeof(sa);
    int c = accept(fd, (struct sockaddr *)&sa, &sl);
    if (c < 0) return -1;
    if (addr && addrlen) shim_from_sockaddr(&sa, addr);
    return c;
}

static inline int sock_send(int fd, const void *buf, int len, int flags) {
    return (int)send(fd, buf, (size_t)len, flags);
}

static inline int sock_recv(int fd, void *buf, int len, int flags) {
    return (int)recv(fd, buf, (size_t)len, flags);
}

static inline int sock_sendto(int fd, const void *buf, int len, int flags,
                              const codeos_sockaddr_t *addr, int addrlen) {
    struct sockaddr_in sa;
    (void)addrlen;
    shim_to_sockaddr(addr, &sa);
    return (int)sendto(fd, buf, (size_t)len, flags,
                       (struct sockaddr *)&sa, sizeof(sa));
}

static inline int sock_recvfrom(int fd, void *buf, int len, int flags,
                                codeos_sockaddr_t *addr, unsigned int *addrlen) {
    struct sockaddr_in sa;
    socklen_t sl = sizeof(sa);
    int r = (int)recvfrom(fd, buf, (size_t)len, flags,
                          (struct sockaddr *)&sa, &sl);
    if (r < 0) return r;
    if (addr) shim_from_sockaddr(&sa, addr);
    (void)addrlen;
    return r;
}

static inline int sock_getsockname(int fd, codeos_sockaddr_t *addr, unsigned int *addrlen) {
    struct sockaddr_in sa;
    socklen_t sl = sizeof(sa);
    if (getsockname(fd, (struct sockaddr *)&sa, &sl) != 0) return -1;
    if (addr) shim_from_sockaddr(&sa, addr);
    (void)addrlen;
    return 0;
}

static inline int sock_getpeername(int fd, codeos_sockaddr_t *addr, unsigned int *addrlen) {
    struct sockaddr_in sa;
    socklen_t sl = sizeof(sa);
    if (getpeername(fd, (struct sockaddr *)&sa, &sl) != 0) return -1;
    if (addr) shim_from_sockaddr(&sa, addr);
    (void)addrlen;
    return 0;
}

static inline int sock_setsockopt(int fd, int level, int opt, const void *val, int len) {
    return setsockopt(fd, level, opt, val, (socklen_t)len);
}

static inline int sock_getsockopt(int fd, int level, int opt, void *val, int *len) {
    /* getsockopt takes socklen_t*, and it is in/out: POSIX writes the
     * actual length back. Copying through a local keeps the caller's int
     * intact and matches the CodeOS ABI's pointer semantics. */
    socklen_t sl = (socklen_t)(len ? *len : 0);
    int r = getsockopt(fd, level, opt, val, &sl);
    if (r == 0 && len) *len = (int)sl;
    return r;
}

/* Dotted-quad -> network-order u32. Copied verbatim from the real
 * kernel/userspace/include/socket.h so the tested code path is the one
 * that ships. */
static inline uint32_t cos_inet_addr(const char *ip) {
    uint32_t a[4] = {0, 0, 0, 0};
    int n = 0, cur = 0;
    const char *p = ip;
    while (*p) {
        if (*p == '.') { n++; cur = 0; p++; continue; }
        if (n > 3 || *p < '0' || *p > '9') return 0;
        cur = cur * 10 + (*p - '0');
        if (cur > 255) return 0;
        a[n] = cur;
        p++;
    }
    return (a[0] << 24) | (a[1] << 16) | (a[2] << 8) | a[n];
}

/* vsnprintf is in the CodeOS userspace libc (lib/stdio.c). */
int vsnprintf(char *buf, size_t size, const char *fmt, va_list ap);

#endif /* NETBEAM_SHIM_H */
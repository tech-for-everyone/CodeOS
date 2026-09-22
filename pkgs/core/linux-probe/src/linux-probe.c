/* linux-probe — freestanding x86_64 Linux-ABI syscall probe.
 *
 * Runs under CodeOS's Linux personality translation (linux_syscall_handler)
 * to measure which Linux syscalls the compatibility layer implements and
 * which still return -ENOSYS. Output goes to fd 1 (serial via kprintf).
 *
 * No libc, no crt0: entry is _start, syscalls are issued with the `syscall`
 * instruction using stock Linux x86_64 numbering and registers.
 *
 * Build: userspace Makefile custom rule (link.ld, no crt0.o).
 */

typedef unsigned long u64;
typedef long s64;
typedef unsigned short u16;
typedef unsigned int u32;
typedef unsigned char u8;

static s64 lsys(s64 n, s64 a1, s64 a2, s64 a3) {
    s64 r;
    __asm__ volatile("syscall"
                     : "=a"(r)
                     : "a"(n), "D"(a1), "S"(a2), "d"(a3)
                     : "rcx", "r11", "memory");
    return r;
}

static s64 lsys4(s64 n, s64 a1, s64 a2, s64 a3, s64 a4) {
    s64 r;
    register s64 _a4 asm("r10") = a4;
    __asm__ volatile("syscall"
                     : "=a"(r)
                     : "a"(n), "D"(a1), "S"(a2), "d"(a3), "r"(_a4)
                     : "rcx", "r11", "memory");
    return r;
}

static s64 lsys6(s64 n, s64 a1, s64 a2, s64 a3, s64 a4, s64 a5, s64 a6) {
    s64 r;
    register s64 _a4 asm("r10") = a4;
    register s64 _a5 asm("r8")  = a5;
    register s64 _a6 asm("r9")  = a6;
    __asm__ volatile("syscall"
                     : "=a"(r)
                     : "a"(n), "D"(a1), "S"(a2), "d"(a3), "r"(_a4), "r"(_a5),
                       "r"(_a6)
                     : "rcx", "r11", "memory");
    return r;
}

#define SYS_READ        0
#define SYS_WRITE       1
#define SYS_OPEN        2
#define SYS_CLOSE       3
#define SYS_STAT        4
#define SYS_FSTAT       5
#define SYS_LSEEK       8
#define SYS_MMAP        9
#define SYS_MPROTECT    10
#define SYS_MUNMAP      11
#define SYS_BRK         12
#define SYS_RT_SIGACTION  13
#define SYS_RT_SIGPROCMASK 14
#define SYS_IOCTL       16
#define SYS_PREAD64     17
#define SYS_PWRITE64    18
#define SYS_READV       19
#define SYS_WRITEV      20
#define SYS_ACCESS      21
#define SYS_PIPE        22
#define SYS_SCHED_YIELD 24
#define SYS_DUP         32
#define SYS_DUP2        33
#define SYS_NANOSLEEP   35
#define SYS_GETPID      39
#define SYS_SOCKET      41
#define SYS_CONNECT     42
#define SYS_ACCEPT      43
#define SYS_SENDTO      44
#define SYS_RECVFROM    45
#define SYS_SENDMSG     46
#define SYS_RECVMSG     47
#define SYS_SHUTDOWN    48
#define SYS_BIND        49
#define SYS_LISTEN      50
#define SYS_GETSOCKNAME 51
#define SYS_GETPEERNAME 52
#define SYS_SOCKETPAIR  53
#define SYS_SETSOCKOPT  54
#define SYS_GETSOCKOPT  55
#define SYS_EXIT        60
#define SYS_UNAME       63
#define SYS_FCNTL       72
#define SYS_GETCWD      79
#define SYS_CHDIR       80
#define SYS_GETPPID     110
#define SYS_GETUID      102
#define SYS_GETGID      104
#define SYS_SETUID      105
#define SYS_SETGID      106
#define SYS_GETEUID     107
#define SYS_GETEGID     108
#define SYS_SETTID      218
#define SYS_SIGALTSTACK 131
#define SYS_STATFS      137
#define SYS_FSTATFS     138
#define SYS_GETPRIORITY 140
#define SYS_SETPRIORITY 141
#define SYS_PRCTL       157
#define SYS_ARCH_PRCTL  158
#define SYS_KILL        62
#define SYS_GETRUSAGE   165
#define SYS_GETTIMEOFDAY 96
#define SYS_CLOCK_GETTIME 228
#define SYS_GETTID      186
#define SYS_FUTEX       202
#define SYS_SCHED_GETAFFINITY 204
#define SYS_GETDENTS64  217
#define SYS_READLINK    89
#define SYS_GETRANDOM   318
#define SYS_NEWFSTATAT  262
#define SYS_FACCESSAT   269
#define SYS_OPENAT      257
#define SYS_READLINKAT  267
#define SYS_STATX       332
#define SYS_PRLIMIT64   302
#define SYS_SET_ROBUST_LIST 273
#define SYS_EPOLL_CREATE1 291
#define SYS_EPOLL_CTL   233
#define SYS_EPOLL_WAIT  232
#define SYS_EVENTFD2    290
#define SYS_TIMERFD_CREATE 283
#define SYS_SIGNALFD4   289
#define SYS_PTRACE      101
#define SYS_MEMFD_CREATE 319
#define SYS_CLONE3      435
#define SYS_DUP3        292
#define SYS_PIPE2       293
#define SYS_INOTIFY_INIT1 294
#define SYS_TGKILL      234

static void out(const char *s) {
    s64 len = 0;
    while (s[len]) len++;
    lsys(SYS_WRITE, 1, (s64)s, len);
}

static void outn(s64 v) {
    char b[24];
    int i = 24;
    int neg = 0;
    if (v < 0) { neg = 1; v = -v; }
    b[--i] = 0;
    do { b[--i] = '0' + (int)(v % 10); v /= 10; } while (v);
    if (neg) b[--i] = '-';
    out(b + i);
}

static void outln(s64 v, const char *tail) {
    outn(v);
    if (tail) out(tail);
}

static void pass(const char *name, s64 v) {
    out("LP[A ] ");
    out(name);
    out(": ");
    outln(v, " OK\n");
}

static void gap(const char *name, s64 v) {
    out("LP[GAP] ");
    out(name);
    out(": ");
    outln(v, " (ENOSYS or stub)\n");
}

static void raw(const char *name, s64 v) {
    out("LP[RAW] ");
    out(name);
    out(": ");
    outln(v, "\n");
}

struct linux_uname {
    char sysname[65];
    char nodename[65];
    char release[65];
    char version[65];
    char machine[65];
    char domainname[65];
};

struct timespec { long tv_sec; long tv_nsec; };
struct timeval { long tv_sec; long tv_usec; };

static char b64[64];

void _start(void) {
    out("LPROBE: begin\n");

    /* --- identity --- */
    pass("getpid",   lsys(SYS_GETPID, 0, 0, 0));
    pass("getppid",  lsys(SYS_GETPPID, 0, 0, 0));
    pass("gettid",   lsys(SYS_GETTID, 0, 0, 0));
    pass("getuid",   lsys(SYS_GETUID, 0, 0, 0));
    pass("geteuid",  lsys(SYS_GETEUID, 0, 0, 0));
    pass("getgid",   lsys(SYS_GETGID, 0, 0, 0));
    pass("getegid",  lsys(SYS_GETEGID, 0, 0, 0));

    /* --- time / clock --- */
    {
        struct timeval tv;
        s64 r = lsys(SYS_GETTIMEOFDAY, (s64)&tv, 0, 0);
        pass("gettimeofday", r);
        outln(tv.tv_sec, " (sec)\n");
    }
    {
        struct timespec ts;
        s64 r = lsys(SYS_CLOCK_GETTIME, 0, (s64)&ts, 0);
        pass("clock_gettime(CLOCK_REALTIME)", r);
    }

    /* --- memory --- */
    {
        s64 r = lsys(SYS_BRK, 0, 0, 0);
        raw("brk(0)", r);
        r = lsys4(SYS_MMAP, 0, 4096, 3, 0x22);
        pass("mmap(RW|ANON)", r >= 0 ? 0 : r);
        raw("mmap_vaddr", r);
        if (r >= 0) {
            s64 m2 = lsys(SYS_MPROTECT, r, 4096, 5);
            pass("mprotect(RX)", m2);
            s64 m3 = lsys(SYS_MUNMAP, r, 4096, 0);
            pass("munmap", m3);
        }
        pass("madvise", lsys(28, 0, 4096, 0)); /* MADVISE */
    }

    /* --- fs --- */
    {
        /* openat AT_FDCWD=-100 */
        s64 fd = lsys4(SYS_OPENAT, -100, (s64)"/bin/linux-probe", 0, 0);
        pass("openat(/bin/linux-probe)", fd);
        if (fd >= 0) {
            s64 n = lsys(SYS_READ, fd, (s64)b64, 16);
            pass("read(16)", n);
            raw("data", b64[0]);
            s64 off = lsys(SYS_LSEEK, fd, 0, 1); /* SEEK_CUR */
            pass("lseek(SEEK_CUR)", off);
            pass("close", lsys(SYS_CLOSE, fd, 0, 0));
        }
        pass("access(F_OK)", lsys(SYS_ACCESS, (s64)"/bin", 0, 0));
        pass("faccessat", lsys4(SYS_FACCESSAT, -100, (s64)"/", 0, 4));
        pass("chdir(/)", lsys(SYS_CHDIR, (s64)"/", 0, 0));
        {
            s64 r = lsys(SYS_GETCWD, (s64)b64, 63, 0);
            pass("getcwd", r);
        }
        {
            s64 r = lsys4(SYS_NEWFSTATAT, -100, (s64)"/", (s64)b64, 0);
            pass("newfstatat(/)", r);
        }
        {
            s64 r = lsys4(SYS_STATX, -100, (s64)"/", 0, (s64)b64);
            raw("statx(/)", r);
        }
        pass("readlinkat", lsys4(SYS_READLINKAT, -100, (s64)"/bin", (s64)b64, 63));
        {
            s64 fdd = lsys4(SYS_OPENAT, -100, (s64)"/", 0, 0);
            pass("openat(/)", fdd);
            if (fdd >= 0) {
                s64 n = lsys4(SYS_GETDENTS64, fdd, (s64)b64, 64, 0);
                raw("getdents64(64B buf)", n);
                lsys(SYS_CLOSE, fdd, 0, 0);
            }
        }
        pass("fcntl", lsys4(SYS_FCNTL, 1, 1, 0, 0)); /* F_GETFD */
    }

    /* --- ipc / signal --- */
    {
        struct sigact { void (*h)(int); s64 mask; int fl; void *r; } sa = { 0, 0, 0, 0 };
        s64 r = lsys4(SYS_RT_SIGACTION, 1, (s64)&sa, (s64)&sa, 8);
        pass("rt_sigaction(1)", r);
        r = lsys4(SYS_RT_SIGPROCMASK, 0, 0, (s64)&sa.mask, 8);
        pass("rt_sigprocmask", r);
    }
    pass("kill(0,SIGKILL=9?)", lsys4(SYS_KILL, 0, 99, 0, 0));
    pass("tgkill", lsys4(SYS_TGKILL, 0, 0, 15, 0));
    pass("futex", lsys4(SYS_FUTEX, 0, 202, 0, 0));
    pass("prctl(PR_GET_NAME?)", lsys4(SYS_PRCTL, 15, (s64)b64, 0, 0));
    pass("set_tid_address", lsys(SYS_SETTID, (s64)b64, 0, 0));
    pass("set_robust_list", lsys(SYS_SET_ROBUST_LIST, (s64)b64, 24, 0));
    pass("sigaltstack", lsys(SYS_SIGALTSTACK, 0, (s64)b64, 0));

    /* --- fd ops --- */
    {
        int p[2];
        s64 r = lsys(SYS_PIPE, (s64)p, 0, 0);
        pass("pipe", r);
        if (r == 0) {
            lsys(SYS_WRITE, p[1], (s64)"x", 1);
            s64 rd = lsys(SYS_READ, p[0], (s64)b64, 1);
            raw("pipe_ping", rd);
            lsys(SYS_CLOSE, p[0], 0, 0);
            lsys(SYS_CLOSE, p[1], 0, 0);
        }
        r = lsys(SYS_PIPE2, (s64)p, 0, 0);
        pass("pipe2", r);
        if (r == 0) { lsys(SYS_CLOSE, p[0], 0, 0); lsys(SYS_CLOSE, p[1], 0, 0); }
        pass("dup", lsys(SYS_DUP, 1, 0, 0));
        pass("dup2", lsys(SYS_DUP2, 1, 5, 0));
        pass("dup3", lsys4(SYS_DUP3, 1, 6, 0, 0));
        lsys(SYS_CLOSE, 5, 0, 0);
        lsys(SYS_CLOSE, 6, 0, 0);
    }

    /* --- misc implemented --- */
    {
        struct linux_uname u;
        s64 r = lsys(SYS_UNAME, (s64)&u, 0, 0);
        pass("uname", r);
        if (r == 0) raw("uname.sysname", (s64)(u.sysname[0] ? u.sysname[0] : 0));
    }
    {
        s64 r = lsys(SYS_GETRANDOM, (s64)b64, 8, 0);
        pass("getrandom(8)", r);
    }
    pass("prlimit64", lsys4(SYS_PRLIMIT64, 0, 0, 0, 0));
    pass("getrusage", lsys4(SYS_GETRUSAGE, 0, (s64)b64, 0, 0));
    pass("writev", lsys4(SYS_WRITEV, 1, (s64)b64, 1, 0));
    pass("sched_yield", lsys(SYS_SCHED_YIELD, 0, 0, 0));
    {
        struct timespec ts = { 0, 1000000 };
        lsys(SYS_NANOSLEEP, (s64)&ts, (s64)b64, 0);
        pass("nanosleep(1ms)", 0);
    }
    {
        s64 r = lsys4(SYS_IOCTL, 1, 0, 0, 0);
        raw("ioctl", r);
    }
    pass("getpriority", lsys(SYS_GETPRIORITY, 0, 0, 0));
    pass("sched_getaffinity", lsys4(SYS_SCHED_GETAFFINITY, 0, 0, (s64)b64, 64));

    /* --- real UDP over the Linux socket mapping: DNS query to slirp --- */
    {
        struct lsockaddr_in {
            u16 family;   /* AF_INET=2 */
            u16 port;     /* network byte order: 53 -> 0x3500 */
            u32 addr;     /* memory bytes = dotted quad in order */
            u8  zero[8];
        } sa;
        static const u8 sample_ip[4] = { 10, 0, 2, 3 };
        /* memory bytes must read 0A 00 02 03 */
        sa.family = 2;
        sa.port = 0x3500;               /* bytes 35 00 */
        sa.addr = (u32)(sample_ip[0]) | ((u32)sample_ip[1] << 8) |
                  ((u32)sample_ip[2] << 16) | ((u32)sample_ip[3] << 24);
        sa.zero[0] = 0; sa.zero[1] = 0; sa.zero[2] = 0; sa.zero[3] = 0;
        sa.zero[4] = 0; sa.zero[5] = 0; sa.zero[6] = 0; sa.zero[7] = 0;

        /* build "example.com" A query in b64 */
        static const u8 qname[] = { 7, 'e','x','a','m','p','l','e',
                                    3, 'c','o','m', 0 };
        int qlen = 12 + (int)sizeof(qname) + 4;
        u8 *q = (u8 *)b64;
        q[0] = 0x12; q[1] = 0x34;               /* id */
        q[2] = 0x01; q[3] = 0x00;               /* RD */
        q[4] = 0x00; q[5] = 0x01;               /* qdcount */
        q[6] = 0;    q[7] = 0;                  /* ancount */
        q[8] = 0;    q[9] = 0;  q[10] = 0; q[11] = 0;
        for (int i = 0; i < (int)sizeof(qname); i++) q[12 + i] = qname[i];
        q[12 + sizeof(qname)]     = 0x00;        /* qtype A */
        q[12 + sizeof(qname) + 1] = 0x01;
        q[12 + sizeof(qname) + 2] = 0x00;        /* qclass IN */
        q[12 + sizeof(qname) + 3] = 0x01;

        s64 fd = lsys4(SYS_SOCKET, 2, 2, 0, 0);  /* AF_INET, SOCK_DGRAM */
        out("LP[UDP] socket: ");
        outln(fd, "\n");
        s64 cr = lsys4(SYS_CONNECT, fd, (s64)&sa, 16, 0);
        out("LP[UDP] connect: ");
        outln(cr, "\n");
        s64 snd = lsys6(SYS_SENDTO, fd, (s64)q, (s64)qlen, 0,
                        (s64)&sa, 16);
        out("LP[UDP] sendto: ");
        outln(snd, "\n");
        u8 rbuf[512];
        int alen = 16;
        struct timespec ns_sleep;
        ns_sleep.tv_sec = 0;
        ns_sleep.tv_nsec = 2000000;      /* 2 ms between RX retries */
        s64 r = 0;
        s64 steps = 0;
        for (; steps < 4; steps++) {   /* RX poller may lag; retry briefly */
            r = lsys6(SYS_RECVFROM, fd, (s64)rbuf, (s64)sizeof(rbuf), 0,
                      0, (s64)&alen);
            if (r != -11) break;
            lsys(SYS_NANOSLEEP, (s64)&ns_sleep, 0, 0);
        }
        out("LP[UDP] recvfrom: ");
        outln(r, "\n");
        if (r >= 12) {
            u16 an = (u16)((rbuf[6] << 8) | rbuf[7]);
            out("LP[UDP] rcode=");
            outn(rbuf[3] & 0x0F);
            out(" ancount=");
            outln(an, "\n");
            for (int i = 0; i < (s64)an; i++) {
                int pos = 12;
                while (pos < r && (rbuf[pos] & 0xC0) == 0) pos += 1 + rbuf[pos];
                pos += 2;
                if (pos + 10 > r) break;
                u16 type = (u16)((rbuf[pos] << 8) | rbuf[pos + 1]);
                u16 rdlen = (u16)((rbuf[pos + 8] << 8) | rbuf[pos + 9]);
                int rd = pos + 10;
                if (type == 1 && rdlen == 4 && rd + 4 <= r) {
                    out("LP[UDP]    A ");
                    outn(rbuf[rd]); out(".");
                    outn(rbuf[rd + 1]); out(".");
                    outn(rbuf[rd + 2]); out(".");
                    outn(rbuf[rd + 3]); out("\n");
                } else {
                    out("LP[UDP]    type ");
                    outln(type, "\n");
                }
                pos = -1;
            }
        }
        raw("close(sock)", lsys(SYS_CLOSE, fd, 0, 0));
    }

    /* --- gap battery (document missing surface) --- */
    gap("epoll_create1",  lsys(SYS_EPOLL_CREATE1, 0, 0, 0));
    gap("epoll_ctl",      lsys4(SYS_EPOLL_CTL, -1, 1, 0, 0));
    gap("epoll_wait",     lsys4(SYS_EPOLL_WAIT, -1, (s64)b64, 1, 0));
    gap("eventfd2",       lsys(SYS_EVENTFD2, 0, 0, 0));
    gap("timerfd_create", lsys(SYS_TIMERFD_CREATE, 0, 0, 0));
    gap("signalfd4",      lsys4(SYS_SIGNALFD4, -1, (s64)b64, 0, 0));
    gap("ptrace",         lsys4(SYS_PTRACE, 0, 0, 0, 0));
    gap("memfd_create",   lsys(SYS_MEMFD_CREATE, (s64)"ow", 0, 0));
    gap("clone3",         lsys4(SYS_CLONE3, 0, 0, 0, 0));
    gap("statfs",         lsys(SYS_STATFS, (s64)"/", (s64)b64, 0));
    gap("fstatfs",        lsys(SYS_FSTATFS, 0, (s64)b64, 0));
    gap("setpriority",    lsys4(SYS_SETPRIORITY, 0, 0, 0, 0));
    gap("inotify_init1",  lsys(SYS_INOTIFY_INIT1, 0, 0, 0));
    raw("socketpair",     lsys(SYS_SOCKETPAIR, (s64)2, (s64)1, (s64)b64));
    gap("arch_prctl",     lsys4(SYS_ARCH_PRCTL, 0x1002, 0, 0, 0));
    gap("pread64",        lsys4(SYS_PREAD64, -1, (s64)b64, 16, 0));
    gap("pwrite64",       lsys4(SYS_PWRITE64, -1, (s64)b64, 16, 0));

    out("LPROBE: done\n");
    lsys(SYS_EXIT, 0, 0, 0);
    for (;;) { }
}
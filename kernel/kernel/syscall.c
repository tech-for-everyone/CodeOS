#include "syscall.h"
#include "../arch/x86_64/io.h"
#include "sched.h"
#include "kprintf.h"
#include "idt.h"
#include "pmm.h"
#include "vmm.h"
#include "string.h"
#include "umode.h"
#include "fs.h"
#include "ext2.h"
#include "x11_server.h"
#include "../drivers/timer.h"
#include "../drivers/zircon_ipc.h"
#include "../drivers/ac97.h"
#include "audio_mixer.h"
#include "../arch/x86_64/serial.h"
#include "version.h"
#include "process.h"
#include "elf.h"
#include "shell.h"
#include "android.h"
#include "container.h"
#include "namespace.h"
#include "vm_manager.h"
#include "mm.h"
#include "rng.h"
#include "ai.h"
#include "apphost.h"
#include "user_wm.h"
#include "ow_http.h"
#include "socket.h"
#include "../arch/x86_64/fb.h"
#include "../drivers/keyboard.h"
#include "../drivers/mouse.h"

extern uint64_t syscall_kernel_rsp;
extern void user_mode_force_return(void);

#ifdef __aarch64__
#define TASK_SIZE 0x0000FFFFFFFFFFFFULL
#else
#define TASK_SIZE 0x7FFFFFFFFFFFULL
#endif

#define access_ok(addr, size) \
    ((uint64_t)(addr) + (size) >= (uint64_t)(addr) && \
     (uint64_t)(addr) + (size) <= TASK_SIZE)

/* struct stat size (x86_64) — single source of truth for all stat syscalls. */
#define LINUX_STAT_SZ 144

/* Native open(2) flags (mirrors Linux x86_64 values) */
#define O_RDONLY     0x0
#define O_WRONLY     0x1
#define O_RDWR       0x2
#define O_CREAT      0x40
#define O_TRUNC      0x200
#define O_APPEND     0x400

#define LINUX_READ       0
#define LINUX_WRITE      1
#define LINUX_OPEN       2
#define LINUX_CLOSE      3
#define LINUX_STAT       4
#define LINUX_FSTAT      5
#define LINUX_MMAP       9
#define LINUX_MPROTECT  10
#define LINUX_MUNMAP    11
#define LINUX_BRK       12
#define LINUX_RT_SIGPROCMASK 14
#define LINUX_IOCTL     16
#define LINUX_GETDENTS64 217
#define LINUX_EXIT      60
#define LINUX_EXIT_GROUP 231
#define LINUX_UNAME     63
#define LINUX_ARCH_PRCTL 158
#define LINUX_GETCWD    79
#define LINUX_CHDIR     80
#define LINUX_GETPID    39
#define LINUX_GETUID    102
#define LINUX_GETGID    104
#define LINUX_NANOSLEEP 35
#define LINUX_WRITEV      20
#define LINUX_LSEEK       8
#define LINUX_SCHED_YIELD 24
#define LINUX_GETDENTS    78
#define LINUX_OPENAT      257
#define LINUX_NEWFSTAT    137
#define LINUX_FACCESSAT   269
#define LINUX_READLINK    89
#define LINUX_READLINKAT  267
#define LINUX_CLOCK_GETTIME   228
#define LINUX_GETTIMEOFDAY   96

#define LINUX_RT_SIGACTION 13
#define LINUX_ACCESS       21
#define LINUX_DUP          32
#define LINUX_DUP2         33
#define LINUX_FCNTL        72
#define LINUX_GETTID       186
#define LINUX_FUTEX        202
#define LINUX_MADVISE      233
#define LINUX_GETRUSAGE    165
#define LINUX_PRCTL        157
#define LINUX_SET_TID_ADDRESS 218
#define LINUX_SET_ROBUST_LIST   273
#define LINUX_GETRANDOM    318
#define LINUX_PRLIMIT64    302
#define LINUX_PIPE2        293
#define LINUX_PREAD64      17
#define LINUX_PWRITE64     18
#define LINUX_READLINKAT   267
#define LINUX_STATX        332

/* Additional Linux syscalls used by real glibc/musl binaries */
#define LINUX_KILL        62
#define LINUX_GETPPID     64
#define LINUX_GETEUID     107
#define LINUX_GETEGID     108
#define LINUX_TGKILL      234
#define LINUX_NEWFSTATAT  262
#define LINUX_CLONE       56
#define LINUX_SETGID      106
#define LINUX_SETUID      105
#define LINUX_SIGALTSTACK 131

/* Linux socket syscalls (x86_64 direct numbers) */
#define LINUX_SOCKET        41
#define LINUX_CONNECT       42
#define LINUX_ACCEPT        43
#define LINUX_SENDTO        44
#define LINUX_RECVFROM      45
#define LINUX_SHUTDOWN      48
#define LINUX_BIND          49
#define LINUX_LISTEN        50
#define LINUX_GETSOCKNAME   51
#define LINUX_GETPEERNAME   52
#define LINUX_SETSOCKOPT    54
#define LINUX_GETSOCKOPT    55

/* Linux socket fds are handed out as LINUX_SOCKFD_BASE+idx so they can never
 * collide with the in-memory-file fd_table (which uses 3..511). */
#define LINUX_SOCKFD_BASE   1000
#define LINUX_SOCKFD_MAX    48
static int ltx_sockfd[LINUX_SOCKFD_MAX];
static int ltx_sockpid[LINUX_SOCKFD_MAX];
static int ltx_sock_inited;

static void ltx_sock_init(void) {
    if (ltx_sock_inited) return;
    for (int i = 0; i < LINUX_SOCKFD_MAX; i++) ltx_sockfd[i] = -1;
    ltx_sock_inited = 1;
}

static int ltx_sock_translate(int lfd) {
    int idx = lfd - LINUX_SOCKFD_BASE;
    if (idx < 0 || idx >= LINUX_SOCKFD_MAX) return -1;
    if (ltx_sockfd[idx] < 0) return -1;
    process_t *p = current_process;
    if (p && ltx_sockpid[idx] != p->pid) return -1;
    return ltx_sockfd[idx];
}

/* Linux sockaddr_in -> kernel sockaddr_t (ip@0, host-order port@4).
 * Both layouts carry the dotted-quad in the same 4 memory bytes, so the
 * address bytes are copied verbatim (kernel IP fields are wire-format). */
static void ltx_addr_to_kernel(const uint8_t *uaddr, int addrlen,
                               sockaddr_t *out) {
    memset(out, 0, sizeof(*out));
    if (addrlen < 16 || !uaddr) return;
    out->sa_data[0] = uaddr[4];
    out->sa_data[1] = uaddr[5];
    out->sa_data[2] = uaddr[6];
    out->sa_data[3] = uaddr[7];
    *(uint16_t *)&out->sa_data[4] = (uint16_t)((uaddr[2] << 8) | uaddr[3]);
}

/* Kernel sockaddr_t -> Linux sockaddr_in (family, BE port, address bytes). */
static void ltx_kernel_to_addr(const sockaddr_t *ks, uint8_t *uaddr) {
    uint16_t port = *(const uint16_t *)&ks->sa_data[4];
    memset(uaddr, 0, 16);
    uaddr[0] = 2; uaddr[1] = 0;
    uaddr[2] = (uint8_t)(port >> 8); uaddr[3] = (uint8_t)(port & 0xFF);
    uaddr[4] = ks->sa_data[0];
    uaddr[5] = ks->sa_data[1];
    uaddr[6] = ks->sa_data[2];
    uaddr[7] = ks->sa_data[3];
}

/* Linux errno values returned as negative from syscalls */
#define LINUX_EPERM    1
#define LINUX_ENOENT   2
#define LINUX_EIO      5
#define LINUX_ENOMEM   12
#define LINUX_EACCES   13
#define LINUX_ENOTDIR  20
#define LINUX_EINVAL   22
#define LINUX_ENAMETOOLONG 36
#define LINUX_ENOSYS   38
#define LINUX_EWOULDBLOCK  11 /* EAGAIN */
#define LINUX_EBADF         9
#define LINUX_EFAULT       14
#define LINUX_EAFNOSUPPORT 97
#define LINUX_EMFILE       24

#define MAX_FDS 256

/* ── mmap region tracking for proper munmap support ── */
#define MMAP_MAX_REGIONS 64
static struct {
    uint64_t vaddr;
    uint64_t pages;
    int      used;
} mmap_regions[MMAP_MAX_REGIONS];

static int mmap_region_add(uint64_t vaddr, uint64_t pages) {
    for (int i = 0; i < MMAP_MAX_REGIONS; i++) {
        if (!mmap_regions[i].used) {
            mmap_regions[i].vaddr = vaddr;
            mmap_regions[i].pages = pages;
            mmap_regions[i].used = 1;
            return 0;
        }
    }
    return -1;
}

static int mmap_region_find(uint64_t vaddr, uint64_t *out_pages) {
    for (int i = 0; i < MMAP_MAX_REGIONS; i++) {
        if (mmap_regions[i].used && mmap_regions[i].vaddr == vaddr) {
            if (out_pages) *out_pages = mmap_regions[i].pages;
            return i;
        }
    }
    return -1;
}

static void mmap_region_remove(int idx) {
    if (idx >= 0 && idx < MMAP_MAX_REGIONS)
        mmap_regions[idx].used = 0;
}

static struct {
    char path[FS_PATH_MAX];
    char data[FS_CONTENT_MAX];
    int len;
    int pos;
    int flags;
    int used;
    int owner_pid;
} fd_table[MAX_FDS];

static int fd_alloc(void) {
    int pid = proc_getpid();
    for (int i = 3; i < MAX_FDS; i++)
        if (!fd_table[i].used) {
            fd_table[i].used = 1;
            fd_table[i].pos = 0;
            fd_table[i].data[0] = 0;
            fd_table[i].len = 0;
            fd_table[i].owner_pid = pid;
            return i;
        }
    return -1;
}

static int fd_verify(int fd) {
    return (fd >= 0 && fd < MAX_FDS && fd_table[fd].used &&
            fd_table[fd].owner_pid == proc_getpid());
}

#define FD_CHECK(fd) do { if (!fd_verify(fd)) return -LINUX_EINVAL; } while(0)

static int copy_from_user(void *dst, uint64_t user_src, uint64_t len) {
    if (!access_ok(user_src, len)) return -1;
    uint64_t start_page = user_src & ~0xFFF;
    uint64_t end_page = len > 0 ? (user_src + len - 1) & ~0xFFF : start_page;
    for (uint64_t p = start_page; p <= end_page; p += 0x1000) {
        uint64_t flags;
        if (vmm_get_mapping(p, 0, &flags) < 0 || !(flags & PAGE_USER))
            return -1;
    }
#ifdef __aarch64__
    for (uint64_t i = 0; i < len; i++)
        ((uint8_t*)dst)[i] = *(volatile uint8_t*)(user_src + i);
#else
    {
        int _smap = cpu_smap_enabled();
        if (_smap) __asm__ volatile("stac" : : : "memory");
        __asm__ volatile("rep movsb"
                         : : "S"(user_src), "D"(dst), "c"(len) : "memory");
        if (_smap) __asm__ volatile("clac" : : : "memory");
    }
#endif
    return 0;
}

static int copy_to_user(uint64_t user_dst, void *src, uint64_t len) {
    if (!access_ok(user_dst, len)) return -1;
    uint64_t start_page = user_dst & ~0xFFF;
    uint64_t end_page = len > 0 ? (user_dst + len - 1) & ~0xFFF : start_page;
    for (uint64_t p = start_page; p <= end_page; p += 0x1000) {
        uint64_t flags;
        if (vmm_get_mapping(p, 0, &flags) < 0 || !(flags & PAGE_USER))
            return -1;
    }
#ifdef __aarch64__
    for (uint64_t i = 0; i < len; i++)
        *(volatile uint8_t*)(user_dst + i) = ((uint8_t*)src)[i];
#else
    {
        int _smap = cpu_smap_enabled();
        if (_smap) __asm__ volatile("stac" : : : "memory");
        __asm__ volatile("rep movsb"
                         : : "S"(src), "D"(user_dst), "c"(len) : "memory");
        if (_smap) __asm__ volatile("clac" : : : "memory");
    }
#endif
    return 0;
}

/* ── Shared BRK implementation (native + Linux personality) ── */
static uint64_t do_brk(uint64_t new_brk, int enomem) {
    process_t *p = current_process;
    if (!p) return 0;

    if (new_brk == 0)
        return p->brk;

    if (new_brk > PROC_BRK_MAX)
        return p->brk;

    if (new_brk > p->brk) {
        uint64_t old_end = (p->brk + PAGE_SIZE - 1) & PAGE_MASK;
        uint64_t new_end = (new_brk + PAGE_SIZE - 1) & PAGE_MASK;
        for (uint64_t a = old_end; a < new_end; a += PAGE_SIZE) {
            uint64_t page_phys = (uint64_t)pmm_alloc_page();
            if (!page_phys) return enomem ? -LINUX_ENOMEM : -1;
            memset((void *)phys_to_virt(page_phys), 0, PAGE_SIZE);
            if (vmm_map_page(a, page_phys, PAGE_PRESENT | PAGE_WRITE | PAGE_USER) < 0)
                return enomem ? -LINUX_ENOMEM : -1;
        }
    } else if (new_brk < p->brk) {
        uint64_t new_end = (new_brk + PAGE_SIZE - 1) & PAGE_MASK;
        uint64_t old_end = (p->brk + PAGE_SIZE - 1) & PAGE_MASK;
        for (uint64_t a = new_end; a < old_end; a += PAGE_SIZE) {
            uint64_t phys = 0;
            if (vmm_get_mapping(a, &phys, 0) == 0 && phys) {
                vmm_unmap_page(a);
                pmm_free_page(phys);
            }
        }
    }

    p->brk = new_brk;
    return p->brk;
}

static int sys_open(const char *user_path, int flags) {
    char path[FS_PATH_MAX];
    if (copy_from_user(path, (uint64_t)user_path, FS_PATH_MAX - 1) < 0)
        return -1;
    path[FS_PATH_MAX - 1] = 0;
    int fd = fd_alloc();
    if (fd < 0) return -1;
    fd_table[fd].flags = flags;
    strcpy(fd_table[fd].path, path);
    int n = fs_read(path, fd_table[fd].data, FS_CONTENT_MAX);
    if (n < 0) {
        n = ext2_read_file_path(path, fd_table[fd].data, FS_CONTENT_MAX);
        if (n <= 0) {
            if ((flags & O_CREAT) && fs_mkfile(path) == 0) {
                n = 0;
            } else {
                fd_table[fd].used = 0;
                return -1;
            }
        }
    }
    fd_table[fd].len = n;
    fd_table[fd].pos = (flags & O_APPEND) ? n : 0;
    return fd;
}

static int64_t sys_write_file(int fd, uint64_t user_buf, int count) {
    if (!fd_verify(fd)) return -1;
    if (!(fd_table[fd].flags & (O_WRONLY | O_RDWR))) return -1;
    if (count <= 0) return 0;
    if (count > FS_CONTENT_MAX) count = FS_CONTENT_MAX;
    int wpos = (fd_table[fd].flags & O_APPEND) ? fd_table[fd].len : fd_table[fd].pos;
    if (wpos < 0) wpos = 0;
    int room = FS_CONTENT_MAX - wpos;
    if (room <= 0) return 0;
    if (count > room) count = room;
    if (copy_from_user(fd_table[fd].data + wpos, user_buf, count) < 0)
        return -1;
    if (wpos + count > fd_table[fd].len) fd_table[fd].len = wpos + count;
    fd_table[fd].pos = wpos + count;
    fs_write(fd_table[fd].path, fd_table[fd].data, fd_table[fd].len);
    return count;
}

static int64_t sys_lseek(int fd, int64_t off, int whence) {
    if (!fd_verify(fd)) return -1;
    int64_t new_pos;
    switch (whence) {
    case 0: new_pos = off; break;
    case 1: new_pos = fd_table[fd].pos + off; break;
    case 2: new_pos = fd_table[fd].len + off; break;
    default: return -1;
    }
    if (new_pos < 0) new_pos = 0;
    if (new_pos > FS_CONTENT_MAX) new_pos = FS_CONTENT_MAX;
    fd_table[fd].pos = (int)new_pos;
    return new_pos;
}

static int64_t sys_read(int fd, uint64_t user_buf, uint64_t count) {
    if (fd == 0) {
        if (apphost_active()) {
            uint8_t buf[256];
            uint64_t n = count > sizeof(buf) ? sizeof(buf) : count;
            if (n == 0) return 0;
            /* Terminal semantics: block until at least one byte is available
             * (or the app is being torn down), matching serial_readchar() on
             * the synchronous path. Returning 0 mid-line would make console
             * REPLs discard the partial line. */
            for (;;) {
                int r = apphost_read_in(buf, (int)n);
                if (r > 0) {
                    if (copy_to_user(user_buf, buf, r) < 0) return -1;
                    return r;
                }
                if (!apphost_active()) return 0;
                /* After a teardown request, report EOF rather than blocking:
                 * the host is closing the app and will not inject more input. */
                if (apphost_killed()) return 0;
                sched_sleep_ms(5);
            }
        }
        uint8_t buf[256];
        int max_read = count > sizeof(buf) ? sizeof(buf) : count;
        int i = 0;
        for (; i < max_read; i++) {
            int c = serial_readchar();
            if (c < 0) break;
            buf[i] = (uint8_t)c;
            if (c == '\n' || c == '\r') { i++; break; }
        }
        if (i > 0 && copy_to_user(user_buf, buf, i) < 0)
            return -1;
        return i;
    }
    if (fd_verify(fd)) {
        int available = fd_table[fd].len - fd_table[fd].pos;
        if (available <= 0) return 0;
        if ((int64_t)count > available) count = (uint64_t)available;
        if (copy_to_user(user_buf, fd_table[fd].data + fd_table[fd].pos, count) < 0)
            return -1;
        fd_table[fd].pos += (int)count;
        return (int64_t)count;
    }
    process_t *p = current_process;
    if (p && fd >= 0 && fd < PROC_FD_MAX && p->fds[fd].pipe) {
        proc_fd_t *pfd = &p->fds[fd];
        if (pfd->type != 1) return -1;
        uint8_t buf[PROC_PIPE_BUF];
        int n = pipe_read(pfd->pipe, buf, (int)count);
        if (n > 0 && copy_to_user(user_buf, buf, n) < 0)
            return -1;
        return n;
    }
    return -1;
}

int64_t kernel_read(int fd, void *buf, int count) {
    if (fd == 0) {
        if (apphost_active()) {
            uint8_t kbuf[256];
            int kbuf_size = (int)sizeof(kbuf);
            int r = apphost_read_in(kbuf, count > kbuf_size ? kbuf_size : count);
            if (r > 0) memcpy(buf, kbuf, r);
            return r;
        }
        int i = 0;
        for (; i < count && i < 255; i++) {
            int c = serial_readchar();
            if (c < 0) break;
            ((uint8_t *)buf)[i] = (uint8_t)c;
            if (c == '\n' || c == '\r') { i++; break; }
        }
        return i;
    }
    process_t *p = current_process;
    if (p && fd >= 0 && fd < PROC_FD_MAX && p->fds[fd].pipe) {
        proc_fd_t *pfd = &p->fds[fd];
        if (pfd->type != 0) return -1;
        uint8_t kbuf[PROC_PIPE_BUF];
        int n = pipe_read(pfd->pipe, kbuf, count);
        if (n > 0) memcpy(buf, kbuf, n);
        return n;
    }
    if (fd >= 0 && fd < MAX_FDS && fd_table[fd].used) {
        int available = fd_table[fd].len - fd_table[fd].pos;
        if (available <= 0) return 0;
        if (count > available) count = available;
        memcpy(buf, fd_table[fd].data + fd_table[fd].pos, count);
        fd_table[fd].pos += count;
        return count;
    }
    return -1;
}

int64_t kernel_write(int fd, const void *buf, int count) {
    process_t *p = current_process;
    if (!p || fd < 0 || fd >= PROC_FD_MAX) return -1;
    proc_fd_t *pfd = &p->fds[fd];
    if (!pfd->pipe || pfd->type != 2) return -1;
    uint8_t kbuf[PROC_PIPE_BUF];
    int total = 0;
    while (total < count) {
        int chunk = count - total;
        if (chunk > PROC_PIPE_BUF) chunk = PROC_PIPE_BUF;
        memcpy(kbuf, (const uint8_t *)buf + total, chunk);
        int n = pipe_write(pfd->pipe, kbuf, chunk);
        if (n < 0) return total > 0 ? total : -1;
        total += n;
    }
    return total;
}

static void linux_handle_write(uint64_t fd, uint64_t buf, uint64_t len) {
    if (fd >= 3 || len == 0) return;
    if (len > 65536) len = 65536;
    char tmp[1024];
    while (len > 0) {
        uint64_t chunk = len > sizeof(tmp) ? sizeof(tmp) : len;
        if (copy_from_user(tmp, buf, chunk) < 0)
            return;
        if (apphost_active()) {
            apphost_write_out((uint8_t *)tmp, (int)chunk);
            buf += chunk;
            len -= chunk;
            continue;
        }
        for (uint64_t i = 0; i < chunk; i++)
            kprintf("%c", tmp[i]);
        buf += chunk;
        len -= chunk;
    }
}

static void linux_handle_exit(int status) {
    if (user_mode_active()) {
        user_mode_end_from_exit(status);
        return;
    }
    sched_exit(status);
    while (1) asm volatile("cli; hlt");
}

static void linux_handle_exit_group(int status) {
    linux_handle_exit(status);
}

/* ─── Linux stat helpers ─── */
#define LINUX_STAT_DEV    0x0801
#define LINUX_S_IFMT      0xF000
#define LINUX_S_IFDIR     0x4000
#define LINUX_S_IFREG     0x8000
#define LINUX_S_IRWXU     00700
#define LINUX_S_IRUSR     00400
#define LINUX_S_IWUSR     00200
#define LINUX_S_IXUSR     00100
#define LINUX_S_IRGRP     00040
#define LINUX_S_IROTH     00004
#define LINUX_DT_UNKNOWN  0
#define LINUX_DT_DIR      4
#define LINUX_DT_REG      8
#define LINUX_AT_FDCWD   (-100)
#define LINUX_TCGETS      0x5401

struct linux_dirent64 {
    uint64_t  d_ino;
    int64_t   d_off;
    unsigned short d_reclen;
    unsigned char  d_type;
    char      d_name[];
} __attribute__((packed));

struct linux_iovec {
    uint64_t iov_base;
    uint64_t iov_len;
};

static int do_stat_fill(const char *path, uint8_t *buf) {
    int size, is_dir;
    if (fs_get_info(path, &size, &is_dir) == 0) {
        size = is_dir ? 4096 : size;
        goto fill;
    }
    if (ext2_mounted()) {
        ext2_dirent_t ent;
        if (ext2_find(path, &ent) >= 0 && ent.valid) {
            struct ext2_inode inode;
            if (ext2_read_inode(ent.inode, &inode) >= 0) {
                size = ent.is_dir ? 4096 : (int)inode.size;
                is_dir = ent.is_dir;
                *(uint64_t*)(buf + 0)  = LINUX_STAT_DEV;
                *(uint64_t*)(buf + 8)  = (uint64_t)ent.inode;
                *(uint64_t*)(buf + 16) = inode.links_count;
                *(uint32_t*)(buf + 24) = (inode.mode & 0xFFFF) | (is_dir ? LINUX_S_IFDIR : LINUX_S_IFREG);
                *(uint32_t*)(buf + 28) = inode.uid;
                *(uint32_t*)(buf + 32) = inode.gid;
                *(uint32_t*)(buf + 36) = 0;
                *(int64_t*)(buf + 48)  = (int64_t)size;
                *(int64_t*)(buf + 56)  = 4096;
                *(int64_t*)(buf + 64)  = (inode.size + 511) / 512;
                *(uint64_t*)(buf + 72) = inode.atime;
                *(uint64_t*)(buf + 80) = 0;
                *(uint64_t*)(buf + 88) = inode.mtime;
                *(uint64_t*)(buf + 96) = 0;
                *(uint64_t*)(buf + 104)= inode.ctime;
                *(uint64_t*)(buf + 112)= 0;
                return 0;
            }
        }
    }
    return -1;

fill:
    memset(buf, 0, 144);
    *(uint64_t*)(buf + 0)  = LINUX_STAT_DEV;
    *(uint64_t*)(buf + 8)  = 1;
    *(uint64_t*)(buf + 16) = 1;
    *(uint32_t*)(buf + 24) = (is_dir ? LINUX_S_IFDIR : LINUX_S_IFREG) | LINUX_S_IRUSR | LINUX_S_IWUSR | LINUX_S_IRGRP | LINUX_S_IROTH;
    *(uint32_t*)(buf + 28) = 0;
    *(uint32_t*)(buf + 32) = 0;
    *(int64_t*)(buf + 48)  = (int64_t)size;
    *(int64_t*)(buf + 56)  = 4096;
    *(int64_t*)(buf + 64)  = (size + 511) / 512;
    return 0;
}

/* ─── getdents64: populate fd's data buffer with linux_dirent64 entries ─── */
static int populate_dirents(const char *path, uint8_t *buf, int max) {
    int off = 0;

    /* Helper to add one dirent. Returns 0 on success, -1 if buffer full. */
    #define ADD_DIRENT(_ino, _type, _name) do { \
        int _nl = strlen(_name); \
        int _reclen = (int)(sizeof(struct linux_dirent64) + _nl + 1 + 7) & ~7; \
        if (off + _reclen > max) { goto done; } \
        struct linux_dirent64 *_de = (struct linux_dirent64 *)(buf + off); \
        _de->d_ino = (_ino); \
        _de->d_reclen = _reclen; \
        _de->d_type = (_type); \
        memcpy(_de->d_name, _name, _nl + 1); \
        off += _reclen; \
    } while(0)

    ADD_DIRENT(1, LINUX_DT_DIR, ".");
    ADD_DIRENT(1, LINUX_DT_DIR, "..");

    /* Try initramfs */
    {
        char names[512][FS_NAME_MAX];
        int n = fs_listdir(path, names, 512);
        if (n > 0) {
            for (int i = 0; i < n; i++) {
                int sz;
                char fp[FS_PATH_MAX];
                int pl = strlen(path);
                memcpy(fp, path, pl);
                if (pl > 0 && fp[pl-1] != '/') fp[pl++] = '/';
                int nl = strlen(names[i]);
                memcpy(fp + pl, names[i], nl + 1);
                int is_dir = 0;
                fs_get_info(fp, &sz, &is_dir);
                ADD_DIRENT((uint64_t)(2 + i), is_dir ? LINUX_DT_DIR : LINUX_DT_REG, names[i]);
            }
            goto done;
        }
    }

    /* Try ext2 */
    if (ext2_mounted()) {
        ext2_dirent_t self;
        if (ext2_find(path, &self) >= 0 && self.valid && self.is_dir) {
            struct ext2_inode inode;
            if (ext2_read_inode(self.inode, &inode) >= 0) {
                uint8_t dir_buf[4096];
                int dir_size = inode.size < 4096 ? inode.size : 4096;
                if (ext2_read_file(self.inode, dir_buf, dir_size, 0) >= 0) {
                    int d_off = 0;
                    while (d_off < dir_size) {
                        struct ext2_dirent *de = (struct ext2_dirent *)(dir_buf + d_off);
                        if (de->inode == 0 || de->rec_len == 0) { d_off += de->rec_len ? de->rec_len : 1; continue; }
                        int nl = de->name_len < 255 ? de->name_len : 255;
                        char ename[256];
                        memcpy(ename, de->name, nl); ename[nl] = 0;
                        if (strcmp(ename, ".") == 0 || strcmp(ename, "..") == 0) { d_off += de->rec_len; continue; }
                        ADD_DIRENT(de->inode,
                            de->file_type == 2 ? LINUX_DT_DIR : LINUX_DT_REG,
                            ename);
                        d_off += de->rec_len;
                    }
                }
            }
        }
    }

done:
    /* Patch d_off: set each entry's d_off to the byte offset of the next entry */
    {
        int pos = 0;
        while (pos < off) {
            struct linux_dirent64 *de = (struct linux_dirent64 *)(buf + pos);
            int next = pos + de->d_reclen;
            de->d_off = (next < off) ? (int64_t)next : 0;
            pos = next;
        }
    }

    return off;
    #undef ADD_DIRENT
}

/* ─── Modified sys_open that handles directories ─── */
static int linux_sys_open_path(const char *path, int flags) {
    int fd = fd_alloc();
    if (fd < 0) return -LINUX_ENOMEM;
    fd_table[fd].flags = flags;
    strcpy(fd_table[fd].path, path);

    /* Try as regular file (initramfs) */
    int n = fs_read(path, fd_table[fd].data, FS_CONTENT_MAX);
    if (n >= 0) {
        fd_table[fd].len = n;
        return fd;
    }

    /* Try as regular file (ext2) */
    n = ext2_read_file_path(path, fd_table[fd].data, FS_CONTENT_MAX);
    if (n >= 0) {
        fd_table[fd].len = n;
        return fd;
    }

    /* Try as directory — populate dirents */
    if (ext2_mounted()) {
        ext2_dirent_t ent;
        if (ext2_find(path, &ent) >= 0 && ent.valid && ent.is_dir) {
            int sz = populate_dirents(path, (uint8_t*)fd_table[fd].data, FS_CONTENT_MAX);
            if (sz > 0) {
                fd_table[fd].len = sz;
                return fd;
            }
        }
    }
    {
        int sz, d;
        if (fs_get_info(path, &sz, &d) == 0 && d) {
            int sz2 = populate_dirents(path, (uint8_t*)fd_table[fd].data, FS_CONTENT_MAX);
            if (sz2 > 0) {
                fd_table[fd].len = sz2;
                return fd;
            }
        }
    }

    fd_table[fd].used = 0;
    return -LINUX_ENOENT;
}

static int linux_sys_open(const char *user_path, int flags) {
    char path[FS_PATH_MAX];
    if (copy_from_user(path, (uint64_t)user_path, FS_PATH_MAX - 1) < 0)
        return -LINUX_EINVAL;
    path[FS_PATH_MAX - 1] = 0;
    return linux_sys_open_path(path, flags);
}

int64_t linux_syscall_handler(uint64_t n, uint64_t a1, uint64_t a2, uint64_t a3,
                               uint64_t a4, uint64_t a5, uint64_t a6,
                               uint64_t uip, uint64_t ufl) {
    (void)a4; (void)a5; (void)a6; (void)uip; (void)ufl;

    switch (n) {
    case LINUX_WRITE:
        linux_handle_write(a1, a2, a3);
        return a3;
    case LINUX_READ:
        return (uint64_t)sys_read((int)a1, a2, a3);
    case LINUX_OPEN:
        return (uint64_t)linux_sys_open((const char *)a1, (int)a2);
    case LINUX_CLOSE: {
        int cfd = (int)a1;
        int sock = ltx_sock_translate(cfd);
        if (sock >= 0) {
            int idx = cfd - LINUX_SOCKFD_BASE;
            ltx_sockfd[idx] = -1;
            ltx_sockpid[idx] = 0;
            return socket_close(sock);
        }
        if (fd_verify(cfd)) { memset(&fd_table[cfd], 0, sizeof(fd_table[cfd])); }
        proc_fd_close(cfd);
        return 0;
    }
    case LINUX_SOCKET: {
        ltx_sock_init();
        if ((int)a1 != 2) return -LINUX_EAFNOSUPPORT; /* AF_INET only */
        int idx = -1;
        for (int i = 0; i < LINUX_SOCKFD_MAX; i++) { if (ltx_sockfd[i] < 0) { idx = i; break; } }
        if (idx < 0) return -LINUX_EMFILE;
        int s = socket_create((int)a1, (int)a2, (int)a3);
        if (s < 0) return -LINUX_EMFILE;
        ltx_sockfd[idx] = s;
        process_t *p = current_process;
        ltx_sockpid[idx] = p ? p->pid : 0;
        return LINUX_SOCKFD_BASE + idx;
    }
    case LINUX_BIND:
    case LINUX_CONNECT: {
        int sock = ltx_sock_translate((int)a1);
        if (sock < 0) return -LINUX_EBADF;
        sockaddr_t saddr;
        ltx_addr_to_kernel((const uint8_t *)a2, (int)a3, &saddr);
        int r = (n == LINUX_BIND)
                    ? socket_bind(sock, &saddr, (int)a3)
                    : socket_connect(sock, &saddr, (int)a3);
        return r < 0 ? -LINUX_EINVAL : 0;
    }
    case LINUX_SENDTO: {
        int sock = ltx_sock_translate((int)a1);
        if (sock < 0) return -LINUX_EBADF;
        if (a3 <= 0 || a3 > 1024) return -LINUX_EINVAL;
        uint8_t sndbuf[1024];
        if (copy_from_user(sndbuf, a2, a3) < 0) return -LINUX_EFAULT;
        sockaddr_t dst;
        ltx_addr_to_kernel((const uint8_t *)a5, (int)a6 ? (int)a6 : 16, &dst);
        int r = socket_sendto(sock, sndbuf, (int)a3, (int)a4, &dst,
                              a6 ? (int)a6 : (int)sizeof(sockaddr_t));
        return r < 0 ? -LINUX_EINVAL : r;
    }
    case LINUX_RECVFROM: {
        int sock = ltx_sock_translate((int)a1);
        if (sock < 0) return -LINUX_EBADF;
        if (a3 <= 0 || a3 > 1024) return -LINUX_EINVAL;
        uint8_t rcvbuf[1024];
        sockaddr_t src;
        int r = socket_recvfrom_nb(sock, rcvbuf, (int)a3, (int)a4,
                                   a5 ? &src : 0, a6 ? (int *)a6 : (int *)&src);
        if (r < 0) return -LINUX_EWOULDBLOCK;
        if (copy_to_user(a2, rcvbuf, (uint64_t)r) < 0) return -LINUX_EFAULT;
        if (a5 && a6) {
            uint8_t lu[16];
            ltx_kernel_to_addr(&src, lu);
            if (copy_to_user(a5, lu, 16) < 0) return -LINUX_EFAULT;
        }
        return r;
    }
    case LINUX_GETSOCKNAME:
    case LINUX_GETPEERNAME: {
        int sock = ltx_sock_translate((int)a1);
        if (sock < 0) return -LINUX_EBADF;
        sockaddr_t saddr;
        int addrlen = sizeof(saddr);
        int r = (n == LINUX_GETSOCKNAME)
                    ? socket_getsockname(sock, &saddr, &addrlen)
                    : socket_getpeername(sock, &saddr, &addrlen);
        if (r < 0) return -LINUX_EINVAL;
        uint8_t lu[16];
        ltx_kernel_to_addr(&saddr, lu);
        if (copy_to_user(a2, lu, 16) < 0) return -LINUX_EFAULT;
        if (a3) {
            int outlen = 16;
            if (copy_to_user(a3, &outlen, sizeof(outlen)) < 0) return -LINUX_EFAULT;
        }
        return 0;
    }
    case LINUX_SETSOCKOPT: {
        int sock = ltx_sock_translate((int)a1);
        if (sock < 0) return -LINUX_EBADF;
        char val[256];
        if (a5 > 256) return -LINUX_EINVAL;
        if (a4 && a5) { if (copy_from_user(val, a4, a5) < 0) return -LINUX_EFAULT; }
        return socket_setsockopt(sock, (int)a2, (int)a3, a4 ? val : 0, (int)a5);
    }
    case LINUX_GETSOCKOPT: {
        int sock = ltx_sock_translate((int)a1);
        if (sock < 0) return -LINUX_EBADF;
        return 0;
    }
    case LINUX_LISTEN:
    case LINUX_ACCEPT:
    case LINUX_SHUTDOWN:
        return -LINUX_ENOSYS;
    case LINUX_EXIT:
        linux_handle_exit((int)a1);
        return 0;
    case LINUX_EXIT_GROUP:
        linux_handle_exit_group((int)a1);
        return 0;
    case LINUX_BRK: {
        process_t *proc = current_process;
        if (!proc) return -LINUX_ENOMEM;
        if (a1 == 0) {
            if (proc->brk == 0) proc->brk = PROC_BRK_BASE;
            return proc->brk;
        }
        return do_brk(a1, 1);
    }
    case LINUX_MMAP: {
        uint64_t addr = a1;
        uint64_t len  = a2;
        int prot   = (int)a3;
        int flags  = (int)a4;
        (void)flags;
        if (len == 0) return -LINUX_EINVAL;
        uint64_t base = (addr == 0) ? 0x7F00000000ULL : (addr & ~0xFFFULL);
        uint64_t end  = (base + len + 0xFFF) & ~0xFFFULL;

        uint64_t vmm_flags = PAGE_PRESENT | PAGE_USER;
        if (prot & 2) vmm_flags |= PAGE_WRITE;
        if (!(prot & 4)) vmm_flags |= PAGE_NX;

        uint64_t cur = base;
        while (cur < end) {
            uint64_t page_phys = (uint64_t)pmm_alloc_page();
            if (!page_phys) return -LINUX_ENOMEM;
            memset((void*)phys_to_virt(page_phys), 0, 0x1000);
            if (vmm_map_page(cur, page_phys, vmm_flags) < 0)
                return -LINUX_ENOMEM;
            cur += 0x1000;
        }
        uint64_t total_pages = (end - base) / 0x1000;
        mmap_region_add(base, total_pages);
        return base;
    }
    case LINUX_MPROTECT:
        return 0;
    case LINUX_MUNMAP: {
        uint64_t addr = a1;
        uint64_t len  = a2;
        if (addr == 0 || len == 0) return 0;
        uint64_t page_start = addr & ~0xFFF;
        (void)len;
        uint64_t found_pages = 0;
        int idx = mmap_region_find(page_start, &found_pages);
        if (idx >= 0) {
            for (uint64_t i = 0; i < found_pages; i++) {
                uint64_t virt = page_start + i * 0x1000;
                uint64_t phys = 0;
                if (vmm_get_mapping(virt, &phys, 0) == 0 && phys) {
                    vmm_unmap_page(virt);
                    pmm_free_page(phys);
                }
            }
            mmap_region_remove(idx);
        }
        return 0;
    }
    case LINUX_RT_SIGPROCMASK:
        return 0;
    case LINUX_UNAME: {
        struct { char sysname[65]; char nodename[65]; char release[65]; char version[65]; char machine[65]; } info;
        int i;
        for (i = 0; KERNEL_NAME[i] && i < 64; i++) info.sysname[i] = KERNEL_NAME[i];
        info.sysname[i] = 0;
        for (i = 0; "codeos"[i] && i < 64; i++) info.nodename[i] = "codeos"[i];
        info.nodename[i] = 0;
        for (i = 0; KERNEL_VERSION[i] && i < 64; i++) info.release[i] = KERNEL_VERSION[i];
        info.release[i] = 0;
        for (i = 0; KERNEL_VERSION_STR[i] && i < 64; i++) info.version[i] = KERNEL_VERSION_STR[i];
        info.version[i] = 0;
        for (i = 0; KERNEL_ARCH[i] && i < 64; i++) info.machine[i] = KERNEL_ARCH[i];
        info.machine[i] = 0;
        if (copy_to_user(a1, &info, sizeof(info)) < 0) return -LINUX_ENOMEM;
        return 0;
    }
    case LINUX_ARCH_PRCTL:
        /* A Linux process may set its userspace TLS via ARCH_SET_FS.
         * This kernel has no swapgs and uses FS itself for its own TLS
         * (boot.S sets IA32_FS_BASE=__tls_end), so writing the MSR here
         * would destroy the kernel's TLS and fault later Qt/desktop code.
         * Accept the request without touching the machine state. */
        return 0;
    /* ── NEW: stat/fstat/newfstatat ── */
    case LINUX_STAT: {
        uint8_t stbuf[LINUX_STAT_SZ];
        char path[FS_PATH_MAX];
        if (copy_from_user(path, a1, FS_PATH_MAX - 1) < 0) return -LINUX_EINVAL;
        path[FS_PATH_MAX - 1] = 0;
        if (do_stat_fill(path, stbuf) < 0) return -LINUX_ENOENT;
        if (copy_to_user(a2, stbuf, LINUX_STAT_SZ) < 0) return -LINUX_ENOMEM;
        return 0;
    }
    case LINUX_FSTAT: {
        int fd = (int)a1;
        uint8_t stbuf[LINUX_STAT_SZ];
        FD_CHECK(fd);
        if (do_stat_fill(fd_table[fd].path, stbuf) < 0) return -LINUX_ENOENT;
        if (copy_to_user(a2, stbuf, LINUX_STAT_SZ) < 0) return -LINUX_ENOMEM;
        return 0;
    }
    case LINUX_NEWFSTAT: {
        /* newfstatat: dirfd, path, statbuf, flags */
        int dirfd = (int)a1;
        char path[FS_PATH_MAX];
        if (a2) {
            if (copy_from_user(path, a2, FS_PATH_MAX - 1) < 0) return -LINUX_EINVAL;
            path[FS_PATH_MAX - 1] = 0;
        } else {
            if (dirfd != LINUX_AT_FDCWD && fd_verify(dirfd)) {
                strcpy(path, fd_table[dirfd].path);
            } else {
                path[0] = '/'; path[1] = 0;
            }
        }
        uint8_t stbuf[LINUX_STAT_SZ];
        if (do_stat_fill(path, stbuf) < 0) return -LINUX_ENOENT;
        if (copy_to_user(a3, stbuf, LINUX_STAT_SZ) < 0) return -LINUX_ENOMEM;
        return 0;
    }
    case LINUX_LSEEK: {
        int fd = (int)a1;
        int64_t off = (int64_t)a2;
        int whence = (int)a3;
        FD_CHECK(fd);
        int new_pos;
        switch (whence) {
        case 0: new_pos = (int)off; break;                /* SEEK_SET */
        case 1: new_pos = fd_table[fd].pos + (int)off; break; /* SEEK_CUR */
        case 2: new_pos = fd_table[fd].len + (int)off; break; /* SEEK_END */
        default: return -LINUX_EINVAL;
        }
        if (new_pos < 0) new_pos = 0;
        fd_table[fd].pos = new_pos;
        return (uint64_t)new_pos;
    }
    case LINUX_IOCTL: {
        int fd = (int)a1;
        unsigned long request = a2;
        (void)a3;
        FD_CHECK(fd);
        if (request == LINUX_TCGETS) {
            /* Return a minimal termios that looks like a TTY */
            uint8_t termios[44];
            memset(termios, 0, sizeof(termios));
            /* Set c_cflag: B38400 | CS8 | CREAD | CLOCAL */
            *(unsigned int*)(termios + 8) = 0x000010bf;
            /* Set c_lflag: ECHO | ICANON | ISIG */
            *(unsigned int*)(termios + 12) = 0x0000038b;
            if (copy_to_user(a3, termios, 44) < 0) return -LINUX_ENOMEM;
            return 0;
        }
        return -LINUX_ENOSYS;
    }
    /* ── WRITEV ── */
    case LINUX_WRITEV: {
        int fd = (int)a1;
        if (fd >= 3) return 0;
        struct linux_iovec iov;
        uint64_t iov_ptr = a2;
        int iovcnt = (int)a3;
        if (iovcnt > 128) iovcnt = 128;
        for (int i = 0; i < iovcnt; i++) {
            if (copy_from_user(&iov, iov_ptr + i * sizeof(iov), sizeof(iov)) < 0)
                break;
            uint64_t len = iov.iov_len;
            if (len > 65536) len = 65536;
            char tmp[1024];
            while (len > 0) {
                uint64_t chunk = len > sizeof(tmp) ? sizeof(tmp) : len;
                if (copy_from_user(tmp, iov.iov_base, chunk) < 0) break;
                for (uint64_t j = 0; j < chunk; j++) kprintf("%c", tmp[j]);
                iov.iov_base += chunk;
                len -= chunk;
            }
        }
        return a2 ? 0 : 0;
    }
    /* ── SCHED_YIELD ── */
    case LINUX_SCHED_YIELD:
        sched_yield();
        return 0;
    /* ── NANOSLEEP ── */
    case LINUX_NANOSLEEP: {
        uint64_t ts_user = a1;
        uint64_t rem_user = a2;
        uint64_t sec, nsec;
        if (copy_from_user(&sec, ts_user, 8) < 0) return -LINUX_EINVAL;
        if (copy_from_user(&nsec, ts_user + 8, 8) < 0) return -LINUX_EINVAL;
        uint64_t ms = sec * 1000 + nsec / 1000000;
        if (ms > 0) sched_sleep_ms(ms);
        if (rem_user) {
            uint64_t zero = 0;
            copy_to_user(rem_user, &zero, 8);
            copy_to_user(rem_user + 8, &zero, 8);
        }
        return 0;
    }
    /* ── CLOCK_GETTIME ── */
    case LINUX_CLOCK_GETTIME: {
        int clk_id = (int)a1;
        uint64_t tp = a2;
        uint64_t sec = 0, nsec = 0;
        if (clk_id == 0 || clk_id == 1) {
            /* CLOCK_REALTIME or CLOCK_MONOTONIC */
            uint64_t ms = timer_get_milliseconds();
            sec = ms / 1000;
            nsec = (ms % 1000) * 1000000;
        }
        uint8_t ts[16];
        *(uint64_t*)(ts + 0) = sec;
        *(uint64_t*)(ts + 8) = nsec;
        if (copy_to_user(tp, ts, 16) < 0) return -LINUX_ENOMEM;
        return 0;
    }
    /* ── GETTIMEOFDAY ── */
    case LINUX_GETTIMEOFDAY: {
        uint64_t tv = a1;
        uint64_t tz = a2;
        if (tv) {
            uint64_t ms = timer_get_milliseconds();
            uint8_t val[16];
            *(uint64_t*)(val + 0) = ms / 1000;
            *(uint64_t*)(val + 8) = (ms % 1000) * 1000;
            if (copy_to_user(tv, val, 16) < 0) return -LINUX_ENOMEM;
        }
        if (tz) {
            uint8_t tz_buf[8];
            memset(tz_buf, 0, 8);
            copy_to_user(tz, tz_buf, 8);
        }
        return 0;
    }
    /* ── GETPID ── */
    case LINUX_GETPID:
        return proc_getpid() > 0 ? (uint64_t)proc_getpid() : 1;
    /* ── GETUID / GETGID ── */
    case LINUX_GETUID:
        return 0;
    case LINUX_GETGID:
        return 0;
    /* ── GETCWD ── */
    case LINUX_GETCWD: {
        char buf[FS_PATH_MAX];
        fs_getcwd(buf, FS_PATH_MAX);
        int len = strlen(buf) + 1;
        if ((int)a2 < len) return (uint64_t)(-LINUX_EINVAL);
        if (copy_to_user(a1, buf, len) < 0) return -LINUX_ENOMEM;
        return a1;
    }
    /* ── CHDIR ── */
    case LINUX_CHDIR: {
        char path[FS_PATH_MAX];
        if (copy_from_user(path, a1, FS_PATH_MAX - 1) < 0) return -LINUX_EINVAL;
        path[FS_PATH_MAX - 1] = 0;
        if (fs_cd(path) < 0) return -LINUX_ENOENT;
        return 0;
    }
    /* ── READLINK ── */
    case LINUX_READLINK: {
        /* For now, only handle /proc/self/exe → return the binary path */
        char path[FS_PATH_MAX];
        if (copy_from_user(path, a1, FS_PATH_MAX - 1) < 0) return -LINUX_EINVAL;
        path[FS_PATH_MAX - 1] = 0;
        (void)a3;
        if (strcmp(path, "/proc/self/exe") == 0) {
            const char *reply = "/bin/container";
            int rl = strlen(reply) + 1;
            if (copy_to_user(a2, (void*)reply, rl) < 0) return -LINUX_ENOMEM;
            return rl - 1;
        }
        return -LINUX_ENOENT;
    }
    case LINUX_GETDENTS64: {
        int fd = (int)a1;
        uint64_t user_buf = a2;
        unsigned int count = (unsigned int)a3;
        FD_CHECK(fd);
        int avail = fd_table[fd].len - fd_table[fd].pos;
        if (avail <= 0) return 0;
        if (count > (unsigned int)avail) count = (unsigned int)avail;
        if (copy_to_user(user_buf, fd_table[fd].data + fd_table[fd].pos, count) < 0)
            return -LINUX_ENOMEM;
        fd_table[fd].pos += (int)count;
        return (uint64_t)count;
    }
    case LINUX_GETDENTS: {
        /* Same as getdents64 — a few fields differ but callers accept it */
        int fd = (int)a1;
        uint64_t user_buf = a2;
        unsigned int count = (unsigned int)a3;
        FD_CHECK(fd);
        int avail = fd_table[fd].len - fd_table[fd].pos;
        if (avail <= 0) return 0;
        if (count > (unsigned int)avail) count = (unsigned int)avail;
        if (copy_to_user(user_buf, fd_table[fd].data + fd_table[fd].pos, count) < 0)
            return -LINUX_ENOMEM;
        fd_table[fd].pos += (int)count;
        return (uint64_t)count;
    }
    /* ── OPENAT ── */
    case LINUX_OPENAT: {
        int dirfd = (int)a1;
        char user_path[FS_PATH_MAX];
        char path[FS_PATH_MAX];
        if (copy_from_user(user_path, a2, FS_PATH_MAX - 1) < 0)
            return -LINUX_EINVAL;
        user_path[FS_PATH_MAX - 1] = 0;

        if (user_path[0] == '/') {
            strcpy(path, user_path);
        } else if (dirfd != LINUX_AT_FDCWD && fd_verify(dirfd)) {
            int base_len = strlen(fd_table[dirfd].path);
            int name_len = strlen(user_path);
            if (base_len + 1 + name_len >= FS_PATH_MAX)
                return -LINUX_ENAMETOOLONG;
            strcpy(path, fd_table[dirfd].path);
            if (base_len == 0 || path[base_len - 1] != '/')
                path[base_len++] = '/';
            strcpy(path + base_len, user_path);
        } else {
            /* Relative AT_FDCWD paths are resolved by the filesystem layer. */
            strcpy(path, user_path);
        }
        return (uint64_t)linux_sys_open_path(path, (int)a3);
    }
    /* ── FACESSAT ── */
    case LINUX_FACCESSAT: {
        int dirfd = (int)a1;
        (void)dirfd;
        int mode = (int)a3;
        (void)mode;
        char path[FS_PATH_MAX];
        if (copy_from_user(path, a2, FS_PATH_MAX - 1) < 0) return -LINUX_EINVAL;
        path[FS_PATH_MAX - 1] = 0;
        int sz, d;
        if (fs_get_info(path, &sz, &d) == 0) return 0;
        if (ext2_mounted()) {
            ext2_dirent_t ent;
            if (ext2_find(path, &ent) >= 0 && ent.valid) return 0;
        }
        return -LINUX_ENOENT;
    }
    /* ── RT_SIGACTION ── */
    case LINUX_RT_SIGACTION:
        /* Stub: accept any signal handler setup */
        return 0;
    /* ── ACCESS ── */
    case LINUX_ACCESS: {
        char path[FS_PATH_MAX];
        if (copy_from_user(path, a1, FS_PATH_MAX - 1) < 0) return -LINUX_EINVAL;
        path[FS_PATH_MAX - 1] = 0;
        int sz, d;
        if (fs_get_info(path, &sz, &d) == 0) return 0;
        if (ext2_mounted()) {
            ext2_dirent_t ent;
            if (ext2_find(path, &ent) >= 0 && ent.valid) return 0;
        }
        return -LINUX_ENOENT;
    }
    /* ── DUP / DUP2 ── */
    case LINUX_DUP: {
        int oldfd = (int)a1;
        if (!fd_verify(oldfd)) return -(uint64_t)LINUX_EINVAL;
        int newfd = fd_alloc();
        if (newfd < 0) return -(uint64_t)LINUX_EINVAL;
        fd_table[newfd] = fd_table[oldfd];
        return (uint64_t)newfd;
    }
    case LINUX_DUP2: {
        int oldfd = (int)a1;
        int newfd = (int)a2;
        if (!fd_verify(oldfd) || newfd < 0 || newfd >= MAX_FDS)
            return -(uint64_t)LINUX_EINVAL;
        fd_table[newfd] = fd_table[oldfd];
        return (uint64_t)newfd;
    }
    /* ── FCNTL ── */
    case LINUX_FCNTL: {
        int cmd = (int)a2;
        (void)cmd;
        return 0;
    }
    /* ── GETTID ── */
    case LINUX_GETTID:
        return proc_getpid() > 0 ? (uint64_t)proc_getpid() : 1;
    /* ── FUTEX ── */
    case LINUX_FUTEX: {
        int op = (int)(a2 & 0xFF);
        if (op == 0 || op == 1) {
            /* FUTEX_WAIT: don't actually block */
            return 0;
        }
        if (op == 2) {
            /* FUTEX_REQUEUE */
            return 0;
        }
        return -LINUX_ENOSYS;
    }
    /* ── MADVISE ── */
    case LINUX_MADVISE:
        return 0;
    /* ── GETRUSAGE ── */
    case LINUX_GETRUSAGE: {
        int who = (int)a1;
        (void)who;
        uint64_t usage = a2;
        if (!usage) return 0;
        uint8_t rusage[144];
        memset(rusage, 0, sizeof(rusage));
        if (copy_to_user(usage, rusage, sizeof(rusage)) < 0)
            return -LINUX_ENOMEM;
        return 0;
    }
    /* ── PRCTL ── */
    case LINUX_PRCTL:
        return 0;
    /* ── SET_TID_ADDRESS ── */
    case LINUX_SET_TID_ADDRESS:
        return proc_getpid() > 0 ? (uint64_t)proc_getpid() : 1;
    /* ── SET_ROBUST_LIST ── */
    case LINUX_SET_ROBUST_LIST:
        return 0;
    /* ── GETRANDOM ── */
    case LINUX_GETRANDOM: {
        uint64_t buf = a1;
        size_t len = (size_t)a2;
        unsigned int flags = (unsigned int)a3;
        (void)flags;
        if (!buf || len == 0) return 0;
        if (len > 256) len = 256;
        uint8_t tmp[256];
        for (size_t i = 0; i < len; i++) tmp[i] = (uint8_t)rng_next();
        if (copy_to_user(buf, tmp, len) < 0) return -LINUX_ENOMEM;
        return (int64_t)len;
    }
    /* ── PRLIMIT64 ── */
    case LINUX_PRLIMIT64: {
        int pid = (int)a1;
        int resource = (int)a2;
        uint64_t new_limit = a3;
        uint64_t old_limit = a4;
        (void)pid;
        (void)new_limit;
        if (old_limit) {
            uint8_t rlim[16];
            memset(rlim, 0, sizeof(rlim));
            /* Return RLIM_INFINITY for all limits */
            *(uint64_t*)(rlim + 0) = ~0ULL;
            *(uint64_t*)(rlim + 8) = ~0ULL;
            if (copy_to_user(old_limit, rlim, sizeof(rlim)) < 0)
                return -LINUX_ENOMEM;
        }
        (void)resource;
        return 0;
    }
    /* ── PIPE2 ── */
    case LINUX_PIPE2: {
        if (!a1) return -LINUX_EINVAL;
        int fds[2];
        if (pipe_create(fds) < 0) return -LINUX_ENOMEM;
        if (copy_to_user(a1, fds, sizeof(fds)) < 0) return -LINUX_ENOMEM;
        return 0;
    }
    /* ── PREAD64 ── */
    case LINUX_PREAD64: {
        int fd = (int)a1;
        uint64_t buf = a2;
        size_t count = (size_t)a3;
        uint64_t offset = a4;
        FD_CHECK(fd);
        if (offset > 0x7FFFFFFF) return -LINUX_EINVAL;
        fd_table[fd].pos = (int)offset;
        int avail = fd_table[fd].len - fd_table[fd].pos;
        if (avail <= 0) return 0;
        if ((int)count > avail) count = (size_t)avail;
        if (copy_to_user(buf, fd_table[fd].data + fd_table[fd].pos, count) < 0)
            return -LINUX_ENOMEM;
        return (int64_t)count;
    }
    /* ── PWRITE64 ── */
    case LINUX_PWRITE64: {
        int fd = (int)a1;
        (void)fd;
        /* No-op for now */
        return (int64_t)a3;
    }
    /* ── KILL (62) ── */
    case LINUX_KILL: {
        int pid = (int)a1;
        int sig = (int)a2;
        return proc_kill(pid, sig);
    }
    /* ── TGKILL (234) ── */
    case LINUX_TGKILL: {
        /* tgkill(tgid, tid, sig) */
        int pid = (int)a2;
        int sig = (int)a3;
        return proc_kill(pid, sig);
    }
    /* ── GETPPID (64) ── */
    case LINUX_GETPPID:
        return (uint64_t)proc_getppid();
    /* ── GETEUID (107) / GETEGID (108) ── */
    case LINUX_GETEUID:
        return current_process ? (uint64_t)current_process->euid : 0;
    case LINUX_GETEGID:
        return current_process ? (uint64_t)current_process->egid : 0;
    /* ── SETUID (105) / SETGID (106) ── */
    case LINUX_SETUID:
        if (current_process) current_process->euid = (int)a1;
        return 0;
    case LINUX_SETGID:
        if (current_process) current_process->egid = (int)a1;
        return 0;
    /* ── SIGALTSTACK (131) ── */
    case LINUX_SIGALTSTACK:
        /* Accept and ignore altstack setup */
        return 0;
    /* ── READLINKAT (267) ── */
    case LINUX_READLINKAT: {
        int dirfd = (int)a1;
        char path[FS_PATH_MAX];
        if (copy_from_user(path, a2, FS_PATH_MAX - 1) < 0) return -LINUX_EINVAL;
        path[FS_PATH_MAX - 1] = 0;
        if (dirfd != LINUX_AT_FDCWD && path[0] != '/') {
            /* Relative to dirfd — resolve via CWD for now */
        }
        if (strcmp(path, "/proc/self/exe") == 0) {
            const char *reply = "/bin/container";
            int rl = strlen(reply) + 1;
            if (copy_to_user(a3, (void *)reply, rl) < 0) return -LINUX_ENOMEM;
            return rl - 1;
        }
        return -LINUX_ENOENT;
    }
    /* ── NEWFSTATAT (262) ── */
    case LINUX_NEWFSTATAT: {
        int dirfd = (int)a1;
        uint64_t pathbuf = a2;
        uint64_t statbuf = a3;
        uint64_t flags = a4;
        (void)flags;
        char path[FS_PATH_MAX];
        if (pathbuf) {
            if (copy_from_user(path, pathbuf, FS_PATH_MAX - 1) < 0) return -LINUX_EINVAL;
            path[FS_PATH_MAX - 1] = 0;
        } else {
            if (dirfd != LINUX_AT_FDCWD && fd_verify(dirfd)) {
                strcpy(path, fd_table[dirfd].path);
            } else {
                path[0] = '/'; path[1] = 0;
            }
        }
        uint8_t stbuf[LINUX_STAT_SZ];
        if (do_stat_fill(path, stbuf) < 0) return -LINUX_ENOENT;
        if (copy_to_user(statbuf, stbuf, LINUX_STAT_SZ) < 0) return -LINUX_ENOMEM;
        return 0;
    }
    /* ── STATX ── */
    case LINUX_STATX: {
        int dirfd = (int)a1;
        (void)dirfd;
        /* Return minimal statx result for root */
        uint8_t stx[256];
        memset(stx, 0, sizeof(stx));
        *(uint16_t*)(stx + 0) = 0x00F; /* stx_mask = basic attrs */
        *(uint16_t*)(stx + 8) = 0x000; /* stx_blksize */
        *(uint64_t*)(stx + 16) = 4096;  /* stx_nlink - just 1 dir */
        *(uint32_t*)(stx + 40) = 0;     /* stx_uid */
        *(uint32_t*)(stx + 44) = 0;     /* stx_gid */
        *(uint16_t*)(stx + 48) = 0x41ED; /* stx_mode = 0755 */
        if (a4 && copy_to_user(a4, stx, sizeof(stx)) < 0)
            return -LINUX_ENOMEM;
        return 0;
    }
    /* ── FORK ── */
    case 57: {
        int pid = proc_fork();
        if (pid < 0) return -LINUX_ENOMEM;
        return (uint64_t)pid;
    }
    /* ── EXECVE ── */
    case 59: {
        char path[FS_PATH_MAX];
        if (copy_from_user(path, a1, FS_PATH_MAX - 1) < 0) return -LINUX_EINVAL;
        path[FS_PATH_MAX - 1] = 0;
        uint64_t entry, stack;
        elf_auxv_info_t auxv;
        if (elf_load(path, &entry, &stack, &auxv) < 0) return -LINUX_ENOENT;
        /* Linux execve: a1=path, a2=argv ptr, a3=envp ptr — count argv by walking */
        int argc = 0;
        if (a2) {
            uint64_t ptr;
            while (argc < 63) {
                if (copy_from_user(&ptr, a2 + argc * 8, 8) < 0 || !ptr) break;
                argc++;
            }
        }
        char *argv[64];
        uint64_t user_argv[64];
        if (a2 && argc > 0) {
            if (copy_from_user(user_argv, a2, argc * 8) < 0) return -LINUX_EINVAL;
            for (int i = 0; i < argc && i < 63; i++) {
                char argbuf[256];
                if (copy_from_user(argbuf, user_argv[i], 255) < 0) continue;
                argbuf[255] = 0;
                argv[i] = (char *)pmm_alloc_page();
                if (argv[i]) {
                    memset((void *)phys_to_virt((uint64_t)argv[i]), 0, 0x1000);
                    strcpy((void *)phys_to_virt((uint64_t)argv[i]), argbuf);
                    argv[i] = (char *)phys_to_virt((uint64_t)argv[i]);
                }
            }
            argv[argc] = 0;
        } else {
            argv[0] = (char *)path;
            argv[1] = 0;
        }
        uint64_t rsp = proc_exec(entry, stack, argc > 0 ? argc : 1, argv, 0, &auxv);
        /* Free argv pages — data already copied to user stack by elf_setup_stack */
        if (a2 && argc > 0) {
            for (int i = 0; i < argc && i < 63; i++) {
                if (argv[i] && argv[i] != (char *)path) {
                    pmm_free_page(virt_to_phys((uint64_t)argv[i]));
                }
            }
        }
        if (!rsp) return -LINUX_ENOMEM;
        user_mode_set_return(shell_exec_done);
        user_mode_begin();
        thread_t *cur = sched_current();
        if (cur && cur->syscall_stack_top)
            syscall_kernel_rsp = (uint64_t)cur->syscall_stack_top;
        user_mode_enter(entry, rsp);
        return 0;
    }
    /* ── CLONE (simplified: just fork) ── */
    case 56: {
        unsigned long clone_flags = a1;
        (void)clone_flags;
        int pid = proc_fork();
        if (pid < 0) return -LINUX_ENOMEM;
        return (uint64_t)pid;
    }
    /* ── WAIT4 / WAITPID ── */
    case 61: {
        int status = 0;
        int pid = proc_wait((int)a1, &status);
        if (pid < 0) return -LINUX_ENOENT;
        if (a2) {
            if (copy_to_user(a2, &status, sizeof(int)) < 0) return -LINUX_ENOMEM;
        }
        return (uint64_t)pid;
    }
    /* ── GETPPID ── */
    case 110:
        return (uint64_t)proc_getppid();
    /* ── SELECT (stub: return ready on stdin) ── */
    case 23: {
        int nfds = (int)a1;
        if (nfds < 0) nfds = 0;
        if (nfds > 1024) nfds = 1024;
        /* Just return 1 (stdin ready) as a minimal stub */
        if (a2) {
            /* Zero out readfds except fd 0 */
            uint8_t fdset[128];
            memset(fdset, 0, sizeof(fdset));
            if (copy_to_user(a2, fdset, (nfds + 7) / 8) < 0) return -LINUX_ENOMEM;
        }
        return 1;
    }
    /* ── POLL (stub) ── */
    case 7: {
        (void)a1; (void)a2; (void)a3;
        return 1;
    }
    /* ── EPOLL_CREATE1 (stub) ── */
    case 291: {
        (void)a1;
        int fd = fd_alloc();
        if (fd < 0) return -LINUX_ENOMEM;
        return (uint64_t)fd;
    }
    /* ── EPOLL_WAIT (stub) ── */
    case 232:
        return 0;
    /* ── SENDMSG/RECVMSG/SOCKETPAIR (not implemented) ── */
    case 46:
    case 47:
    case 53:
        return -LINUX_ENOSYS;
    default:
        return -LINUX_ENOSYS;
    }
}

/* ── Fork support: copy parent's linux fds to child ── */
void linux_fd_fork_update(int parent_pid, int child_pid) {
    for (int i = 0; i < MAX_FDS; i++) {
        if (fd_table[i].used && fd_table[i].owner_pid == parent_pid) {
            /* Duplicate the entry for the child */
            int child_fd = fd_alloc();
            if (child_fd < 0) break;
            memcpy(&fd_table[child_fd], &fd_table[i], sizeof(fd_table[0]));
            fd_table[child_fd].owner_pid = child_pid;
            fd_table[child_fd].pos = 0; /* child starts reading from beginning */
        }
    }
}

/* ── Filesystem syscalls ── */
static int64_t sys_stat(const char *user_path, uint64_t user_statbuf) {
    char path[FS_PATH_MAX];
    if (copy_from_user(path, (uint64_t)user_path, FS_PATH_MAX - 1) < 0)
        return -1;
    path[FS_PATH_MAX - 1] = 0;

    int size = 0;
    int is_dir = 0;
    if (fs_get_info(path, &size, &is_dir) < 0)
        return -1;

    struct {
        int size;
        int is_dir;
    } st;
    st.size = size;
    st.is_dir = is_dir;

    if (copy_to_user(user_statbuf, &st, sizeof(st)) < 0)
        return -1;
    return 0;
}

static int64_t sys_chdir(const char *user_path) {
    char path[FS_PATH_MAX];
    if (copy_from_user(path, (uint64_t)user_path, FS_PATH_MAX - 1) < 0)
        return -1;
    path[FS_PATH_MAX - 1] = 0;

    if (fs_cd(path) < 0)
        return -1;
    return 0;
}

static int64_t sys_getcwd(char *user_buf, int max) {
    if (max <= 0) return -1;
    char buf[FS_PATH_MAX];
    fs_getcwd(buf, FS_PATH_MAX);
    if (copy_to_user((uint64_t)user_buf, buf, max > FS_PATH_MAX ? FS_PATH_MAX : max) < 0)
        return -1;
    return 0;
}

static int64_t sys_mkdir(const char *user_path) {
    char path[FS_PATH_MAX];
    if (copy_from_user(path, (uint64_t)user_path, FS_PATH_MAX - 1) < 0)
        return -1;
    path[FS_PATH_MAX - 1] = 0;

    if (fs_mkdir(path) < 0)
        return -1;
    return 0;
}

static int64_t sys_rmdir(const char *user_path) {
    char path[FS_PATH_MAX];
    if (copy_from_user(path, (uint64_t)user_path, FS_PATH_MAX - 1) < 0)
        return -1;
    path[FS_PATH_MAX - 1] = 0;

    if (fs_rmdir(path) < 0)
        return -1;
    return 0;
}

static int64_t sys_unlink(const char *user_path) {
    char path[FS_PATH_MAX];
    if (copy_from_user(path, (uint64_t)user_path, FS_PATH_MAX - 1) < 0)
        return -1;
    path[FS_PATH_MAX - 1] = 0;

    if (fs_rm(path) < 0)
        return -1;
    return 0;
}

static int64_t sys_rename(const char *user_old, const char *user_new) {
    char oldp[FS_PATH_MAX], newp[FS_PATH_MAX];
    if (copy_from_user(oldp, (uint64_t)user_old, FS_PATH_MAX - 1) < 0)
        return -1;
    if (copy_from_user(newp, (uint64_t)user_new, FS_PATH_MAX - 1) < 0)
        return -1;
    oldp[FS_PATH_MAX - 1] = 0;
    newp[FS_PATH_MAX - 1] = 0;

    if (fs_rename(oldp, newp) < 0)
        return -1;
    return 0;
}

static int64_t sys_readdir(const char *user_path, char *user_names, int max_entries) {
    if (max_entries <= 0) return -1;
    char path[FS_PATH_MAX];
    if (copy_from_user(path, (uint64_t)user_path, FS_PATH_MAX - 1) < 0)
        return -1;
    path[FS_PATH_MAX - 1] = 0;

    char names[128][FS_NAME_MAX];
    int count = fs_listdir(path, names, 128);
    if (count < 0) return -1;

    int total = count * FS_NAME_MAX;
    if (copy_to_user((uint64_t)user_names, names, total > max_entries * FS_NAME_MAX ? max_entries * FS_NAME_MAX : total) < 0)
        return -1;
    return count;
}

static int64_t sys_chmod(const char *user_path, int mode) {
    (void)user_path; (void)mode;
    return 0; /* stub */
}

static int64_t sys_access(const char *user_path, int mode) {
    char path[FS_PATH_MAX];
    if (copy_from_user(path, (uint64_t)user_path, FS_PATH_MAX - 1) < 0)
        return -1;
    path[FS_PATH_MAX - 1] = 0;

    int size = 0, is_dir = 0;
    if (fs_get_info(path, &size, &is_dir) < 0)
        return -1;
    (void)mode; /* existence check only */
    return 0;
}

/* Kernel-local copies of user-facing structs (no stdint.h issues) */
typedef struct {
    uint64_t addr;
    uint32_t width;
    uint32_t height;
    uint32_t pitch;
    uint8_t  bpp;
    uint8_t  type;
} codeos_fb_info_t;
typedef struct {
    uint64_t reserved0;
    int      type;
    int      key;
    int      mouse_x;
    int      mouse_y;
    int      mouse_buttons;
    uint64_t reserved1;
} codeos_input_ev_t;

/* ── Linux personality syscall number translation ── */
/* When a process has PERSONALITY_LINUX, route directly to linux_syscall_handler
   instead of using a broken translation table. This unifies the two layers. */

static void syscall_sock_ip_to_kernel(sockaddr_t *a) {
    *(uint32_t *)a = __builtin_bswap32(*(uint32_t *)a);
}
static void syscall_sock_ip_to_user(sockaddr_t *a) {
    *(uint32_t *)a = __builtin_bswap32(*(uint32_t *)a);
}

int64_t syscall_handler(uint64_t n, uint64_t a1, uint64_t a2, uint64_t a3,
                        uint64_t a4, uint64_t a5, uint64_t a6) {
    (void)a4; (void)a5; (void)a6;

    /* Personality syscall translation — route directly to linux_syscall_handler */
    if (n == SYSCALL_SET_PERSONALITY) {
        process_t *p = current_process;
        if (!p) return -1;
        p->personality = (int)a1;
        return 0;
    }
    if (current_process && current_process->personality == PERSONALITY_LINUX) {
        return linux_syscall_handler(n, a1, a2, a3, a4, a5, a6, n, a1);
    }

    switch (n) {
    case SYSCALL_WRITE: {
        char tmp[256];
        uint64_t rem = a2;
        uint64_t off = a1;
        if (apphost_active()) {
            int total = 0;
            while (rem > 0) {
                uint64_t chunk = rem > sizeof(tmp) ? sizeof(tmp) : rem;
                if (copy_from_user(tmp, off, chunk) < 0) return -1;
                total += apphost_write_out((uint8_t *)tmp, (int)chunk);
                off += chunk;
                rem -= chunk;
            }
            return total;
        }
        while (rem > 0) {
            uint64_t chunk = rem > sizeof(tmp) ? sizeof(tmp) : rem;
            if (copy_from_user(tmp, off, chunk) < 0) return -1;
            for (uint64_t i = 0; i < chunk; i++) kprintf("%c", tmp[i]);
            off += chunk;
            rem -= chunk;
        }
        return a2;
    }
    case SYSCALL_SLEEP:
        sched_sleep_ms(a1);
        return 0;
    case SYSCALL_EXIT:
        {
            /* Inside a container session the same process hosts pid-1, so a
             * container exit must NOT kill the enclosing user program. */
            process_t *xp = proc_current();
            if (xp && xp->namespaces[NS_TYPE_MOUNT] != 0) {
                user_mode_end_from_exit((int)a1);
                return 0;
            }
        }
        proc_exit((int)a1);
        user_mode_end_from_exit((int)a1);
        return 0;
    case SYSCALL_GETTID:
        if (sched_current()) return sched_current()->tid;
        return -1;
    case SYSCALL_TIME:
        return (int64_t)(timer_get_milliseconds() / 1000);
    case SYSCALL_GETTIMEOFDAY: {
        uint64_t ms = timer_get_milliseconds();
        uint8_t val[16];
        *(uint64_t *)(val + 0) = ms / 1000;
        *(uint64_t *)(val + 8) = (ms % 1000) * 1000;
        if (copy_to_user(a1, val, 16) < 0) return -1;
        return 0;
    }
    case SYSCALL_OPEN:
        return sys_open((const char *)a1, (int)a2);
    case SYSCALL_READ:
        /* Android app event channel (user-window bridge, fd 4). */
        if ((int)a1 == UW_FD_EVT && proc_current() &&
            user_wm_active(proc_current()->pid)) {
            uint8_t kbuf[512];
            int n = user_wm_events_out(proc_current()->pid, kbuf, sizeof(kbuf));
            if (n > 0 && copy_to_user(a2, kbuf, n) < 0) return -1;
            return n;
        }
        return sys_read((int)a1, a2, a3);
    case SYSCALL_LSEEK:
        return sys_lseek((int)a1, (int64_t)a2, (int)a3);
    case SYSCALL_YIELD:
        sched_yield();
        return 0;
    case SYSCALL_FORK: {
        int pid = proc_fork();
        if (pid < 0) return -1;
        if (pid == 0) return 0;
        return pid;
    }
    case SYSCALL_EXECVE: {
        char path[FS_PATH_MAX];
        if (copy_from_user(path, a1, FS_PATH_MAX - 1) < 0) return -1;
        path[FS_PATH_MAX - 1] = 0;
        uint64_t entry, stack;
        elf_auxv_info_t auxv;
        if (elf_load(path, &entry, &stack, &auxv) < 0) return -1;
        int argc = (int)a3;
        if (argc <= 0) argc = 0;
        if (argc > 63) argc = 63;
        char *argv[64];
        uint64_t user_argv[64];
        if (a2 && argc > 0) {
            if (copy_from_user(user_argv, a2, argc * 8) < 0) return -1;
            for (int i = 0; i < argc && i < 63; i++) {
                char argbuf[256];
                if (copy_from_user(argbuf, user_argv[i], 255) < 0) continue;
                argbuf[255] = 0;
                argv[i] = (char *)pmm_alloc_page();
                if (argv[i]) {
                    memset((void *)phys_to_virt((uint64_t)argv[i]), 0, 0x1000);
                    strcpy((void *)phys_to_virt((uint64_t)argv[i]), argbuf);
                    argv[i] = (char *)phys_to_virt((uint64_t)argv[i]);
                }
            }
            argv[argc] = 0;
        } else {
            argv[0] = (char *)path;
            argv[1] = 0;
        }
        uint64_t rsp = proc_exec(entry, stack, argc > 0 ? argc : 1, argv, 0, &auxv);
        /* Free argv pages — data already copied to user stack by elf_setup_stack */
        if (a2 && argc > 0) {
            for (int i = 0; i < argc && i < 63; i++) {
                if (argv[i] && argv[i] != (char *)path) {
                    pmm_free_page(virt_to_phys((uint64_t)argv[i]));
                }
            }
        }
        if (!rsp) return -1;

        user_mode_set_return(shell_exec_done);
        user_mode_begin();
        thread_t *cur = sched_current();
        if (cur && cur->syscall_stack_top)
            syscall_kernel_rsp = (uint64_t)cur->syscall_stack_top;
        user_mode_enter(entry, rsp);
        return 0;
    }
    case SYSCALL_WAIT: {
        int status = 0;
        int pid = proc_wait((int)a1, &status);
        if (pid < 0) return -1;
        if (a2) {
            if (copy_to_user(a2, &status, sizeof(int)) < 0) return -1;
        }
        return pid;
    }
    case SYSCALL_GETPID:
        return proc_getpid();
    case SYSCALL_GETPPID:
        return proc_getppid();
    case SYSCALL_CLOSE:
        return proc_fd_close((int)a1);
    case SYSCALL_PIPE: {
        int fd[2];
        if (pipe_create(fd) < 0) return -1;
        if (copy_to_user(a1, fd, sizeof(fd)) < 0) return -1;
        return 0;
    }
    case SYSCALL_DUP:
        return proc_fd_dup((int)a1);
    case SYSCALL_DUP2:
        return proc_fd_dup2((int)a1, (int)a2);
    case SYSCALL_BRK: {
        process_t *p = current_process;
        if (!p) return -1;
        if (a1 == 0) return p->brk;
        return do_brk(a1, 0);
    }
    case SYSCALL_MMAP: {
        uint64_t addr = a1;
        uint64_t len  = a2;
        int prot   = (int)a3;
        int flags  = (int)a4;
        (void)flags;
        uint64_t vaddr;

        if (addr == 0) {
            process_t *p = current_process;
            if (!p) return -1;
            vaddr = p->mmap_base;
            p->mmap_base += ((len + 0xFFF) & ~0xFFF);
        } else {
            vaddr = addr;
        }

        uint64_t vmm_flags = PAGE_PRESENT | PAGE_USER;
        if (prot & 2) vmm_flags |= PAGE_WRITE;
        if (!(prot & 4)) vmm_flags |= PAGE_NX;

        /* MAP_PHYSICAL: addr is a physical address, map it directly */
        if (flags & MAP_PHYSICAL) {
            uint64_t phys_base = addr;
            uint64_t cur = vaddr;
            while (cur < vaddr + ((len + 0xFFF) & ~0xFFF)) {
                if (vmm_map_page(cur, phys_base, vmm_flags) < 0)
                    return -1;
                cur += 0x1000;
                phys_base += 0x1000;
            }
            return vaddr;
        }

        uint64_t cur = vaddr;
        while (cur < vaddr + ((len + 0xFFF) & ~0xFFF)) {
            uint64_t page_phys = (uint64_t)pmm_alloc_page();
            if (!page_phys) return -1;
            memset((void *)phys_to_virt(page_phys), 0, 0x1000);
            if (vmm_map_page(cur, page_phys, vmm_flags) < 0)
                return -1;
            cur += 0x1000;
        }
        uint64_t total_pages = ((len + 0xFFF) & ~0xFFF) / 0x1000;
        mmap_region_add(vaddr, total_pages);
        return vaddr;
    }
    case SYSCALL_MUNMAP: {
        uint64_t addr = a1;
        uint64_t len  = a2;
        if (addr == 0 || len == 0) return 0;
        uint64_t page_start = addr & ~0xFFF;
        (void)len;
        uint64_t found_pages = 0;
        int idx = mmap_region_find(page_start, &found_pages);
        if (idx >= 0) {
            for (uint64_t i = 0; i < found_pages; i++) {
                uint64_t virt = page_start + i * 0x1000;
                uint64_t phys = 0;
                if (vmm_get_mapping(virt, &phys, 0) == 0 && phys) {
                    vmm_unmap_page(virt);
                    pmm_free_page(phys);
                }
            }
            mmap_region_remove(idx);
        }
        return 0;
    }
    case SYSCALL_SBRK: {
        process_t *p = current_process;
        if (!p) return -1;
        int64_t inc = (int64_t)a1;
        uint64_t old = p->brk;
        if (inc > 0) {
            uint64_t new_end = (old + inc + 0xFFF) & ~0xFFF;
            uint64_t cur = (old + 0xFFF) & ~0xFFF;
            for (; cur < new_end; cur += 0x1000) {
                uint64_t page_phys = (uint64_t)pmm_alloc_page();
                if (!page_phys) return -1;
                memset((void *)phys_to_virt(page_phys), 0, 0x1000);
                if (vmm_map_page(cur, page_phys, PAGE_PRESENT | PAGE_WRITE | PAGE_USER) < 0)
                    return -1;
            }
        } else if (inc < 0) {
            uint64_t new_end = ((uint64_t)(old + inc) + 0xFFF) & ~0xFFF;
            uint64_t old_end = (old + 0xFFF) & ~0xFFF;
            if (new_end < old_end) {
                for (uint64_t pg = new_end; pg < old_end; pg += 0x1000) {
                    uint64_t phys = 0;
                    if (vmm_get_mapping(pg, &phys, 0) == 0 && phys) {
                        vmm_unmap_page(pg);
                        pmm_free_page(phys);
                    }
                }
            }
        }
        p->brk = old + inc;
        return old;
    }
    case SYSCALL_BINDER: {
        binder_transaction_t t;
        if (!a1) return -1;
        if (a2 > sizeof(t.data)) a2 = sizeof(t.data);
        if (copy_from_user(&t, a1, sizeof(binder_transaction_t)) < 0)
            return -1;
        int ret = android_binder_cmd(&t);
        if (copy_to_user(a1, &t, sizeof(binder_transaction_t)) < 0)
            return -1;
        return ret;
    }

    case SYSCALL_ASHMEM: {
        int cmd = (int)a1;
        switch (cmd) {
        case 0: {
            /* ashmem_create: a2=name_ptr, a3=size */
            char name[ANDROID_ASHMEM_NAME_MAX];
            if (a2 && copy_from_user(name, a2, ANDROID_ASHMEM_NAME_MAX - 1) == 0) {
                name[ANDROID_ASHMEM_NAME_MAX - 1] = 0;
                return android_ashmem_create(name, (int)a3, 0);
            }
            return android_ashmem_create(0, (int)a3, 0);
        }
        case 1:
            /* ashmem_get_size: a2=id */
            return android_ashmem_get_size((int)a2);
        case 2: {
            /* ashmem_set_name: a2=id, a4=name_ptr */
            char name[ANDROID_ASHMEM_NAME_MAX];
            if (a4 && copy_from_user(name, a4, ANDROID_ASHMEM_NAME_MAX - 1) == 0) {
                name[ANDROID_ASHMEM_NAME_MAX - 1] = 0;
                return android_ashmem_set_name((int)a2, name);
            }
            return -1;
        }
        default:
            return -1;
        }
    }

    case SYSCALL_CONTAINER_CREATE:
        return container_create((const char *)a1, (const char *)a2);
    case SYSCALL_CONTAINER_START:
        return container_start((int)a1);
    case SYSCALL_CONTAINER_EXEC: {
        int id = (int)a1;
        char path[FS_PATH_MAX];
        if (copy_from_user(path, a2, FS_PATH_MAX - 1) < 0) return -1;
        path[FS_PATH_MAX - 1] = 0;
        int argc = (int)a3;
        uint64_t user_argv = a4;
        char *argv[64];
        uint64_t kargv[64];
        if (argc > 0 && user_argv) {
            if (argc > 63) argc = 63;
            if (copy_from_user(kargv, user_argv, argc * 8) < 0) return -1;
            for (int i = 0; i < argc; i++) {
                char argbuf[256];
                if (copy_from_user(argbuf, kargv[i], 255) < 0) continue;
                argbuf[255] = 0;
                argv[i] = (char *)pmm_alloc_page();
                if (argv[i]) {
                    memset((void *)phys_to_virt((uint64_t)argv[i]), 0, 0x1000);
                    strcpy((void *)phys_to_virt((uint64_t)argv[i]), argbuf);
                    argv[i] = (char *)phys_to_virt((uint64_t)argv[i]);
                }
            }
            argv[argc] = 0;
        } else {
            argv[0] = path;
            argv[1] = 0;
        }
        int ret = container_exec(id, path, argc, argv, 0);
        /* Free argv pages — data already copied to user stack */
        for (int i = 0; i < argc && i < 63; i++) {
            if (argv[i] && argv[i] != path) {
                pmm_free_page(virt_to_phys((uint64_t)argv[i]));
            }
        }
        return ret;
    }
    case SYSCALL_CONTAINER_DESTROY:
        return container_destroy((int)a1);
    case SYSCALL_CONTAINER_LIST: {
        char names[CONTAINER_MAX][CONTAINER_NAME_MAX];
        int n = container_list(names, CONTAINER_MAX);
        if (a1 && n > 0) {
            if (copy_to_user(a1, names, n * CONTAINER_NAME_MAX) < 0) return -1;
        }
        return n;
    }

    case SYSCALL_AI_QUERY: {
        char prompt[512];
        char response[2048];
        if (!a1) return -1;
        if (copy_from_user(prompt, a1, sizeof(prompt) - 1) < 0) return -1;
        prompt[sizeof(prompt) - 1] = 0;
        int len = ai_query(prompt, response, sizeof(response));
        if (len < 0) return -1;
        if (len >= (int)sizeof(response)) len = sizeof(response) - 1;
        if (a2 && len > 0) {
            if (copy_to_user(a2, response, len + 1) < 0) return -1;
        }
        return len;
    }

    case SYSCALL_ZIRCON_IPC: {
        if (a1 == 0) {
            if (zircon_ipc_ring) {
                return (int64_t)virt_to_phys((uint64_t)zircon_ipc_ring);
            }
            return -1;
        }
        if (a1 == 1) {
            if (!a2) return -1;
            zircon_ipc_msg_t msg;
            if (zircon_ipc_recv(&msg)) {
                if (copy_to_user(a2, &msg, sizeof(msg)) < 0)
                    return -1;
                return 1;
            }
            return 0;
        }
        if (a1 == 2) {
            if (!a2) return -1;
            zircon_ipc_msg_t msg;
            if (copy_from_user(&msg, a2, sizeof(msg)) < 0)
                return -1;
            return zircon_ipc_send(&msg) == 0 ? 1 : -1;
        }
        return -1;
    }

    case SYSCALL_FB_INFO: {
        codeos_fb_info_t info;
        info.addr  = fb_get_addr_phys();
        info.width  = fb_getwidth();
        info.height = fb_getheight();
        info.pitch  = fb_get_pitch();
        info.bpp    = fb_get_bpp();
        info.type   = 0;
        if (a1) {
            if (copy_to_user(a1, &info, sizeof(info)) < 0)
                return -1;
        }
        return 0;
    }

    case SYSCALL_INPUT_POLL: {
        codeos_input_ev_t ev;
        ev.type = 0;
        ev.key     = 0;
        ev.mouse_x = mouse_get_x();
        ev.mouse_y = mouse_get_y();
        ev.mouse_buttons = mouse_get_buttons();
        if (keyboard_has_input()) {
            ev.type = 1;
            ev.key  = keyboard_getchar();
        }
        if (a1) {
            if (copy_to_user(a1, &ev, sizeof(ev)) < 0)
                return -1;
        }
        return ev.type;
    }

    case SYSCALL_AUDIO_PLAY: {
        if (!ac97.present) return -1;
        int samples = (int)a2;
        if (samples <= 0 || samples > 2048) return -1;
        int16_t buf[2048];
        if (copy_from_user(buf, a1, samples * 2) < 0)
            return -1;
        return ac97_play_pcm(buf, samples) == 0 ? 0 : -1;
    }

    case SYSCALL_AUDIO_STATUS: {
        if (!ac97.present) return -1;
        return ac97_is_playing() ? 1 : 0;
    }
    case SYSCALL_PWRITE:
        /* Android app command channel (user-window bridge, fd 3). */
        if ((int)a1 == UW_FD_CMD && proc_current() &&
            user_wm_active(proc_current()->pid)) {
            uint8_t kbuf[512];
            int len = (int)a3;
            if (len <= 0) return 0;
            if (len > (int)sizeof(kbuf)) len = (int)sizeof(kbuf);
            if (copy_from_user(kbuf, a2, (uint64_t)len) < 0) return -1;
            return user_wm_msg_in(proc_current()->pid, kbuf, len);
        }
        if (fd_verify((int)a1))
            return sys_write_file((int)a1, a2, (int)a3);
        return kernel_write((int)a1, (const void *)a2, (int)a3);

    case SYSCALL_SHM: {
        int cmd = (int)a1;
        switch (cmd) {
        case 0: /* create */
            return shm_create((int)a2);
        case 1: /* map */
            return (int64_t)shm_map((int)a2);
        default:
            return -1;
        }
    }

    case SYSCALL_GET_INFO: {
        int cmd = (int)a1;
        uint64_t user_buf = a2;
        size_t max_len = (size_t)a3;
        switch (cmd) {
        case 0: { /* kernel version string */
            const char *ver = KERNEL_VERSION_STR;
            int len = strlen(ver) + 1;
            if (user_buf && max_len > 0) {
                if (len > (int)max_len) len = (int)max_len;
                if (copy_to_user(user_buf, (void*)ver, len) < 0)
                    return -1;
            }
            return len;
        }
        case 1: { /* kernel name */
            const char *name = KERNEL_NAME;
            int len = strlen(name) + 1;
            if (user_buf && max_len > 0) {
                if (len > (int)max_len) len = (int)max_len;
                if (copy_to_user(user_buf, (void*)name, len) < 0)
                    return -1;
            }
            return len;
        }
        case 2: /* apphost context: 1 when stdin is the async apphost terminal */
            return apphost_active() ? 1 : 0;
        default:
            return -1;
        }
    }

    case SYSCALL_WEB: {
        int cmd = (int)a1;
        uint64_t user_ptr = a2;
        uint64_t user_max = a3;
        switch (cmd) {
        case WEB_NAVIGATE:
        case WEB_SEARCH:
        case WEB_TAB_NEW: {
            char s[512];
            int n = user_max > 0 ? (user_max < sizeof(s) - 1 ? (int)user_max : (int)sizeof(s) - 1) : 0;
            if (n > 0 && copy_from_user(s, user_ptr, n) < 0) return -1;
            s[n] = 0;
            if (cmd == WEB_NAVIGATE) ow_navigate(s);
            else if (cmd == WEB_SEARCH) ow_search(s);
            else ow_tab_new(n > 0 ? s : 0);
            return 0;
        }
        case WEB_TAB_CLOSE:
            ow_tab_close((int)user_ptr);
            return 0;
        case WEB_TAB_SET_ACTIVE:
            ow_set_tab_active((int)user_ptr);
            return 0;
        case WEB_TAB_ACTIVE_GET:
            return ow_get_tab_active();
        case WEB_TAB_COUNT:
            return ow_get_tab_count();
        case WEB_TAB_USED:
            return ow_tab_used_count();
        case WEB_TAB_PROGRESS:
            return ow_get_load_progress();
        case WEB_GET_INFO: {
            openweb_tab_t *tabs = ow_get_tabs();
            int active = ow_get_tab_active();
            if (!tabs || active < 0 || active >= ow_get_tab_count()) return -1;
            openweb_tab_t *t = &tabs[active];
            web_state_t st;
            memset(&st, 0, sizeof(st));
            strncpy_safe(st.url, t->url, sizeof(st.url));
            strncpy_safe(st.status, t->status, sizeof(st.status));
            st.content_len = t->content_len;
            st.loading = t->loading;
            st.error = t->error;
            st.can_go_back = t->can_go_back;
            st.can_go_forward = t->can_go_forward;
            if (user_ptr && copy_to_user(user_ptr, &st, sizeof(st)) < 0) return -1;
            return 0;
        }
        case WEB_GET_CONTENT: {
            openweb_tab_t *tabs = ow_get_tabs();
            int active = ow_get_tab_active();
            if (!tabs || active < 0 || active >= ow_get_tab_count()) return -1;
            openweb_tab_t *t = &tabs[active];
            int max = (int)user_max;
            if (max > t->content_len) max = t->content_len;
            if (max > 0 && copy_to_user(user_ptr, t->content, max) < 0) return -1;
            return max;
        }
        default:
            return -1;
        }
    }

    case SYSCALL_STAT: {
        return sys_stat((const char *)a1, a2);
    }
    case SYSCALL_CHDIR: {
        return sys_chdir((const char *)a1);
    }
    case SYSCALL_GETCWD: {
        return sys_getcwd((char *)a1, (int)a2);
    }
    case SYSCALL_MKDIR: {
        return sys_mkdir((const char *)a1);
    }
    case SYSCALL_RMDIR: {
        return sys_rmdir((const char *)a1);
    }
    case SYSCALL_UNLINK: {
        return sys_unlink((const char *)a1);
    }
    case SYSCALL_RENAME: {
        return sys_rename((const char *)a1, (const char *)a2);
    }
    case SYSCALL_READDIR: {
        return sys_readdir((const char *)a1, (char *)a2, (int)a3);
    }
    case SYSCALL_CHMOD: {
        return sys_chmod((const char *)a1, (int)a2);
    }
    case SYSCALL_ACCESS: {
        return sys_access((const char *)a1, (int)a2);
    }
    case SYSCALL_X11_CONNECT: {
        int fd = (int)a1;
        int pid = current_process ? current_process->pid : 0;
        int client_id = x11_client_connect(fd, pid);
        if (client_id > 0 && a2) {
            copy_to_user(a2, &client_id, sizeof(int));
        }
        return client_id;
    }
    case SYSCALL_X11_REQUEST: {
        int client_id = (int)a1;
        const uint8_t *req = (const uint8_t *)a2;
        int req_len = (int)a3;
        uint8_t *reply = (uint8_t *)a4;
        int *reply_len = (int *)a5;

        int rlen = 0;
        if (req_len < 1 || req_len > X11_BUF_SIZE) return -1;
        /* Never dereference raw user pointers: copy the request and reply
         * through kernel buffers (heap-allocated to protect syscall stacks). */
        uint8_t *req_buf = (uint8_t *)malloc(X11_BUF_SIZE);
        uint8_t *reply_buf = (uint8_t *)malloc(X11_BUF_SIZE);
        if (!req_buf || !reply_buf) {
            if (req_buf) free(req_buf);
            return -1;
        }
        if (copy_from_user(req_buf, (uint64_t)req, (uint64_t)req_len) < 0) {
            free(req_buf);
            free(reply_buf);
            return -1;
        }

        int ret = x11_client_process_request(client_id, req_buf, req_len,
                                             reply_buf, &rlen);
        if (rlen > 0 && rlen <= X11_BUF_SIZE) {
            if (copy_to_user((uint64_t)reply, reply_buf, (uint64_t)rlen) < 0) {
                free(req_buf);
                free(reply_buf);
                return -1;
            }
        } else {
            rlen = 0;
        }
        free(req_buf);
        free(reply_buf);
        if (reply_len && copy_to_user((uint64_t)reply_len, &rlen, sizeof(int)) < 0)
            return -1;
        return ret;
    }
    case SYSCALL_X11_POLL: {
        (void)a1; (void)a2;
        return 0;
    }
    case SYSCALL_KILL: {
        int pid = (int)a1;
        int sig = (int)a2;
        return proc_kill(pid, sig);
    }
    case SYSCALL_VM: {
        int cmd = (int)a1;
        char *str = (char *)a2;
        switch (cmd) {
        case VM_CMD_LIST: {
            vm_sync_states();
            char names[VM_MAX][VM_NAME_MAX];
            int n = vm_list_all(names, VM_MAX);
            if (n < 0) return -1;
            char out[1024];
            int off = 0;
            for (int i = 0; i < n; i++) {
                vm_t *vm = vm_find(names[i]);
                if (!vm) continue;
                int w = snprintf(out + off, sizeof(out) - off, "%s state=%s type=%d mem=%uM cpus=%d\n",
                                vm->name, vm_state_str(vm->state), (int)vm->type,
                                vm->config.memory_mb, vm->config.cpu_count);
                if (w < 0) break;
                off += w;
                if (off >= (int)sizeof(out) - 1) { off = (int)sizeof(out) - 1; break; }
            }
            out[off] = 0;
            if (copy_to_user((uint64_t)str, out, off + 1) < 0) return -1;
            return n;
        }
        case VM_CMD_CREATE: {
            vm_config_t cfg;
            if (copy_from_user(&cfg, (uint64_t)str, sizeof(cfg)) < 0) return -1;
            return vm_create(&cfg);
        }
        case VM_CMD_START: {
            char name[VM_NAME_MAX];
            if (copy_from_user(name, (uint64_t)str, VM_NAME_MAX - 1) < 0) return -1;
            name[VM_NAME_MAX - 1] = 0;
            vm_t *vm = vm_find(name);
            if (!vm) return -1;
            return vm_start(vm->id);
        }
        case VM_CMD_STOP: {
            char name[VM_NAME_MAX];
            if (copy_from_user(name, (uint64_t)str, VM_NAME_MAX - 1) < 0) return -1;
            name[VM_NAME_MAX - 1] = 0;
            vm_t *vm = vm_find(name);
            if (!vm) return -1;
            return vm_stop(vm->id);
        }
        case VM_CMD_DESTROY: {
            char name[VM_NAME_MAX];
            if (copy_from_user(name, (uint64_t)str, VM_NAME_MAX - 1) < 0) return -1;
            name[VM_NAME_MAX - 1] = 0;
            vm_t *vm = vm_find(name);
            if (!vm) return -1;
            return vm_destroy(vm->id);
        }
        case VM_CMD_PAUSE: {
            char name[VM_NAME_MAX];
            if (copy_from_user(name, (uint64_t)str, VM_NAME_MAX - 1) < 0) return -1;
            name[VM_NAME_MAX - 1] = 0;
            vm_t *vm = vm_find(name);
            if (!vm) return -1;
            return vm_pause(vm->id);
        }
        case VM_CMD_RESUME: {
            char name[VM_NAME_MAX];
            if (copy_from_user(name, (uint64_t)str, VM_NAME_MAX - 1) < 0) return -1;
            name[VM_NAME_MAX - 1] = 0;
            vm_t *vm = vm_find(name);
            if (!vm) return -1;
            return vm_resume(vm->id);
        }
        case VM_CMD_INFO: {
            vm_sync_states();
            char name[VM_NAME_MAX];
            if (copy_from_user(name, (uint64_t)str, VM_NAME_MAX - 1) < 0) return -1;
            name[VM_NAME_MAX - 1] = 0;
            vm_t *vm = vm_find(name);
            if (!vm) return -1;
            char out[512];
            int off = snprintf(out, sizeof(out),
                    "name=%s state=%s type=%d mem=%uM cpus=%d ip=%d.%d.%d.%d ssh=%d\n",
                    vm->name, vm_state_str(vm->state), (int)vm->type,
                    vm->config.memory_mb, vm->config.cpu_count,
                    (vm->ip >> 24) & 0xff, (vm->ip >> 16) & 0xff,
                    (vm->ip >> 8) & 0xff, vm->ip & 0xff, vm->ssh_port);
            if (copy_to_user((uint64_t)str + VM_NAME_MAX, out, off + 1) < 0) return -1;
            return off;
        }
        case VM_CMD_SNAPSHOT: {
            /* a3 = snapshot name */
            char name[VM_NAME_MAX];
            if (copy_from_user(name, (uint64_t)str, VM_NAME_MAX - 1) < 0) return -1;
            name[VM_NAME_MAX - 1] = 0;
            char snap[VM_NAME_MAX];
            if (copy_from_user(snap, (uint64_t)a3, VM_NAME_MAX - 1) < 0) return -1;
            snap[VM_NAME_MAX - 1] = 0;
            return vm_concierge_snapshot(name, snap);
        }
        case VM_CMD_CROSVM_CREATE: {
            /* a1=cmd, a2 = pointer to struct {
             *   char name[32]; char kernel[128]; char initrd[128];
             *   char rootfs[128]; uint64_t mem; int cpus; } */
            char name[VM_NAME_MAX];
            char kernel[VM_IMAGE_PATH_MAX];
            char initrd[VM_IMAGE_PATH_MAX];
            char rootfs[VM_IMAGE_PATH_MAX];
            uint64_t mem;
            int cpus;
            if (copy_from_user(name, (uint64_t)str, VM_NAME_MAX - 1) < 0) return -1;
            name[VM_NAME_MAX - 1] = 0;
            if (copy_from_user(kernel, (uint64_t)str + VM_NAME_MAX, VM_IMAGE_PATH_MAX - 1) < 0) return -1;
            if (copy_from_user(initrd, (uint64_t)str + VM_NAME_MAX + VM_IMAGE_PATH_MAX, VM_IMAGE_PATH_MAX - 1) < 0) return -1;
            if (copy_from_user(rootfs, (uint64_t)str + VM_NAME_MAX + 2 * VM_IMAGE_PATH_MAX, VM_IMAGE_PATH_MAX - 1) < 0) return -1;
            if (copy_from_user(&mem, (uint64_t)str + VM_NAME_MAX + 3 * VM_IMAGE_PATH_MAX, sizeof(uint64_t)) < 0) return -1;
            if (copy_from_user(&cpus, (uint64_t)str + VM_NAME_MAX + 3 * VM_IMAGE_PATH_MAX + sizeof(uint64_t), sizeof(int)) < 0) return -1;
            return vm_create_crosvm(name, kernel, initrd, rootfs, mem, cpus);
        }
        case VM_CMD_CONSOLE_WRITE: {
            /* a2 = container_id, a3 = user pointer, a4 = len */
            int cid = (int)a2;
            int len = (int)a4;
            if (len > 4096) len = 4096;
            uint8_t tmp[4096];
            if (copy_from_user(tmp, (uint64_t)a3, len) < 0) return -1;
            return container_console_write(cid, tmp, len);
        }
        case VM_CMD_CONSOLE_READ: {
            /* a2 = container_id, a3 = user buffer, a4 = max len */
            int cid = (int)a2;
            int max = (int)a4;
            if (max > 4096) max = 4096;
            uint8_t tmp[4096];
            int n = container_console_read(cid, tmp, max);
            if (n <= 0) return n;
            if (copy_to_user((uint64_t)a3, tmp, n) < 0) return -1;
            return n;
        }
        case VM_CMD_STDIN_WRITE: {
            /* a2 = container_id, a3 = user pointer, a4 = len */
            int cid = (int)a2;
            int len = (int)a4;
            if (len > 4096) len = 4096;
            uint8_t tmp[4096];
            if (copy_from_user(tmp, (uint64_t)a3, len) < 0) return -1;
            return container_stdin_write(cid, tmp, len);
        }
        case VM_CMD_STDIN_READ: {
            /* a2 = container_id, a3 = user buffer, a4 = max len */
            int cid = (int)a2;
            int max = (int)a4;
            if (max > 4096) max = 4096;
            uint8_t tmp[4096];
            int n = container_stdin_read(cid, tmp, max);
            if (n <= 0) return n;
            if (copy_to_user((uint64_t)a3, tmp, n) < 0) return -1;
            return n;
        }
        case VM_CMD_CONSOLE_ENABLE: {
            /* a2 = container_id */
            container_console_enable((int)a2);
            return 0;
        }
        case VM_CMD_CONTAINER_START: {
            /* a2 = container_id — launch with console I/O bridge */
            return container_launch_console((int)a2);
        }
        case VM_CMD_CONTAINER_STOP: {
            /* a2 = container_id */
            return container_stop((int)a2);
        }
        default:
            return -1;
        }
    }

    case SYSCALL_AUDIO_OPEN: {
        int sr = (int)a1;
        int ch = (int)a2;
        if (sr <= 0) sr = 48000;
        if (ch <= 0) ch = 2;
        return mixer_open_source(sr, ch);
    }
    case SYSCALL_AUDIO_WRITE: {
        int id = (int)a1;
        int bytes = (int)a3;
        if (bytes <= 0 || bytes > 65536) return -1;
        uint8_t tmp[8192];
        int total = 0;
        while (total < bytes) {
            int chunk = bytes - total;
            if (chunk > 8192) chunk = 8192;
            if (copy_from_user(tmp, a2 + total, chunk) < 0) return total > 0 ? total : -1;
            int w = mixer_write(id, tmp, chunk);
            if (w <= 0) break;
            total += w;
        }
        return total;
    }
    case SYSCALL_AUDIO_CLOSE:
        mixer_close_source((int)a1);
        return 0;
    case SYSCALL_AUDIO_SET_VOLUME:
        mixer_set_volume((int)a1, (int)a2);
        return 0;
    case SYSCALL_AUDIO_GET_VOLUME:
        return mixer_get_volume((int)a1);
    case SYSCALL_AUDIO_SET_STATE: {
        int id = (int)a1;
        int st = (int)a2;
        if (st == 1) mixer_play(id);
        else if (st == 2) mixer_pause(id);
        else mixer_stop(id);
        return 0;
    }
    case SYSCALL_AUDIO_GET_POS:
        return mixer_get_position((int)a1);
    case SYSCALL_AUDIO_GET_FREE:
        return mixer_get_free((int)a1);
    case SYSCALL_AUDIO_SET_MASTER:
        mixer_set_master_volume((int)a1);
        return 0;
    case SYSCALL_AUDIO_GET_MASTER:
        return mixer_get_master_volume();

    /* ── Socket / UDP syscalls ── */
    case SYSCALL_SOCKET_SOCKET:
        return socket_create((int)a1, (int)a2, (int)a3);
    case SYSCALL_SOCKET_CLOSE:
        return socket_close((int)a1);
    case SYSCALL_SOCKET_BIND:
    case SYSCALL_SOCKET_CONNECT: {
        if (a3 < (uint64_t)sizeof(sockaddr_in_t)) return -1;
        sockaddr_t saddr;
        if (copy_from_user(&saddr, a2, sizeof(saddr)) < 0) return -1;
        syscall_sock_ip_to_kernel(&saddr);
        if (n == SYSCALL_SOCKET_BIND) return socket_bind((int)a1, &saddr, (int)a3);
        return socket_connect((int)a1, &saddr, (int)a3);
    }
    case SYSCALL_SOCKET_SENDTO: {
        if (a3 <= 0 || a3 > 1024) return -1;
        uint8_t sndbuf[1024];
        if (copy_from_user(sndbuf, a2, a3) < 0) return -1;
        if (a6 < (uint64_t)sizeof(sockaddr_in_t)) return -1;
        sockaddr_t dst;
        if (copy_from_user(&dst, a5, sizeof(dst)) < 0) return -1;
        syscall_sock_ip_to_kernel(&dst);
        return socket_sendto((int)a1, sndbuf, (int)a3, (int)a4, &dst, (int)a6);
    }
    case SYSCALL_SOCKET_RECVFROM: {
        if (a3 <= 0 || a3 > 1024) return -1;
        uint8_t rcvbuf[1024];
        sockaddr_t src;
        uint32_t addrlen = sizeof(sockaddr_t);
        if (a5) {
            if (copy_from_user(&addrlen, a6, sizeof(addrlen)) < 0) return -1;
            if (addrlen < sizeof(sockaddr_in_t)) return -1;
        }
        int r = socket_recvfrom((int)a1, rcvbuf, (int)a3, (int)a4,
                                a5 ? &src : 0, (int *)&addrlen);
        if (r < 0) return r;
        if (copy_to_user(a2, rcvbuf, (uint64_t)r) < 0) return -1;
        if (a5) {
            syscall_sock_ip_to_user(&src);
            if (copy_to_user(a5, &src, sizeof(src)) < 0) return -1;
            if (copy_to_user(a6, &addrlen, sizeof(addrlen)) < 0) return -1;
        }
        return r;
    }
    case SYSCALL_SOCKET_SEND: {
        if (a3 <= 0 || a3 > 1024) return -1;
        uint8_t sndbuf[1024];
        if (copy_from_user(sndbuf, a2, a3) < 0) return -1;
        return socket_send((int)a1, sndbuf, (int)a3, (int)a4);
    }
    case SYSCALL_SOCKET_RECV: {
        if (a3 <= 0 || a3 > 1024) return -1;
        uint8_t rcvbuf[1024];
        int r = socket_recv((int)a1, rcvbuf, (int)a3, (int)a4);
        if (r < 0) return r;
        if (copy_to_user(a2, rcvbuf, (uint64_t)r) < 0) return -1;
        return r;
    }
    case SYSCALL_SOCKET_GETSOCKNAME:
    case SYSCALL_SOCKET_GETPEERNAME: {
        sockaddr_t saddr;
        uint32_t addrlen = sizeof(sockaddr_t);
        if (a3) {
            if (copy_from_user(&addrlen, a3, sizeof(addrlen)) < 0) return -1;
            if (addrlen < sizeof(sockaddr_in_t)) return -1;
        }
        int r = (n == SYSCALL_SOCKET_GETSOCKNAME)
                    ? socket_getsockname((int)a1, &saddr, (int *)&addrlen)
                    : socket_getpeername((int)a1, &saddr, (int *)&addrlen);
        if (r < 0) return r;
        syscall_sock_ip_to_user(&saddr);
        if (a2 && copy_to_user(a2, &saddr, sizeof(saddr)) < 0) return -1;
        addrlen = sizeof(sockaddr_in_t);
        if (a3 && copy_to_user(a3, &addrlen, sizeof(addrlen)) < 0) return -1;
        return 0;
    }
    case SYSCALL_SOCKET_SETSOCKOPT: {
        uint32_t optlen = (uint32_t)a5;
        if (optlen > 256) return -1;
        char optval[256];
        if (a4 && optlen) {
            if (copy_from_user(optval, a4, optlen) < 0) return -1;
        }
        return socket_setsockopt((int)a1, (int)a2, (int)a3,
                                 a4 ? optval : 0, (int)optlen);
    }
    case SYSCALL_SOCKET_GETSOCKOPT: {
        uint32_t optlen = sizeof(uint32_t);
        if (a5) {
            if (copy_from_user(&optlen, a5, sizeof(optlen)) < 0) return -1;
        }
        if (optlen > 256) optlen = 256;
        char optval[256];
        uint32_t outlen = optlen;
        int r = socket_getsockopt((int)a1, (int)a2, (int)a3, optval, (int *)&outlen);
        if (r < 0) return r;
        if (outlen && copy_to_user(a4, optval, outlen) < 0) return -1;
        if (a5 && copy_to_user(a5, &outlen, sizeof(outlen)) < 0) return -1;
        return 0;
    }

    default:
        return -1;
    }
}

static void syscall_irq(int_frame_t *frame) {
    uint64_t n  = frame->rax;
    uint64_t a1 = frame->rdi;
    uint64_t a2 = frame->rsi;
    uint64_t a3 = frame->rdx;
    uint64_t a4 = frame->r10;
    uint64_t a5 = frame->r8;
    uint64_t a6 = frame->r9;

    sched_preempt_disable();
    int64_t ret = syscall_handler(n, a1, a2, a3, a4, a5, a6);
    frame->rax = (uint64_t)ret;
    sched_preempt_enable();

    if (user_mode_exit_flag) {
        user_mode_exit_flag = 0;
        user_mode_force_return();
    }
}

void syscall_init(void) {
#ifndef __aarch64__
    isr_set_handler(0x80, syscall_irq);
#endif
}

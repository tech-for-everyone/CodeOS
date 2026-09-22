#ifndef SECURITY_ADVANCED_H
#define SECURITY_ADVANCED_H

#include <stdint.h>
#include <stddef.h>

/* ========================================================================
 * ADVANCED SECURITY FEATURES
 * ======================================================================== */

/* ASLR Configuration */
#define ASLR_STACK_OFFSET_BITS     16  /* 64KB stack entropy */
#define ASLR_HEAP_OFFSET_BITS      16  /* 64KB heap entropy */
#define ASLR_MMAP_OFFSET_BITS      16  /* 64KB mmap entropy */
#define ASLR_MAX_OFFSET            ((1u << ASLR_STACK_OFFSET_BITS) - 1)

/* Syscall Filtering (seccomp-like) */
#define SECCOMP_FILTER_MAX         64  /* Max rules per process */
#define SECCOMP_ACTION_ALLOW       0
#define SECCOMP_ACTION_KILL        1
#define SECCOMP_ACTION_TRAP        2
#define SECCOMP_ACTION_ERRNO       3
#define SECCOMP_ACTION_LOG         4
#define SECCOMP_ACTION_TRACE       5

/* Syscall numbers used by the seccomp-style filter.
 * NOTE: this is a self-contained reference table for the advanced security
 * module — it is NOT the native CodeOS ABI (see include/codeos/syscall_abi.h)
 * and NOT the Linux-compat numbers used by linux_syscall_handler (syscall.c). */
#define SC_READ                    0
#define SC_WRITE                   1
#define SC_OPEN                    2
#define SC_CLOSE                   3
#define SC_MMAP                    4
#define SC_MUNMAP                  5
#define SC_BRICK                   5
#define SC_EXIT                    6
#define SC_FORK                    7
#define SC_EXECVE                  8
#define SC_WAIT                    9
#define SC_YIELD                   10
#define SC_GETPID                  11
#define SC_GETPPID                 12
#define SC_GETUID                  13
#define SC_GETGID                  14
#define SC_SETUID                  15
#define SC_SETGID                  16
#define SC_GETEUID                 17
#define SC_GETEGID                 18
#define SC_SETEUID                 19
#define SC_SETEGID                 20
#define SC_GETPGID                 21
#define SC_SETPGID                 22
#define SC_GETSID                  23
#define SC_SETSID                  24
#define SC_GETPGRP                 25
#define SC_SETPGRP                 26
#define SC_GETRLIMIT               27
#define SC_SETRLIMIT               28
#define SC_GETRUSAGE               29
#define SC_GETTIMEOFDAY            30
#define SC_SETTIMEOFDAY            31
#define SC_GETITIMER               32
#define SC_SETITIMER               33
#define SC_NANOSLEEP               34
#define SC_SIGACTION               35
#define SC_SIGPROCMASK             36
#define SC_SIGPENDING              37
#define SC_SIGSUSPEND              38
#define SC_SIGRETURN               39
#define SC_KILL                    40
#define SC_TKILL                   41
#define SC_TGKILL                  42
#define SC_ALARM                   42
#define SC_PAUSE                   43
#define SC_SOCKET                  44
#define SC_BIND                    45
#define SC_LISTEN                  46
#define SC_ACCEPT                  47
#define SC_CONNECT                 48
#define SC_SENDTO                  49
#define SC_RECVFROM                50
#define SC_SENDMSG                 51
#define SC_RECVMSG                 52
#define SC_SHUTDOWN                53
#define SC_SETSOCKOPT              54
#define SC_GETSOCKOPT              55
#define SC_GETSOCKNAME             56
#define SC_GETPEERNAME             57
#define SC_SELECT                  58
#define SC_POLL                    59
#define SC_EPOLL_CREATE            60
#define SC_EPOLL_CTL               61
#define SC_EPOLL_WAIT              62
#define SC_EPOLL_PWAIT             63
#define SC_PIPE                    64
#define SC_PIPE2                   65
#define SC_DUP                     66
#define SC_DUP2                    67
#define SC_DUP3                    68
#define SC_FCNTL                   69
#define SC_IOCTL                   70
#define SC_FTRUNCATE               71
#define SC_FSTAT                   72
#define SC_LSTAT                   73
#define SC_STAT                    74
#define SC_STATFS                  75
#define SC_FSTATFS                 76
#define SC_ACCESS                  77
#define SC_CHMOD                   78
#define SC_FCHMOD                  79
#define SC_CHOWN                   80
#define SC_FCHOWN                  81
#define SC_LCHOWN                  82
#define SC_UTIME                   83
#define SC_UTIMES                  84
#define SC_MKNOD                   85
#define SC_MKDIR                   86
#define SC_RMDIR                   87
#define SC_UNLINK                  88
#define SC_RENAME                  89
#define SC_LINK                    90
#define SC_SYMLINK                 91
#define SC_READLINK                92
#define SC_CHROOT                  93
#define SC_CHDIR                   94
#define SC_FCHDIR                  95
#define SC_GETCWD                  95
#define SC_GETDENTS                96
#define SC_GETDENTS64              97
#define SC_MOUNT                   98
#define SC_UMOUNT                  99
#define SC_UMOUNT2                 100
#define SC_SYSLOG                  100
#define SC_SYSCTL                  101
#define SC_PERF_EVENT_OPEN         102
#define SC_PRCTL                   103
#define SC_ARCH_PRCTL              104
#define SC_CAPGET                  105
#define SC_CAPSET                  106
#define SC_GETCPU                  107
#define SC_GETRANDOM               108
#define SC_MEMBARRIER              109
#define SC_USERFAULTFD             110
#define SC_RSEQ                    111
#define SC_PKEY_MPROTECT           112
#define SC_PKEY_ALLOC              113
#define SC_PKEY_FREE               114
#define SC_FACCESSAT               126
#define SC_FCHMODAT                127
#define SC_FCHOWNAT                128
#define SC_FSTATAT                 129
#define SC_FUTIMESAT               130
#define SC_LINKAT                  131
#define SC_MKDIRAT                 132
#define SC_MKNODAT                 133
#define SC_OPENAT                  134
#define SC_READLINKAT              135
#define SC_RENAMEAT                136
#define SC_SYMLINKAT               137
#define SC_UNLINKAT                138
#define SC_UTIMENSAT               139
#define SC_FSTATAT64               140
#define SC_STATX                   141
#define SC_IO_URING_SETUP          142
#define SC_IO_URING_ENTER          143
#define SC_IO_URING_REGISTER       144
#define SC_PROCESS_MADVISE         145
#define SC_PIDFD_SEND_SIGNAL       146
#define SC_PIDFD_OPEN              147
#define SC_CLONE3                  148
#define SC_CLOSE_RANGE             149
#define SC_OPENAT2                 150
#define SC_PIDFD_GETFD             151

#define SYSCALL_MAX                256

/* Process capabilities are provided by capability.h / capability.c
 * (per-process, Linux-style indices). This module deliberately does not
 * redefine that API. */

/* MAC Labels */
#define MAC_LABEL_MAX_LEN     64
#define MAC_LABEL_MAX_ENTRIES 256

/* Secure Memory Allocator */
#define SEC_ALLOC_REDZONE_SIZE    16
#define SEC_ALLOC_GUARD_PAGE      1
#define SEC_ALLOC_QUARANTINE_SIZE 1024

/* Kernel Page Table Isolation */
#define KPTI_ENABLED 1

/* ========================================================================
 * Types
 * ======================================================================== */

/* Syscall filter rule */
typedef struct {
    uint16_t syscall;
    uint8_t  action;     /* SECCOMP_ACTION_* */
    int      errno_val;  /* For SECCOMP_ACTION_ERRNO */
    uint8_t  enabled;
} seccomp_rule_t;

/* Seccomp filter context */
typedef struct {
    seccomp_rule_t rules[SECCOMP_FILTER_MAX];
    int count;
    uint8_t default_action;
    uint8_t log_enabled;
} seccomp_filter_t;

/* MAC label */
typedef struct {
    char label[MAC_LABEL_MAX_LEN];
    uint32_t hash;
    uint8_t active;
} mac_label_t;

/* MAC context */
typedef struct {
    mac_label_t labels[MAC_LABEL_MAX_ENTRIES];
    int count;
    uint8_t enforce;
} mac_context_t;

/* Secure memory allocation header */
typedef struct sec_alloc_header {
    uint32_t magic;
    size_t size;
    struct sec_alloc_header *next;
    struct sec_alloc_header *prev;
    uint8_t redzone[SEC_ALLOC_REDZONE_SIZE];
} sec_alloc_header_t;

/* Secure allocator statistics/counters */
typedef struct {
    size_t allocations;
    size_t current_size;
    size_t peak_size;
} sec_alloc_t;

/* ASLR state */
typedef struct {
    uintptr_t stack_offset;
    uintptr_t heap_offset;
    uintptr_t mmap_offset;
    uint8_t initialized;
} aslr_state_t;

/* Shadow stack for CFI */
#define SHADOW_STACK_SIZE 4096
typedef struct {
    uintptr_t stack[SHADOW_STACK_SIZE];
    int top;
    uint8_t active;
} shadow_stack_t;

/* ========================================================================
 * API Functions
 * ======================================================================== */

/* ASLR */
void aslr_init(void);
uintptr_t aslr_randomize_stack(uintptr_t base);
uintptr_t aslr_randomize_heap(uintptr_t base);
uintptr_t aslr_randomize_mmap(uintptr_t base);
void aslr_get_offsets(uintptr_t *stack, uintptr_t *heap, uintptr_t *mmap);

/* Seccomp */
int seccomp_filter_init(seccomp_filter_t *filter, uint8_t default_action);
int seccomp_rule_add(seccomp_filter_t *filter, uint16_t syscall, uint8_t action, int errno_val);
int seccomp_filter_check(const seccomp_filter_t *filter, uint16_t syscall, int *errno_out);
void seccomp_filter_dump(const seccomp_filter_t *filter);

/* MAC Labels */
int mac_init(mac_context_t *ctx);
int mac_label_create(mac_context_t *ctx, const char *label);
int mac_label_assign(mac_context_t *ctx, const char *subject, const char *object, uint32_t perms);
int mac_check(const mac_context_t *ctx, const char *subject, const char *object, uint32_t perms);
int mac_label_get_hash(const char *label, uint32_t *hash_out);
void mac_context_dump(const mac_context_t *ctx);

/* Secure Allocator */
void *sec_malloc(size_t size);
void *sec_calloc(size_t nmemb, size_t size);
void *sec_realloc(void *ptr, size_t size);
void sec_free(void *ptr);
int sec_alloc_verify(const void *ptr);
size_t sec_alloc_get_size(const void *ptr);
void sec_alloc_dump_stats(void);

/* ASLR */
void aslr_apply_stack_randomization(void *stack_base, size_t stack_size);
void aslr_apply_heap_randomization(void *heap_base, size_t heap_size);

/* Shadow Stack (CFI) */
void shadow_stack_init(shadow_stack_t *ss);
void shadow_stack_push(shadow_stack_t *ss, uintptr_t ret_addr);
uintptr_t shadow_stack_pop(shadow_stack_t *ss);
int shadow_stack_verify(shadow_stack_t *ss, uintptr_t expected_ret);
void shadow_stack_enable(shadow_stack_t *ss);
void shadow_stack_disable(shadow_stack_t *ss);

/* KPTI */
int kpti_init(void);
void kpti_enable(void);
void kpti_disable(void);
int kpti_is_enabled(void);

/* Secure boot verification */
int secure_boot_verify(const void *image, size_t size, const uint8_t *expected_hash);
int secure_boot_hash(const void *data, size_t len, uint8_t *hash_out);

/* Random number generation (CSPRNG) */
int secure_random_bytes(void *buf, size_t len);
uint64_t secure_random_u64(void);
uint32_t secure_random_u32(void);

/* Initialize the advanced security subsystems (called from sec_init) */
void sec_advanced_init(void);

/* ========================================================================
 * Constants
 * ======================================================================== */

#define SEC_ALLOC_MAGIC 0x53454341  /* 'SECA' */

#define SECCOMP_DEFAULT_ACTION SECCOMP_ACTION_ALLOW

/* MAC permission bits */
#define MAC_PERM_READ    (1 << 0)
#define MAC_PERM_WRITE   (1 << 1)
#define MAC_PERM_EXECUTE (1 << 2)
#define MAC_PERM_APPEND  (1 << 3)
#define MAC_PERM_DELETE  (1 << 4)
#define MAC_PERM_ALL     (MAC_PERM_READ | MAC_PERM_WRITE | MAC_PERM_EXECUTE | MAC_PERM_APPEND | MAC_PERM_DELETE)

#endif /* SECURITY_ADVANCED_H */
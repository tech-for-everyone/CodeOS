#ifndef SYS_SYSCALL_H
#define SYS_SYSCALL_H

#include "types.h"

#define SYS_read 0
#define SYS_write 1
#define SYS_open 2
#define SYS_close 3
#define SYS_mmap 9
#define SYS_munmap 11
#define SYS_brk 12
#define SYS_kill 37
#define SYS_dup 32
#define SYS_pipe 33
#define SYS_getpid 39
#define SYS_getuid 40
#define SYS_geteuid 41
#define SYS_getgid 42
#define SYS_fcntl 55
#define SYS_futex 202
#define SYS_getrandom 318
#define SYS_exit_group 231
#define SYS_arch_prctl 210
#define SYS_ioctl 16
#define SYS_readv 145
#define SYS_writev 146
#define SYS_pread64 147
#define SYS_pwrite64 148
#define SYS_set_tid_address 258
#define SYS_clock_gettime 228
#define SYS_sched_yield 24
#define SYS_sched_getaffinity 242
#define SYS_sched_setaffinity 241
#define SYS_clone 56
#define SYS_wait4 61
#define SYS_ptrace 101

#endif

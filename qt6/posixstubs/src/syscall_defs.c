/* CodeOS syscall function implementations
 * These provide real function symbols for the sys_* functions
 * that are normally static inline in unistd.h */
#include <stdint.h>
#include <stddef.h>
static long syscall_0(long n) {
    long ret;
    __asm__ volatile("int $0x80" : "=a"(ret) : "a"(n) : "memory");
    return ret;
}
static long syscall_1(long n, long a1) {
    long ret;
    __asm__ volatile("int $0x80" : "=a"(ret) : "a"(n), "D"(a1) : "memory");
    return ret;
}
static long syscall_2(long n, long a1, long a2) {
    long ret;
    __asm__ volatile("int $0x80" : "=a"(ret) : "a"(n), "D"(a1), "S"(a2) : "memory");
    return ret;
}
static long syscall_3(long n, long a1, long a2, long a3) {
    long ret;
    __asm__ volatile("int $0x80" : "=a"(ret) : "a"(n), "D"(a1), "S"(a2), "d"(a3) : "memory");
    return ret;
}

#define SYSCALL_WRITE      0
#define SYSCALL_SLEEP      1
#define SYSCALL_EXIT       2
#define SYSCALL_GETTID     3
#define SYSCALL_OPEN       4
#define SYSCALL_READ       5
#define SYSCALL_YIELD      6
#define SYSCALL_ZIRCON_IPC 7
#define SYSCALL_FORK       8
#define SYSCALL_EXECVE     9
#define SYSCALL_WAIT       10
#define SYSCALL_GETPID     11
#define SYSCALL_GETPPID    12
#define SYSCALL_CLOSE      13
#define SYSCALL_PIPE       14
#define SYSCALL_DUP        15
#define SYSCALL_BRK        16
#define SYSCALL_MMAP       17
#define SYSCALL_MUNMAP     18
#define SYSCALL_LSEEK      19
#define SYSCALL_SBRK       20
#define SYSCALL_BINDER     21
#define SYSCALL_ASHMEM     22
#define SYSCALL_DUP2       34
#define SYSCALL_FB_INFO     28
#define SYSCALL_INPUT_POLL  29

int sys_write(const void *buf, int count) { return syscall_2(SYSCALL_WRITE, (long)buf, count); }
int sys_read(int fd, void *buf, int count) { return syscall_3(SYSCALL_READ, fd, (long)buf, count); }
int sys_open(const char *path, int flags) { return syscall_2(SYSCALL_OPEN, (long)path, flags); }
int sys_close(int fd) { return syscall_1(SYSCALL_CLOSE, fd); }
int sys_lseek(int fd, int offset, int whence) { return syscall_3(SYSCALL_LSEEK, fd, offset, whence); }
int sys_dup(int oldfd) { return syscall_1(SYSCALL_DUP, oldfd); }
int sys_dup2(int oldfd, int newfd) { return syscall_2(SYSCALL_DUP2, oldfd, newfd); }
int sys_pipe(int fd[2]) { return syscall_1(SYSCALL_PIPE, (long)fd); }
int sys_fork(void) { return syscall_0(SYSCALL_FORK); }
int sys_execve(const char *path, char **argv, int argc) { return syscall_3(SYSCALL_EXECVE, (long)path, (long)argv, argc); }
int sys_wait(int pid, int *status) { return syscall_2(SYSCALL_WAIT, pid, (long)status); }
int sys_getpid(void) { return syscall_0(SYSCALL_GETPID); }
void sys_exit(int status) { syscall_1(SYSCALL_EXIT, status); }
void sys_sleep(int ms) { syscall_1(SYSCALL_SLEEP, ms); }
int sys_yield(void) { return syscall_0(SYSCALL_YIELD); }
void *sys_brk(void *addr) { return (void*)syscall_1(SYSCALL_BRK, (long)addr); }
void *sys_mmap(void *addr, int len, int prot) { return (void*)syscall_3(SYSCALL_MMAP, (long)addr, len, prot); }

uint64_t timer_get_milliseconds(void) {
    /* Stub: return 0 for now; replace with real timer via syscall */
    return 0;
}

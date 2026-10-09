typedef unsigned long uint64_t;
typedef unsigned int uint32_t;

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
#define SYSCALL_DUP2       34
#define SYSCALL_SHM        35

int sys_write(const void *buf, int count) {
    int ret;
    __asm__ volatile("int $0x80" : "=a"(ret) : "a"(SYSCALL_WRITE), "D"(buf), "S"(count) : "memory");
    return ret;
}

int sys_read(int fd, void *buf, int count) {
    int ret;
    __asm__ volatile("int $0x80" : "=a"(ret) : "a"(SYSCALL_READ), "D"(fd), "S"(buf), "d"(count) : "memory");
    return ret;
}

int sys_open(const char *path, int flags) {
    int ret;
    __asm__ volatile("int $0x80" : "=a"(ret) : "a"(SYSCALL_OPEN), "D"(path), "S"(flags) : "memory");
    return ret;
}

int sys_close(int fd) {
    int ret;
    __asm__ volatile("int $0x80" : "=a"(ret) : "a"(SYSCALL_CLOSE), "D"(fd));
    return ret;
}

void sys_exit(int status) {
    __asm__ volatile("int $0x80" : : "a"(SYSCALL_EXIT), "D"(status));
}

void *sys_brk(void *addr) {
    void *ret;
    __asm__ volatile("int $0x80" : "=a"(ret) : "a"(SYSCALL_BRK), "D"(addr) : "memory");
    return ret;
}

int sys_fork(void) {
    int ret;
    __asm__ volatile("int $0x80" : "=a"(ret) : "a"(SYSCALL_FORK));
    return ret;
}

int sys_execve(const char *path, char **argv, int argc) {
    int ret;
    __asm__ volatile("int $0x80" : "=a"(ret) : "a"(SYSCALL_EXECVE), "D"(path), "S"(argv), "d"(argc) : "memory");
    return ret;
}

int sys_wait(int pid, int *status) {
    int ret;
    __asm__ volatile("int $0x80" : "=a"(ret) : "a"(SYSCALL_WAIT), "D"(pid), "S"(status) : "memory");
    return ret;
}

int sys_getpid(void) {
    int ret;
    __asm__ volatile("int $0x80" : "=a"(ret) : "a"(SYSCALL_GETPID));
    return ret;
}

int sys_pipe(int fd[2]) {
    int ret;
    __asm__ volatile("int $0x80" : "=a"(ret) : "a"(SYSCALL_PIPE), "D"(fd) : "memory");
    return ret;
}

int sys_dup(int oldfd) {
    int ret;
    __asm__ volatile("int $0x80" : "=a"(ret) : "a"(SYSCALL_DUP), "D"(oldfd));
    return ret;
}

int sys_dup2(int oldfd, int newfd) {
    int ret;
    __asm__ volatile("int $0x80" : "=a"(ret) : "a"(SYSCALL_DUP2), "D"(oldfd), "S"(newfd) : "memory");
    return ret;
}

int sys_lseek(int fd, int offset, int whence) {
    int ret;
    __asm__ volatile("int $0x80" : "=a"(ret) : "a"(SYSCALL_LSEEK), "D"(fd), "S"(offset), "d"(whence) : "memory");
    return ret;
}

void *sys_mmap(void *addr, int len, int prot) {
    void *ret;
    register int _flags asm("r10") = 0;
    __asm__ volatile("int $0x80" : "=a"(ret) : "a"(SYSCALL_MMAP), "D"(addr), "S"(len), "d"(prot), "r"(_flags) : "memory");
    return ret;
}

void sys_yield(void) {
    __asm__ volatile("int $0x80" : : "a"(SYSCALL_YIELD));
}

void sys_sleep(int ms) {
    __asm__ volatile("int $0x80" : : "a"(SYSCALL_SLEEP), "D"(ms));
}

uint64_t timer_get_milliseconds(void) {
    return 0;
}

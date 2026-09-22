#ifndef _UNISTD_H
#define _UNISTD_H

#include <stdint.h>

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
#define SYSCALL_CONTAINER_CREATE  23
#define SYSCALL_CONTAINER_EXEC    24
#define SYSCALL_CONTAINER_DESTROY 25
#define SYSCALL_CONTAINER_LIST    26
#define SYSCALL_AI_QUERY          27

#define SYSCALL_FB_INFO           28
#define SYSCALL_INPUT_POLL        29
#define SYSCALL_SET_PERSONALITY   30
#define SYSCALL_AUDIO_PLAY        31
#define SYSCALL_AUDIO_STATUS      32
#define SYSCALL_PWRITE            33
#define SYSCALL_DUP2              34
#define SYSCALL_SHM               35

#define MAP_PHYSICAL 0x10000

#define PERSONALITY_LINUX 1

static inline int sys_set_personality(int personality) {
    int ret;
    __asm__ volatile("int $0x80" : "=a"(ret) : "a"(SYSCALL_SET_PERSONALITY), "D"(personality) : "memory");
    return ret;
}

typedef struct {
    uint64_t addr;
    uint32_t width;
    uint32_t height;
    uint32_t pitch;
    uint8_t  bpp;
    uint8_t  type;
} fb_info_t;

typedef struct {
    int      type;   /* 0=none, 1=key, 2=mouse */
    int      key;
    int      mouse_x;
    int      mouse_y;
    int      mouse_buttons;
    uint64_t _pad;
} input_event_t;

static inline int sys_fb_info(fb_info_t *info) {
    int ret;
    __asm__ volatile("int $0x80" : "=a"(ret) : "a"(SYSCALL_FB_INFO), "D"(info) : "memory");
    return ret;
}

static inline int sys_input_poll(input_event_t *ev) {
    int ret;
    __asm__ volatile("int $0x80" : "=a"(ret) : "a"(SYSCALL_INPUT_POLL), "D"(ev) : "memory");
    return ret;
}

static inline int sys_write(const void *buf, int count) {
    int ret;
    __asm__ volatile("int $0x80" : "=a"(ret) : "a"(SYSCALL_WRITE), "D"(buf), "S"(count) : "memory");
    return ret;
}

static inline int sys_read(int fd, void *buf, int count) {
    int ret;
    __asm__ volatile("int $0x80" : "=a"(ret) : "a"(SYSCALL_READ), "D"(fd), "S"(buf), "d"(count) : "memory");
    return ret;
}

static inline int sys_open(const char *path, int flags) {
    int ret;
    __asm__ volatile("int $0x80" : "=a"(ret) : "a"(SYSCALL_OPEN), "D"(path), "S"(flags) : "memory");
    return ret;
}

static inline void sys_exit(int status) {
    __asm__ volatile("int $0x80" : : "a"(SYSCALL_EXIT), "D"(status));
}

static inline void sys_sleep(int ms) {
    __asm__ volatile("int $0x80" : : "a"(SYSCALL_SLEEP), "D"(ms));
}

static inline int sys_gettid(void) {
    int ret;
    __asm__ volatile("int $0x80" : "=a"(ret) : "a"(SYSCALL_GETTID));
    return ret;
}

static inline void sys_yield(void) {
    __asm__ volatile("int $0x80" : : "a"(SYSCALL_YIELD));
}

static inline int sys_fork(void) {
    int ret;
    __asm__ volatile("int $0x80" : "=a"(ret) : "a"(SYSCALL_FORK));
    return ret;
}

static inline int sys_execve(const char *path, char **argv, int argc) {
    int ret;
    __asm__ volatile("int $0x80" : "=a"(ret) : "a"(SYSCALL_EXECVE), "D"(path), "S"(argv), "d"(argc) : "memory");
    return ret;
}

static inline int sys_wait(int pid, int *status) {
    int ret;
    __asm__ volatile("int $0x80" : "=a"(ret) : "a"(SYSCALL_WAIT), "D"(pid), "S"(status) : "memory");
    return ret;
}

static inline int sys_getpid(void) {
    int ret;
    __asm__ volatile("int $0x80" : "=a"(ret) : "a"(SYSCALL_GETPID));
    return ret;
}

static inline int sys_getppid(void) {
    int ret;
    __asm__ volatile("int $0x80" : "=a"(ret) : "a"(SYSCALL_GETPPID));
    return ret;
}

static inline int sys_close(int fd) {
    int ret;
    __asm__ volatile("int $0x80" : "=a"(ret) : "a"(SYSCALL_CLOSE), "D"(fd));
    return ret;
}

static inline int sys_pipe(int fd[2]) {
    int ret;
    __asm__ volatile("int $0x80" : "=a"(ret) : "a"(SYSCALL_PIPE), "D"(fd) : "memory");
    return ret;
}

static inline int sys_dup(int oldfd) {
    int ret;
    __asm__ volatile("int $0x80" : "=a"(ret) : "a"(SYSCALL_DUP), "D"(oldfd));
    return ret;
}

static inline int sys_lseek(int fd, int offset, int whence) {
    int ret;
    __asm__ volatile("int $0x80" : "=a"(ret) : "a"(SYSCALL_LSEEK), "D"(fd), "S"(offset), "d"(whence) : "memory");
    return ret;
}

static inline void *sys_brk(void *addr) {
    void *ret;
    __asm__ volatile("int $0x80" : "=a"(ret) : "a"(SYSCALL_BRK), "D"(addr) : "memory");
    return ret;
}

static inline void *sys_mmap(void *addr, int len, int prot) {
    void *ret;
    register int _flags asm("r10") = 0;
    __asm__ volatile("int $0x80" : "=a"(ret) : "a"(SYSCALL_MMAP), "D"(addr), "S"(len), "d"(prot), "r"(_flags) : "memory");
    return ret;
}

static inline void *sys_mmap_phys(void *phys_addr, int len, int prot) {
    void *ret;
    register int _flags asm("r10") = MAP_PHYSICAL;
    __asm__ volatile("int $0x80" : "=a"(ret) : "a"(SYSCALL_MMAP), "D"(phys_addr), "S"(len), "d"(prot), "r"(_flags) : "memory");
    return ret;
}

static inline int sys_zircon_ipc(int cmd, void *buf) {
    int ret;
    __asm__ volatile("int $0x80" : "=a"(ret) : "a"(SYSCALL_ZIRCON_IPC), "D"(cmd), "S"(buf) : "memory");
    return ret;
}

static inline int sys_binder(void *t, int size) {
    int ret;
    __asm__ volatile("int $0x80" : "=a"(ret) : "a"(SYSCALL_BINDER), "D"(t), "S"(size) : "memory");
    return ret;
}

static inline int sys_ashmem(int cmd, void *data, int arg1, int arg2) {
    int ret;
    register int _a4 asm("r10") = arg2;
    __asm__ volatile("int $0x80" : "=a"(ret) : "a"(SYSCALL_ASHMEM), "D"(cmd), "S"(data), "d"(arg1), "r"(_a4) : "memory");
    return ret;
}

static inline int sys_ai_query(const char *prompt, char *response, int max_len) {
    int ret;
    __asm__ volatile("int $0x80" : "=a"(ret) : "a"(SYSCALL_AI_QUERY), "D"(prompt), "S"(response), "d"(max_len) : "memory");
    return ret;
}

static inline int sys_audio_play(const int16_t *samples, int count) {
    int ret;
    __asm__ volatile("int $0x80" : "=a"(ret) : "a"(SYSCALL_AUDIO_PLAY), "D"(samples), "S"(count) : "memory");
    return ret;
}

static inline int sys_audio_status(void) {
    int ret;
    __asm__ volatile("int $0x80" : "=a"(ret) : "a"(SYSCALL_AUDIO_STATUS));
    return ret;
}

static inline int sys_pwrite(int fd, const void *buf, int count) {
    int ret;
    __asm__ volatile("int $0x80" : "=a"(ret) : "a"(SYSCALL_PWRITE), "D"(fd), "S"(buf), "d"(count) : "memory");
    return ret;
}

static inline int sys_dup2(int oldfd, int newfd) {
    int ret;
    __asm__ volatile("int $0x80" : "=a"(ret) : "a"(SYSCALL_DUP2), "D"(oldfd), "S"(newfd) : "memory");
    return ret;
}

/* Shared memory commands */
#define SHM_CREATE 0
#define SHM_MAP    1
#define SHM_GET_SIZE 2

static inline int sys_shm_create(int size) {
    int ret;
    __asm__ volatile("int $0x80" : "=a"(ret) : "a"(SYSCALL_SHM), "D"(SHM_CREATE), "S"(size) : "memory");
    return ret;
}

static inline void *sys_shm_map(int fd) {
    void *ret;
    __asm__ volatile("int $0x80" : "=a"(ret) : "a"(SYSCALL_SHM), "D"(SHM_MAP), "S"(fd) : "memory");
    return ret;
}

#endif

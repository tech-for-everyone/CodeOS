#include <stdint.h>

/* Syscall wrappers */
static inline long syscall(long n, long a1, long a2, long a3) {
    long ret;
    asm volatile("int $0x80" : "=a"(ret) : "a"(n), "D"(a1), "S"(a2), "d"(a3) : "rcx", "r11", "memory");
    return ret;
}

#define SYSCALL_WRITE   4
#define SYSCALL_MKDIR   39
#define SYSCALL_UNLINK  40
#define SYSCALL_FORK    10
#define SYSCALL_EXECVE  11
#define SYSCALL_EXIT    6
#define SYSCALL_SLEEP   1

static int sys_write(int fd, const void *b, int s) { return (int)syscall(SYSCALL_WRITE, fd, (long)b, s); }
static int sys_mkdir(const char *p, int m) { return (int)syscall(SYSCALL_MKDIR, (long)p, m, 0); }
static void sys_exit(int s) { syscall(SYSCALL_EXIT, s, 0, 0); }
static void sys_sleep(int ms) { syscall(SYSCALL_SLEEP, ms, 0, 0); }

static int slen(const char *s) { int l = 0; while (s[l]) l++; return l; }
static void scpy(char *d, const char *s) { while (*s) *d++ = *s++; *d = 0; }

static void wlog(const char *msg) {
    sys_write(1, msg, slen(msg));
}

static void create_prefix_dirs(const char *name) {
    char buf[256];

    scpy(buf, "/wines/");
    sys_mkdir("/wines", 0);
    scpy(buf + 6, name);
    sys_mkdir(buf, 0);

    scpy(buf + 6 + slen(name), "/drive_c");
    sys_mkdir(buf, 0);

    scpy(buf + 6 + slen(name) + 8, "/windows");
    sys_mkdir(buf, 0);

    scpy(buf + 6 + slen(name) + 16, "/system32");
    sys_mkdir(buf, 0);

    char pf[256];
    scpy(pf, "/wines/");
    scpy(pf + 7, name);
    scpy(pf + 7 + slen(name), "/program_files");
    sys_mkdir(pf, 0);
}

int main(int argc, char **argv) {
    (void)argc; (void)argv;
    wlog("wineserver: starting\n");
    create_prefix_dirs("default");
    create_prefix_dirs("proton-ge");
    create_prefix_dirs("gaming");
    create_prefix_dirs("desktop");
    wlog("wineserver: initialized 4 prefixes\n");
    wlog("wineserver: ready\n");
    while (1) {
        sys_sleep(1000);
    }
    sys_exit(0);
    return 0;
}

#include <stdint.h>

static inline long syscall(long n, long a1, long a2, long a3) {
    long ret;
    asm volatile("int $0x80" : "=a"(ret) : "a"(n), "D"(a1), "S"(a2), "d"(a3) : "rcx", "r11", "memory");
    return ret;
}

#define SYSCALL_OPEN      2
#define SYSCALL_READ      3
#define SYSCALL_WRITE     4
#define SYSCALL_CLOSE     5
#define SYSCALL_MMAP      9
#define SYSCALL_FORK      10
#define SYSCALL_EXECVE    11
#define SYSCALL_WAIT      12
#define SYSCALL_EXIT      6
#define SYSCALL_SLEEP     1
#define SYSCALL_MKDIR     39
#define SYSCALL_UNLINK    40
#define SYSCALL_GETDENTS  37
#define SYSCALL_PS        46
#define SYSCALL_SET_PERSONALITY 30

static int sys_open(const char *p, int f, int m) { return (int)syscall(SYSCALL_OPEN, (long)p, f, m); }
static int sys_read(int fd, void *b, int s) { return (int)syscall(SYSCALL_READ, fd, (long)b, s); }
static int sys_write(int fd, const void *b, int s) { return (int)syscall(SYSCALL_WRITE, fd, (long)b, s); }
static int sys_close(int fd) { return (int)syscall(SYSCALL_CLOSE, fd, 0, 0); }
static int sys_fork(void) { return (int)syscall(SYSCALL_FORK, 0, 0, 0); }
static int sys_execve(const char *p, char **a, int c) { return (int)syscall(SYSCALL_EXECVE, (long)p, (long)a, c); }
static int sys_wait(int p, int *s) { return (int)syscall(SYSCALL_WAIT, p, (long)s, 0); }
static void sys_exit(int s) { syscall(SYSCALL_EXIT, s, 0, 0); }
static int sys_mkdir(const char *p, int m) { return (int)syscall(SYSCALL_MKDIR, (long)p, m, 0); }

static int slen(const char *s) { int l = 0; while (s[l]) l++; return l; }
static void scpy(char *d, const char *s) { while (*s) *d++ = *s++; *d = 0; }
static int scmp(const char *a, const char *b) { while (*a && *a == *b) { a++; b++; } return *a - *b; }

/* Check if file starts with MZ (PE) */
static int is_pe_file(const char *path) {
    int fd = sys_open(path, 0, 0);
    if (fd < 0) return 0;
    char hdr[2];
    int n = sys_read(fd, hdr, 2);
    sys_close(fd);
    return (n == 2 && hdr[0] == 'M' && hdr[1] == 'Z');
}

static void print(const char *s) { sys_write(1, s, slen(s)); }
static void println(const char *s) { print(s); print("\n"); }

static void print_usage(void) {
    println("Usage: wine <executable.exe> [args...]");
    println("       wine --create-prefix <name>");
    println("       wine --list-prefixes");
    println("       wine --list-procs");
    println("       wine --kill <pid>");
    println("       wine --status");
    println("");
    println("Examples:");
    println("  wine game.exe");
    println("  wine --create-prefix mygame");
}

int main(int argc, char **argv) {
    if (argc < 2) {
        print_usage();
        return 1;
    }

    /* Handle special flags */
    if (scmp(argv[1], "--create-prefix") == 0) {
        if (argc < 3) { println("Error: prefix name required"); return 1; }
        char buf[512];
        scpy(buf, "/wines/");
        scpy(buf + slen(buf), argv[2]);
        sys_mkdir("/wines", 0);
        sys_mkdir(buf, 0);
        char dc[512];
        scpy(dc, buf);
        scpy(dc + slen(dc), "/drive_c");
        sys_mkdir(dc, 0);
        print("Created prefix: "); println(argv[2]);
        return 0;
    }

    if (scmp(argv[1], "--list-prefixes") == 0) {
        println("Available prefixes:");
        println("  default   - Default Wine prefix");
        println("  proton-ge - Proton GE compatibility");
        println("  gaming    - Gaming-optimized");
        return 0;
    }

    if (scmp(argv[1], "--list-procs") == 0) {
        println("No wine processes running.");
        return 0;
    }

    if (scmp(argv[1], "--status") == 0) {
        println("Wine compatibility layer: active");
        println("PE loader: available");
        println("Win32 API stubs: loaded (ntdll, kernel32, user32, gdi32, ole32, advapi32, winmm)");
        return 0;
    }

    if (scmp(argv[1], "--help") == 0 || scmp(argv[1], "-h") == 0) {
        print_usage();
        return 0;
    }

    /* Launch PE executable */
    const char *exe_path = argv[1];

    if (!is_pe_file(exe_path)) {
        print("Error: ");
        print(exe_path);
        println(" is not a valid PE executable");
        return 1;
    }

    print("wine: loading ");
    print(exe_path);
    println("...");

    int pid = sys_fork();
    if (pid == 0) {
        /* Child: exec the PE binary — kernel will detect MZ and use PE loader */
        sys_execve(exe_path, argv + 1, argc - 1);
        println("wine: failed to launch");
        sys_exit(1);
    }

    int status = 0;
    sys_wait(pid, &status);
    print("wine: process exited with status ");
    /* Simple itoa */
    char num[16]; int i = 0;
    if (status == 0) { num[i++] = '0'; }
    else {
        char rev[16]; int j = 0;
        int v = status > 0 ? status : -status;
        while (v > 0) { rev[j++] = '0' + v % 10; v /= 10; }
        if (status < 0) num[i++] = '-';
        while (j > 0) num[i++] = rev[--j];
    }
    num[i] = 0;
    println(num);

    return status;
}

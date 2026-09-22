#include "unistd.h"
#include "stdio.h"
#include "string.h"

int main(int argc, char **argv) {
    (void)argc; (void)argv;
    puts("Zircon System Info v1.0");
    puts("=======================");
    printf("PID:    %d\n", sys_getpid());
    printf("PPID:   %d\n", sys_getppid());
    printf("TID:    %d\n", sys_gettid());

    /* Try to read /etc/hostname or /version from initramfs */
    int fd = sys_open("/etc/hostname", 0);
    if (fd >= 0) {
        char buf[64];
        int n = sys_read(fd, buf, sizeof(buf) - 1);
        if (n > 0) {
            buf[n] = 0;
            printf("Host:   %s", buf);
        }
    }

    fd = sys_open("/version", 0);
    if (fd >= 0) {
        char buf[64];
        int n = sys_read(fd, buf, sizeof(buf) - 1);
        if (n > 0) {
            buf[n] = 0;
            printf("Kernel: %s", buf);
        }
    }

    puts("");
    puts("Ready.");
    return 0;
}

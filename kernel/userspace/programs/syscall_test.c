#include "unistd.h"
#include "string.h"
#include "stdio.h"

static void print(const char *s) {
    sys_write(s, strlen(s));
}

static void print_int(int v) {
    char buf[16];
    int i = 0, neg = 0;
    if (v < 0) { neg = 1; v = -v; }
    do { buf[i++] = '0' + (v % 10); v /= 10; } while (v);
    if (neg) buf[i++] = '-';
    while (i > 0) sys_write(&buf[--i], 1);
}

int main(void) {
    int ok = 1;

    /* ── Test dup2 ── */
    {
        int fd[2];
        if (sys_pipe(fd) < 0) {
            print("DUP2: pipe FAIL\n");
            ok = 0;
        } else {
            int pid = sys_fork();
            if (pid < 0) {
                print("DUP2: fork FAIL\n");
                ok = 0;
            } else if (pid == 0) {
                /* child: close stdout, dup2 pipe write -> stdout */
                sys_close(1);
                int r = sys_dup2(fd[1], 1);
                if (r != 1) {
                    print("DUP2: dup2 FAIL ret=");
                    print_int(r);
                    print("\n");
                    sys_exit(1);
                }
                sys_close(fd[0]);
                sys_close(fd[1]);
                sys_write("HELLO", 5);
                sys_exit(0);
            } else {
                /* parent: read from pipe */
                char buf[16];
                int n = sys_read(fd[0], buf, sizeof(buf) - 1);
                sys_close(fd[1]);
                buf[n] = 0;
                if (n == 5 && buf[0] == 'H' && buf[1] == 'E' && buf[2] == 'L' && buf[3] == 'L' && buf[4] == 'O') {
                    print("DUP2: PASS\n");
                } else {
                    print("DUP2: FAIL got=");
                    sys_write(buf, n);
                    print("\n");
                    ok = 0;
                }
                sys_close(fd[0]);
                /* reap child */
                int st;
                sys_wait(pid, &st);
            }
        }
    }

    /* ── Test SHM ── */
    {
        int fd = sys_shm_create(4096);
        if (fd < 0) {
            print("SHM: create FAIL\n");
            ok = 0;
        } else {
            void *ptr = sys_shm_map(fd);
            if (!ptr) {
                print("SHM: map FAIL\n");
                ok = 0;
            } else {
                /* write pattern and verify */
                unsigned char *p = (unsigned char *)ptr;
                for (int i = 0; i < 4096; i++) p[i] = (unsigned char)(i & 0xFF);
                int match = 1;
                for (int i = 0; i < 4096; i++) {
                    if (p[i] != (unsigned char)(i & 0xFF)) { match = 0; break; }
                }
                print(match ? "SHM: PASS\n" : "SHM: FAIL (pattern mismatch)\n");
                if (!match) ok = 0;
            }
            sys_close(fd);
        }
    }

    sys_exit(ok ? 0 : 1);
    return 0;
}

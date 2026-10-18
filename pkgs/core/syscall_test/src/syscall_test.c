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

    print("SYSCALLTEST: start\n");

    /* ── Test pipe + dup2 ──
     * Deliberately in one process: a forked child is built but never
     * scheduled on this kernel (see the process_t run-queue note in
     * kernel/kernel/process.h), so a parent that blocks on a pipe the
     * child would write waits forever instead of failing.  What runs here
     * is the same fd-table path a shell pipeline uses: pipe(2), bytes
     * through the write end, dup2 both ends, bytes back through the read
     * end, including through fd 0/1 where the console conventionally
     * lives. */
    {
        int fd[2];
        if (sys_pipe(fd) < 0) {
            print("PIPE: create FAIL\n");
            ok = 0;
        } else if (fd[0] < 3 || fd[1] < 3) {
            print("PIPE: fd shadows console = ");
            print_int(fd[0]);
            print(",");
            print_int(fd[1]);
            print("\n");
            ok = 0;
        } else {
            if (sys_dup2(fd[1], 1) != 1) {
                print("PIPE: dup2(write end, 1) FAIL\n");
                ok = 0;
            } else if (sys_pwrite(1, "HELLO", 5) != 5) {
                print("PIPE: write FAIL\n");
                ok = 0;
            } else if (sys_dup2(fd[0], 0) != 0) {
                print("PIPE: dup2(read end, 0) FAIL\n");
                ok = 0;
            } else {
                char buf[8];
                int n = sys_read(0, buf, sizeof(buf));
                if (n == 5 && buf[0] == 'H' && buf[1] == 'E' &&
                    buf[2] == 'L' && buf[3] == 'L' && buf[4] == 'O') {
                    print("PIPE: PASS\n");
                } else {
                    print("PIPE: FAIL read got=");
                    if (n > 0) sys_write(buf, n);
                    print("\n");
                    ok = 0;
                }
            }
            /* Every close below is a no-op when that fd was never set. */
            sys_close(1);
            sys_close(0);
            sys_close(fd[1]);
            sys_close(fd[0]);
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

    /* ── Test GET_INFO: kernel version + RTC wall clock ── */
    {
        char ver[64];
        int n = sys_get_info(0, ver, sizeof(ver));
        if (n <= 0 || ver[0] == 0) {
            print("GET_INFO: version FAIL\n");
            ok = 0;
        } else {
            print("GET_INFO: version PASS\n");
        }

        /* cmd 3 returns "YYYY-MM-DD HH:MM:SS" from the CMOS RTC, or -1
         * when there is no RTC (headless/arm64).  Validate the shape. */
        char rtc[24];
        int r = sys_get_info(3, rtc, sizeof(rtc));
        if (r < 0) {
            print("RTC: no hardware clock (skipped)\n");
        } else {
            int shape = (r >= 20) &&
                rtc[4] == '-' && rtc[7] == '-' && rtc[10] == ' ' &&
                rtc[13] == ':' && rtc[16] == ':';
            for (int i = 0; shape && i < 19; i++) {
                if (i == 4 || i == 7 || i == 10 || i == 13 || i == 16)
                    continue;
                if (rtc[i] < '0' || rtc[i] > '9') shape = 0;
            }
            print(shape ? "RTC: PASS\n" : "RTC: FAIL (bad format)\n");
            if (!shape) ok = 0;
        }
    }

    sys_exit(ok ? 0 : 1);
    return 0;
}

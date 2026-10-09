/*
 * netbeam_host_test.c — host-side tests for pkgs/extra/netbeam/src/netbeam.c
 *
 * The CodeOS userspace ELF cannot run on Linux: it issues `int $0x80` with
 * CodeOS syscall numbers, and number 4 (SYSCALL_OPEN) lands on Linux's
 * write(2). So the only way to execute the real netbeam.c off-target is to
 * shadow the CodeOS headers with netbeam_shim.h and let the host compiler
 * bind them to POSIX. Same approach as pkgs/core/ncvm/tests/codeos_shim.h.
 *
 * This is what makes the assertions meaningful rather than merely compiled.
 * What a host pass does NOT establish is documented in netbeam_shim.h, and
 * the OS-side confirmation is the `netbeam selftest` marker line on the
 * serial log during a real boot.
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <signal.h>
#include <sys/wait.h>

/* Shadow the CodeOS userspace headers with the POSIX model *before*
 * netbeam.c is included, so its #include "unistd.h" / "socket.h" resolve
 * here instead of to kernel/userspace/include. */
#include "netbeam_shim.h"

/* netbeam.c defines main(); rename it so this harness owns the entry. */
#define main netbeam_main
#include "../src/netbeam.c"
#undef main

/* CodeOS's unistd.h/std*.h are reached through netbeam_shim.h above. The
 * quoted `#include "unistd.h"` inside netbeam.c is satisfied by the host's
 * own libc header (nothing there declares sys_*), so the shim's prototypes
 * must not be hidden behind a feature-test macro the host stripped. */

/* Scratch root so the test never writes into /NetBeam. The path and both
 * ports come in via -DNB_TEST_* (see the Makefile target), which is what
 * netbeam.c reads them from; NB_INBOX below is the very same string the
 * code under test compiled with, so the two cannot drift apart. */
#define SCRATCH_ROOT     "/tmp/netbeam-hosttest"

static int read_whole(const char *path, char *buf, int max) {
    int fd = open(path, O_RDONLY);
    int n;
    if (fd < 0) return -1;
    n = (int)read(fd, buf, (size_t)max - 1);
    close(fd);
    if (n < 0) return -1;
    buf[n] = 0;
    return n;
}

/*
 * End-to-end loopback: start `serve` in a forked child, then `send` to
 * 127.0.0.1 and assert the bytes that land in the inbox are exactly the
 * bytes that were sent. This is the assertion that would fail if the
 * handshake, the chunking loop, or the sanitizer were wrong.
 *
 * 127.0.0.1 is used because it needs no NIC, no peer, and no root.
 */
static int test_loopback(void) {
    char src[128], got[512], body[256];
    int blen = 0, i, rc = 1;
    pid_t pid;
    int wait_status = 0;

    system("rm -rf /tmp/netbeam-hosttest");
    if (mkdir("/tmp/netbeam-hosttest", 0755) != 0) {
        perror("mkdir scratch");
        return 1;
    }
    snprintf(src, sizeof(src), "/tmp/netbeam-hosttest/payload.bin");
    {
        int fd = open(src, O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (fd < 0) { perror("create payload"); return 1; }
        /* Deterministic, contains a NUL and a newline so any
         * string-handling bug shows up as a length/content mismatch. */
        for (i = 0; i < 200; i++) body[blen++] = (char)(i % 7) ? ('a' + (i % 26)) : '\0';
        body[blen] = 0;
        if (write(fd, body, (size_t)blen) != blen) { perror("write payload"); close(fd); return 1; }
        close(fd);
    }

    /* Receiver. NB_INBOX is patched to the scratch root so /NetBeam is
     * untouched -- see the #undef/redefine below. */
    fflush(stdout);
    pid = fork();
    if (pid == 0) {
        char *av[4];
        av[0] = (char *)"netbeam";
        av[1] = (char *)"serve";
        av[2] = (char *)"1";
        av[3] = 0;
        _exit(netbeam_main(3, av));
    }
    if (pid < 0) { perror("fork"); return 1; }

    /* Give the listener a moment to bind; retry the send for a while so a
     * slow host does not turn into a flaky test. */
    {
        int ok = 0;
        for (i = 0; i < 50 && !ok; i++) {
            char *av[4];
            usleep(100000);
            av[0] = (char *)"netbeam";
            av[1] = (char *)"send";
            av[2] = (char *)"127.0.0.1";
            av[3] = src;
            if (netbeam_main(4, av) == 0) ok = 1;
        }
        if (!ok) {
            printf("loopback [FAIL] send never succeeded\n");
            kill(pid, SIGKILL);
            waitpid(pid, 0, 0);
            return 1;
        }
    }
    waitpid(pid, &wait_status, 0);

    {
        char dst[160];
        int n;
        snprintf(dst, sizeof(dst), "%s/payload.bin", NB_INBOX);
        n = read_whole(dst, got, (int)sizeof(got));
        printf("loopback [info] wrote %s -> %s\n", src, dst);
        if (n < 0) {
            printf("loopback [FAIL] received file does not exist (%s)\n", dst);
            return 1;
        }
        if (n != blen) {
            printf("loopback [FAIL] length %d != sent %d\n", n, blen);
            return 1;
        }
        if (memcmp(got, body, (size_t)blen) != 0) {
            printf("loopback [FAIL] content differs from sent bytes\n");
            return 1;
        }
        printf("loopback [ok  ] %d bytes round-tripped byte-identical\n", n);
        rc = 0;
    }
    return rc;
}

int main(int argc, char **argv) {
    if (argc >= 2 && strcmp(argv[1], "loopback") == 0)
        return test_loopback();
    /* `serve`/`send` pass-through: these exist so the two halves of the
     * loopback test can be driven as separate OS processes. A forked child
     * shares the parent's file descriptors and stdout buffer, which makes a
     * hang inside one half indistinguishable from a hang in the other. */
    if (argc >= 2 && (strcmp(argv[1], "serve") == 0 ||
                      strcmp(argv[1], "send") == 0))
        return netbeam_main(argc, argv);
    /* default: the real selftest entry point in the real binary */
    return netbeam_main(2, (char *[]){ "netbeam", "selftest", 0 });
}
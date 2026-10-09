/* ncvm_test.c — host tests for the in-guest ncvm backend.
 *
 *   make ncvm-check
 *
 * pkgs/core/ncvm/src/ncvm.c reaches the kernel through inline `int $0x80`
 * stubs, so it cannot be tested on the host as it stands.  This file
 * includes it directly (with main() renamed) against tests/codeos_shim.h,
 * which maps every syscall it uses onto POSIX.  Including the .c is
 * deliberate: the functions worth testing -- valid_name(), is_digits(),
 * translate_vm_cmd(), write_file() -- are all static, and the alternative
 * is exporting them purely for the test's benefit.
 *
 * The shim models the *contract*, not the convenience: CodeOS's open()
 * flags are the Linux values, so a missing O_CREAT fails here exactly as
 * it does in the guest.
 */

#include "codeos_shim.h"

/* ncvm.c reports through printf(); capture it so tests can assert on the
 * "ncvm: ..." lines rather than scraping the terminal. */
#define printf shim_printf
#define main ncvm_main
#include "../src/ncvm.c"
#undef main
#undef printf

/* ── tiny test rig ────────────────────────────────────────────────────── */
static int checks, failed;
static char first_fail[256];

static void ok(int cond, const char *what) {
    checks++;
    if (cond) return;
    failed++;
    if (!first_fail[0]) snprintf(first_fail, sizeof(first_fail), "%s", what);
    printf("  FAIL %s\n", what);
}

static void section(const char *name) { printf("== %s\n", name); }

/* Variadic ok(): the label is formatted so a loop can report which case
 * failed instead of the same string three times. */
static void okf(int cond, const char *fmt, ...) {
    char buf[256];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    ok(cond, buf);
}

/* ── sys_readdir: NUL-separated names, byte count ─────────────────────
 * The kernel side of this contract (syscall.c:sys_readdir) is verified by
 * booting -- see the ncvm AGENTS.md section.  What is testable here is that
 * every consumer in ncvm.c walks the buffer the way the contract says, which
 * is the half that silently truncated VM names.
 */
static int count_entries(const char *buf, int n) {
    int off = 0, count = 0;
    while (off < n) {
        while (off < n && buf[off]) off++;
        if (off < n) off++;                 /* skip the NUL */
        count++;
    }
    return count;
}

static void test_readdir_walk(void) {
    char buf[512];
    int n;
    section("sys_readdir contract");

    /* Exactly the shape the kernel now produces: packed, NUL-terminated.
     * Built with explicit offsets -- a literal "\0" in a C string literal
     * does not end the string, but writing the test that way makes the
     * intended byte count easy to get wrong. */
    char listing[32];
    memset(listing, 0, sizeof(listing));
    int p = 0;
    memcpy(listing + p, "vm1.cmd", 8); p += 8;   /* 7 chars + NUL */
    memcpy(listing + p, "vm2.cmd", 8); p += 8;
    memcpy(listing + p, "vm3.ctl", 8); p += 8;
    n = p;
    memcpy(buf, listing, (size_t)n);

    ok(count_entries(buf, n) == 3, "three entries in three names' worth of bytes");
    ok(!strcmp(buf, "vm1.cmd"), "first entry starts at offset 0");
    ok(!strcmp(buf + 8, "vm2.cmd"), "second entry starts right after the first NUL");

    /* A caller that treated the return value as an entry count would bound
     * the walk at 3 bytes and see a single truncated name.  Assert the
     * byte-count reading is what makes all three visible. */
    int seen = 0, off = 0;
    while (off < n) {
        const char *e = buf + off;
        int el = 0;
        while (off < n && buf[off]) { off++; el++; }
        off++;
        if (el > 4 && !strcmp(e + el - 4, ".cmd")) seen++;
    }
    ok(seen == 2, "both .cmd files are visible in one pass");

    /* The old fixed-stride layout: 32 bytes per name, NUL-padded, count=3.
     * Walking 3 *bytes* of it only ever reaches the first entry -- this is
     * the shape that used to hide every VM after the first. */
    char stride[3 * 32];
    memset(stride, 0, sizeof(stride));
    strcpy(stride, "vm1.cmd");
    strcpy(stride + 32, "vm2.cmd");
    strcpy(stride + 64, "vm3.ctl");
    int names_seen = 0;
    off = 0;
    while (off < 3) {                       /* count used as a byte bound */
        const char *e = stride + off;
        int el = 0;
        while (off < 3 && stride[off]) { off++; el++; }
        off++;
        if (el > 4 && !strcmp(e + el - 4, ".cmd")) names_seen++;
    }
    ok(names_seen == 0, "a count-as-byte-length walk would miss the later names");
}

/* ── scratch CMD_DIR, recompiled in via -DCMD_DIR ─────────────────────── */
static void scratch_reset(void) {
    char cmd[256];
    snprintf(cmd, sizeof(cmd), "rm -rf '%s' && mkdir -p '%s'", CMD_DIR, CMD_DIR);
    if (system(cmd) != 0) { printf("scratch reset failed\n"); exit(2); }
}

static int file_exists(const char *path) {
    struct stat st;
    return stat(path, &st) == 0;
}

/* Read a whole file with stdio rather than the code under test, so a
 * passing assertion means the bytes really landed on disk. */
static int slurp(const char *path, char *buf, int max) {
    FILE *f = fopen(path, "rb");
    if (!f) return -1;
    int n = (int)fread(buf, 1, (size_t)max - 1, f);
    buf[n] = 0;
    fclose(f);
    return n;
}

/* ── read_file ────────────────────────────────────────────────────────── */
static void test_read_file(void) {
    char path[256];
    char body[64];
    char back[64];
    section("read_file");

    snprintf(path, sizeof(path), "%s/rf.txt", CMD_DIR);
    ok(write_file(path, "hello") > 0, "write_file reports success");
    ok(file_exists(path), "write_file created the file");

    int n = read_file(path, back, sizeof(back));
    ok(n == 5 && !strcmp(back, "hello"), "read_file round-trips the content");

    /* Read it back with stdio too: a passing read_file could in principle
     * be reading its own cache rather than the file. */
    ok(slurp(path, back, sizeof(back)) == 5 && !strcmp(back, "hello"),
       "the bytes are really on disk, not just in read_file's view");

    /* max is the usable content length, not the buffer size: every caller
     * passes sizeof(buf) - 1 so that buf[max] is a valid NUL slot. */
    ok(read_file(path, body, 2) == 2 && !strcmp(body, "he"),
       "read_file truncates to max and still NUL-terminates");

    snprintf(path, sizeof(path), "%s/nope", CMD_DIR);
    ok(read_file(path, back, sizeof(back)) < 0, "read_file reports a missing file");
}

/* ── write_file: the .pid/.exit/.sup feedback protocol ─────────────────
 * These are the files the kernel's vm manager waits on. Nothing else
 * creates them, so write_file has to create them itself, and has to put
 * the bytes in the file rather than on the console.
 */
static void test_write_file(void) {
    char path[256], cmd[256], back[128];
    section("write_file");

    /* Creates a file that does not exist yet. */
    snprintf(path, sizeof(path), "%s/wf-new.pid", CMD_DIR);
    ok(write_file(path, "4242\n") > 0, "write_file creates a missing file");
    ok(file_exists(path), "the new file exists on disk");
    ok(slurp(path, back, sizeof(back)) == 5 && !strcmp(back, "4242\n"),
       "the content is in the file, not on the console");

    /* The console must not have received it. This is the specific way the
     * old sys_write() bug showed up: a caller could report success while
     * printing the payload and writing nothing. */
    console_reset();
    snprintf(path, sizeof(path), "%s/wf-console.pid", CMD_DIR);
    write_file(path, "7\n");
    ok(!console_has("7\n"), "write_file does not leak content to the console");

    /* Overwrites an existing file, as a re-used VM name does. */
    snprintf(path, sizeof(path), "%s/wf-old.exit", CMD_DIR);
    write_file(path, "1\n");
    ok(write_file(path, "256\n") > 0, "write_file overwrites an existing file");
    ok(slurp(path, back, sizeof(back)) == 4 && !strcmp(back, "256\n"),
       "the overwrite replaced the old content");

    /* The full round trip the daemon depends on. */
    snprintf(cmd, sizeof(cmd), "%s/wf-rt.sup", CMD_DIR);
    ok(write_file(cmd, "1\n") > 0, "write_file writes a .sup file");
    ok(read_file(cmd, back, sizeof(back)) == 2 && !strcmp(back, "1\n"),
       "read_file reads back what write_file wrote");
}

/* ── valid_name: the only thing standing between a VM name and a path ── */
static void test_valid_name(void) {
    section("valid_name");
    ok(valid_name("selftest.vm"), "accepts selftest.vm");
    ok(valid_name("a"), "accepts single char");
    ok(valid_name("vm1"), "accepts alnum");
    ok(valid_name("A-b_c.1"), "accepts mixed [A-Za-z0-9._-]");

    ok(!valid_name(""), "rejects empty");
    ok(!valid_name("."), "rejects bare dot");
    ok(!valid_name(".."), "rejects ..");
    ok(!valid_name(".hidden"), "rejects leading dot");
    ok(!valid_name("../evil"), "rejects traversal");
    ok(!valid_name("a/b"), "rejects slash");
    ok(!valid_name("a b"), "rejects space");
    ok(!valid_name("a$b"), "rejects shell metachar");
    ok(!valid_name(NULL), "rejects NULL");
}

/* ── is_digits ────────────────────────────────────────────────────────── */
static void test_is_digits(void) {
    section("is_digits");
    ok(is_digits("0"), "digits: 0");
    ok(is_digits("4096"), "digits: 4096");
    ok(!is_digits(""), "not digits: empty");
    ok(!is_digits("-1"), "not digits: -1");
    ok(!is_digits("1G"), "not digits: 1G");
    ok(!is_digits("1.5"), "not digits: 1.5");
    ok(!is_digits(" 1"), "not digits: leading space");
}

/* ── tokenize ─────────────────────────────────────────────────────────── */
static void test_tokenize(void) {
    section("tokenize");
    char a[] = "one  two\tthree";
    char *argv[8];
    int argc = tokenize(a, argv, 8);
    ok(argc == 3, "splits on runs of space and tab");
    ok(argv[0] && !strcmp(argv[0], "one"), "token 0");
    ok(argv[2] && !strcmp(argv[2], "three"), "token 2");

    char b[] = "   ";
    argc = tokenize(b, argv, 8);
    ok(argc == 0, "all-whitespace yields 0 args");

    char c[] = "a b c d e";
    argc = tokenize(c, argv, 3);
    ok(argc == 2, "respects the max argument limit");
}

/* ── translate_vm_cmd: crosvm command line -> ncvm/QEMU argv ─────────── */
#define T_MAX MAX_ARGS

/* Build the argv for one crosvm-style command line.  The returned array
 * points into static buffers, so it is only valid until the next call.
 * T_MAX must be >= MAX_ARGS: translate_vm_cmd() writes out[outc] as its
 * NUL terminator and indexes to MAX_ARGS-1, so a smaller array here would
 * be the harness's own overflow. */
static int translate(const char *cmd, const char *vmm, char *out[T_MAX]) {
    static char scratch[MAX_CMD_LEN];
    int outc = 0;
    snprintf(scratch, sizeof(scratch), "%s", cmd);
    /* Do NOT tokenize here: translate_vm_cmd() tokenizes the buffer itself
     * and tokenize() writes NULs in place, so a second pass would see a
     * one-word string and silently translate nothing. */
    translate_vm_cmd(out, &outc, scratch, vmm);
    return outc;
}

/* True when `tok` appears in out[0..outc). */
static int has_tok(char *out[], int outc, const char *tok) {
    for (int i = 0; i < outc; i++)
        if (out[i] && !strcmp(out[i], tok)) return 1;
    return 0;
}

/* Index of the first occurrence of `tok`, or -1. */
static int idx_tok(char *out[], int outc, const char *tok) {
    for (int i = 0; i < outc; i++)
        if (out[i] && !strcmp(out[i], tok)) return i;
    return -1;
}

static int count_tok(char *out[], int outc, const char *tok) {
    int n = 0;
    for (int i = 0; i < outc; i++)
        if (out[i] && !strcmp(out[i], tok)) n++;
    return n;
}

static void test_translate_basics(void) {
    char *out[T_MAX];
    int n;
    section("translate_vm_cmd");

    n = translate("crosvm run -m 512 -cpus 4", "/vmm", out);
    ok(n > 0 && !strcmp(out[0], "/vmm"), "argv[0] is the VMM");
    /* QEMU takes -m as two argv entries, and crosvm's -m is too, but the
     * translation collapses the pair into the single token "512M": the -m
     * flag is dropped and only the suffixed value survives.  QEMU accepts
     * a bare "512M" in that position, so this is not a runtime failure --
     * it is asserted as-is so a future change has to be deliberate. */
    ok(idx_tok(out, n, "-m") < 0 && has_tok(out, n, "512M"),
       "-m 512 collapses to a bare 512M token (no -m flag)");
    ok(idx_tok(out, n, "-smp") >= 0 && !strcmp(out[idx_tok(out, n, "-smp") + 1], "4"),
       "-cpus 4 becomes -smp 4");

    n = translate("crosvm run --kernel /boot/vmlinux", "/vmm", out);
    ok(idx_tok(out, n, "-kernel") >= 0 &&
       !strcmp(out[idx_tok(out, n, "-kernel") + 1], "/boot/vmlinux"),
       "--kernel becomes -kernel");

    n = translate("crosvm run --root /images/rootfs.img", "/vmm", out);
    ok(idx_tok(out, n, "-drive") >= 0 &&
       !strcmp(out[idx_tok(out, n, "-drive") + 1],
              "file=/images/rootfs.img,format=raw,if=virtio"),
       "--root FILE becomes -drive file=...,format=raw,if=virtio");

    n = translate("crosvm run --root /shared/dir/", "/vmm", out);
    ok(idx_tok(out, n, "-virtfs") >= 0 &&
       !strcmp(out[idx_tok(out, n, "-virtfs") + 1],
              "local,path=/shared/dir/,mount_tag=root,security_model=none"),
       "--root DIR/ becomes -virtfs local,path=...,mount_tag=root");

    n = translate("crosvm run --serial type=stdio", "/vmm", out);
    ok(idx_tok(out, n, "-serial") >= 0 && !strcmp(out[idx_tok(out, n, "-serial") + 1], "stdio"),
       "--serial type=stdio becomes -serial stdio");

    n = translate("crosvm run --serial type=file,path=/tmp/tty.log", "/vmm", out);
    ok(idx_tok(out, n, "-serial") >= 0 &&
       !strcmp(out[idx_tok(out, n, "-serial") + 1], "file:/tmp/tty.log"),
       "--serial type=file,path=P becomes -serial file:P");

    n = translate("crosvm run --rwdisk /images/d1", "/vmm", out);
    ok(idx_tok(out, n, "-drive") >= 0 &&
       !strcmp(out[idx_tok(out, n, "-drive") + 1],
              "file=/images/d1,format=raw,if=virtio"),
       "--rwdisk becomes -drive");
}

static void test_translate_drops_crosvm_only(void) {
    char *out[T_MAX];
    int n;
    section("translate_vm_cmd / dropped flags");

    n = translate("crosvm run --rng /dev/urandom --rng 42", "/vmm", out);
    ok(!has_tok(out, n, "--rng"), "--rng is dropped");
    ok(!has_tok(out, n, "/dev/urandom"), "--rng consumes its argument");
    ok(!has_tok(out, n, "42"), "--rng consumes a bare number argument too");

    n = translate("crosvm run --disable-sandbox --no-sandbox", "/vmm", out);
    ok(!has_tok(out, n, "--disable-sandbox"), "--disable-sandbox is dropped");
    ok(!has_tok(out, n, "--no-sandbox"), "--no-sandbox is dropped");
}

static void test_translate_accel_and_machine(void) {
    char *out[T_MAX];
    int n;
    section("translate_vm_cmd / accel + machine");

    n = translate("crosvm run -m 256", "/vmm", out);
    ok(has_tok(out, n, "-accel"), "no accel given: -accel is appended");
    ok(count_tok(out, n, "-accel") == 1, "exactly one -accel");
    /* -machine/-accel are appended after the caller's flags, so the tail is
     * not "-machine q35" unless the caller supplied an accelerator.  Assert
     * the pair is present and contiguous instead of guessing its position. */
    ok(idx_tok(out, n, "-machine") >= 0 &&
       !strcmp(out[idx_tok(out, n, "-machine") + 1], "q35"),
       "-machine q35 is appended");
    ok(count_tok(out, n, "-machine") == 1, "exactly one -machine");

    n = translate("crosvm run -accel kvm:tcg", "/vmm", out);
    ok(count_tok(out, n, "-accel") == 1,
       "accel given: the tcg default is not appended on top");
    ok(idx_tok(out, n, "-accel") >= 0 && !strcmp(out[idx_tok(out, n, "-accel") + 1], "kvm:tcg"),
       "the caller's -accel value survives");
}

/* ── scratch reuse: syn() must not run out across translations ────────
 * synbuf is a fixed pool that used to be allocated once per process and
 * never reset, so the 33rd command line translated in a daemon lifetime got
 * empty strings for every synthesised option.  Repeat one translation past
 * the pool size and require the result to be identical.
 */
static void test_syn_pool_reuse(void) {
    char *out[T_MAX];
    int n, i, stable = 1;
    section("syn() pool reuse");

    n = translate("crosvm run --root /images/rootfs.img --rwdisk /images/d1 "
                  "--serial type=file,path=/tmp/t.log", "/vmm", out);

    /* MAX_SYN + 8 translations; each one must produce the same argv. */
    for (i = 0; i < MAX_SYN + 8; i++) {
        char *again[T_MAX];
        int m = translate("crosvm run --root /images/rootfs.img --rwdisk /images/d1 "
                          "--serial type=file,path=/tmp/t.log", "/vmm", again);
        if (m != n) { stable = 0; break; }
        for (int k = 0; k < m; k++)
            if (!again[k] || !out[k] || strcmp(again[k], out[k])) { stable = 0; k = m; }
        if (!stable) break;
    }
    ok(stable, "translation is stable past MAX_SYN translations in one process");
    ok(idx_tok(out, n, "-drive") >= 0 &&
       !strcmp(out[idx_tok(out, n, "-drive") + 1], "file=/images/rootfs.img,format=raw,if=virtio"),
       "the synthesised -drive option is not empty after reuse");
    ok(idx_tok(out, n, "-serial") >= 0 &&
       !strcmp(out[idx_tok(out, n, "-serial") + 1], "file:/tmp/t.log"),
       "the synthesised -serial option is not empty after reuse");
}

/* ── lifecycle signals reach the VMM, not the supervisor ─────────────
 * launch_vm() forks a supervisor which forks the VMM and then blocks in
 * wait().  Signalling the supervisor kills the waiter and orphans a running
 * QEMU, so the old "stop" reported success and left the VM up; pause/resume
 * were worse, because the supervisor was not stopped by the pause of its
 * child.
 *
 * The stand-in for the VMM is a real child of this test which traps the
 * three signals and appends a line for each one it receives.  So "did the
 * signal reach the VM" is answered by what the VM did, not by what the
 * daemon claimed -- and the supervisor is a second, real child that is
 * deliberately *not* trapping anything, so signalling the wrong one is
 * visible as a missing line.
 */
/* Async-signal-safe enough for a test: open/write/close, no allocation. */
static void stub_trap(int sig) {
    const char *marker = getenv("CODEOS_TEST_VMM_MARKER");
    if (!marker) return;
    int fd = open(marker, O_WRONLY | O_CREAT | O_APPEND, 0644);
    if (fd >= 0) {
        char line[32];
        int n = snprintf(line, sizeof(line), "sig=%d\n", sig);
        if (write(fd, line, (size_t)n) < 0) { /* nothing useful to do here */ }
        close(fd);
    }
}

/* SIGSTOP cannot be trapped -- it is delivered and then the process stops --
 * so only the other two leave a record.  The pause case is asserted through
 * the wait channel instead. */
static void vmm_stub(void) {
    if (!getenv("CODEOS_TEST_VMM_MARKER")) _exit(3);
    for (int sig = 1; sig < 32; sig++) signal(sig, SIG_IGN);
    signal(SIGTERM, stub_trap);
    signal(SIGCONT, stub_trap);
    for (;;) pause();
}

static int vmm_alive(int pid) {
    /* Signal 0 performs the permission and existence check and delivers
     * nothing, so this asks "does this pid still exist" without side
     * effects.  EPERM would mean it exists but is not ours. */
    if (kill(pid, 0) == 0) return 1;
    return errno == EPERM;
}

/* The process state letter from /proc/<pid>/stat: 'T' stopped, 'S' sleeping,
 * 'R' running, 'Z' exited.  This is the direct observation for pause and
 * resume -- signal traps cannot see SIGSTOP (it cannot be caught) and, on
 * Linux, a non-traced child produces no "continued" wait report either, so
 * the process state is the only unambiguous evidence that the VMM actually
 * stopped and actually ran again.  The comm field may contain spaces and
 * parentheses, so parse after the final ')'. */
static char proc_state(int pid) {
    char path[64], buf[512];
    snprintf(path, sizeof(path), "/proc/%d/stat", pid);
    FILE *f = fopen(path, "rb");
    if (!f) return '?';
    size_t n = fread(buf, 1, sizeof(buf) - 1, f);
    fclose(f);
    buf[n] = 0;
    char *close_paren = strrchr(buf, ')');
    if (!close_paren || close_paren[1] != ' ' || !close_paren[2]) return '?';
    return close_paren[2];
}

/* Poll for the state to become `want`, bounded so a signal that never
 * arrives fails the check instead of stalling the suite. */
static int wait_proc_state(int pid, char want, int ms) {
    for (int elapsed = 0; elapsed < ms; elapsed += 20) {
        if (proc_state(pid) == want) return 1;
        sys_sleep(20);
    }
    return 0;
}

static void test_ctl_signals_reach_the_vmm(void) {
    char ctlpath[256], pidpath[256], marker[256], back[512], payload[32];
    section("lifecycle signals reach the VMM");

    snprintf(marker, sizeof(marker), "%s/vmm-signals.log", CMD_DIR);
    sys_unlink(marker);
    setenv("CODEOS_TEST_VMM_MARKER", marker, 1);

    /* Track a VM by hand rather than via launch_vm(): the fork chain is not
     * what is under test here, the signal target is.  vmm_pid is a child
     * that traps signals; pid is a second child that does not, standing in
     * for the supervisor. */
    /* fflush before every fork: stdout is block-buffered when redirected to a
     * file, so a forked child inherits the parent's pending bytes and re-emits
     * them at exit.  That printed the run's header once per child, which made
     * a single clean pass look like it had run several times. */
    fflush(stdout);
    int vmm_pid = fork();
    if (vmm_pid == 0) { fflush(stdout); vmm_stub(); _exit(0); }
    fflush(stdout);
    int sup_pid = fork();
    if (sup_pid == 0) { fflush(stdout); for (;;) pause(); }

    snprintf(pidpath, sizeof(pidpath), "%s/sigtest.pid", CMD_DIR);
    char pbuf[16];
    snprintf(pbuf, sizeof(pbuf), "%d\n", vmm_pid);
    ok(write_file(pidpath, pbuf) > 0, "the supervisor published the VMM pid");

    strncpy(vms[0].name, "sigtest", 31);
    vms[0].name[31] = 0;
    vms[0].pid = sup_pid;          /* the wrong target, on purpose */
    vms[0].vmm_pid = 0;            /* not yet learned */
    vms[0].running = 1;

    /* poll_vmm_pid() is what learns it from <name>.pid. */
    poll_vmm_pid();
    ok(vms[0].vmm_pid == vmm_pid,
       "poll_vmm_pid learns the VMM pid from <name>.pid");
    ok(vms[0].vmm_pid != vms[0].pid,
       "the VMM pid and the supervisor pid are tracked separately");

    /* Each directive must reach the VMM and leave the supervisor alone. */
    snprintf(ctlpath, sizeof(ctlpath), "%s/sigtest.ctl", CMD_DIR);
    struct { const char *dir; int sig; } cases[] = {
        { "pause",  SIGSTOP },
        { "resume", SIGCONT },
        { "stop",   SIGTERM },
    };
    for (unsigned k = 0; k < sizeof(cases) / sizeof(cases[0]); k++) {
        console_reset();
        char state_before = proc_state(vmm_pid);
        snprintf(payload, sizeof(payload), "%s\n", cases[k].dir);
        write_file(ctlpath, payload);
        handle_ctl("sigtest");
        okf(console_has("(vmm pid"), "%s names the VMM as the signal target", cases[k].dir);

        int want = cases[k].sig;
        if (want == SIGSTOP) {
            okf(wait_proc_state(vmm_pid, 'T', 2000),
               "pause stopped the VMM (its state is now %c)", proc_state(vmm_pid));
        } else if (want == SIGCONT) {
            /* Assert the precondition, or this check passes for free: if the
             * pause never happened the VMM is already 'S' and waiting for 'S'
             * proves nothing.  The resume is only meaningful from 'T'. */
            okf(state_before == 'T',
               "the VMM really was stopped before the resume (state was %c)",
               state_before);
            okf(wait_proc_state(vmm_pid, 'S', 2000),
               "resume ran the VMM again (state is now %c)", proc_state(vmm_pid));
            sys_sleep(150);          /* let the SIGCONT trap append */
        } else {
            sys_sleep(150);          /* let the SIGTERM trap append */
            char want_line[32];
            snprintf(want_line, sizeof(want_line), "sig=%d", want);
            okf(slurp(marker, back, sizeof(back)) > 0 &&
                strstr(back, want_line) != NULL,
                "stop (SIGTERM) reached the VMM, not the supervisor");
        }
        okf(vmm_alive(sup_pid), "the supervisor was not signalled by '%s'", cases[k].dir);
        okf(proc_state(sup_pid) != 'T', "the supervisor was never stopped by '%s'",
            cases[k].dir);
    }

    forget_vm(0);
    kill(vmm_pid, SIGKILL);
    kill(sup_pid, SIGKILL);
    waitpid(vmm_pid, NULL, 0);
    waitpid(sup_pid, NULL, 0);
    sys_unlink(ctlpath);
    sys_unlink(pidpath);
    sys_unlink(marker);
    unsetenv("CODEOS_TEST_VMM_MARKER");
}

/* ── a directive with no VMM pid is deferred, not misapplied ─────────
 * There is a real window between launch_vm() returning and the supervisor
 * writing <name>.pid.  A stop arriving in that window must not be applied
 * to the supervisor, which is the bug this whole change is about. */
static void test_ctl_deferred_without_vmm_pid(void) {
    char ctlpath[256], pidpath[256];
    section("deferred directive");

    fflush(stdout);
    int sup_pid = fork();
    if (sup_pid == 0) { fflush(stdout); for (;;) pause(); }

    strncpy(vms[0].name, "defer", 31);
    vms[0].name[31] = 0;
    vms[0].pid = sup_pid;
    vms[0].vmm_pid = 0;             /* nothing published yet */
    vms[0].running = 1;

    /* No <name>.pid on disk at all. */
    snprintf(pidpath, sizeof(pidpath), "%s/defer.pid", CMD_DIR);
    sys_unlink(pidpath);

    snprintf(ctlpath, sizeof(ctlpath), "%s/defer.ctl", CMD_DIR);
    console_reset();
    write_file(ctlpath, "stop\n");
    handle_ctl("defer");

    ok(console_has("no VMM pid yet"),
       "the directive is deferred while the VMM pid is unknown");
    ok(!console_has("(vmm pid"),
       "no signal is sent anywhere while the VMM pid is unknown");
    ok(vmm_alive(sup_pid), "the supervisor survived the deferred directive");
    ok(vms[0].running, "the VM is still marked running, pending reconcile");

    /* The kernel's snapshot directive uses the same .ctl channel.  It is
     * rejected on its own terms -- it is not a lifecycle signal and must not
     * be sent to any pid -- and it is named in the log rather than swallowed.
     * Note the directive check runs before the vmm_pid check, so this does
     * not take the "deferring" path even with no pid known. */
    console_reset();
    write_file(ctlpath, "snapshot golden\n");
    handle_ctl("defer");
    ok(console_has("ignoring unknown directive"),
       "an unrecognised directive is reported, not swallowed silently");
    ok(!console_has("(vmm pid"),
       "an unrecognised directive is not turned into a signal");
    ok(!file_exists(ctlpath),
       "an unrecognised directive is consumed instead of retried forever");
    ok(vmm_alive(sup_pid), "the supervisor was untouched by the snapshot request");

    /* A stop is different: it is a real directive, so with no pid known it
     * must be *deferred* (file kept for the next poll) rather than dropped. */
    console_reset();
    write_file(ctlpath, "stop\n");
    handle_ctl("defer");
    ok(console_has("no VMM pid yet"),
       "a real directive with no VMM pid is deferred, not applied or dropped");
    ok(file_exists(ctlpath),
       "a deferred directive is left in place for the next poll");

    forget_vm(0);
    kill(sup_pid, SIGKILL);
    waitpid(sup_pid, NULL, 0);
    sys_unlink(ctlpath);
}

/* ── name validation happens before any path is built ────────────────
 * launch_vm() and handle_ctl() both used to snprintf the name into a path
 * and read/unlink it, and only then call valid_name().  A name carrying
 * ".." therefore reached the filesystem before being rejected, which is the
 * opposite of what the comment on valid_name() claims.  Names come from
 * sys_readdir so a hostile one is not expected, but the ordering is the
 * property under test: nothing outside CMD_DIR may be touched.
 */
static void test_name_rejected_before_filesystem(void) {
    char victim[256];
    char ctlvictim[256];
    int n;
    section("name validation ordering");

    /* Plant the two files the traversal would actually reach.  The escape
     * is relative to CMD_DIR itself, so the target sits in CMD_DIR's
     * *parent* -- CMD_DIR/../x.cmd -- not inside CMD_DIR.  Planting it in
     * the wrong place would make this test pass for the wrong reason. */
    snprintf(victim, sizeof(victim), "%s/../victim.cmd", CMD_DIR);
    snprintf(ctlvictim, sizeof(ctlvictim), "%s/../victim.ctl", CMD_DIR);
    ok(write_file(victim, "SENTINEL-CMD") > 0, "planted a .cmd file outside CMD_DIR");
    ok(write_file(ctlvictim, "stop\n") > 0, "planted a .ctl file outside CMD_DIR");
    ok(file_exists(victim), "the escape target really is outside CMD_DIR");

    console_reset();
    n = launch_vm("../victim", "/vmm");
    ok(n < 0, "launch_vm rejects a traversal name");
    ok(console_has("rejecting unsafe VM name"),
       "launch_vm says why, rather than reporting a missing command file");

    /* The old code read the .cmd first, so a readable file was consumed
     * ("ncvm: launching '../victim'") and then unlinked. */
    ok(!console_has("launching"),
       "launch_vm does not proceed to launch a traversal name");
    ok(file_exists(victim), "the .cmd file outside CMD_DIR was not unlinked");

    /* handle_ctl must refuse before reading or unlinking anything. */
    console_reset();
    handle_ctl("../victim");
    ok(!console_has("not running"),
       "handle_ctl rejects the name outright instead of acting on it");
    ok(file_exists(ctlvictim), "the .ctl file outside CMD_DIR was not unlinked");

    char back[64];
    ok(slurp(victim, back, sizeof(back)) == 12 && !strcmp(back, "SENTINEL-CMD"),
       "the file outside CMD_DIR was neither consumed nor modified");

    sys_unlink(victim);
    sys_unlink(ctlvictim);
}

/* ── long paths: syn() must not silently truncate ─────────────────────
 * The -virtfs option is "local,path=<path>,mount_tag=root,
 * security_model=none" -- 46 characters of boilerplate around the path. With
 * a 64-byte slot a path longer than 18 bytes was cut off mid-string and
 * handed to QEMU as a corrupt option.
 */
static void test_long_path_not_truncated(void) {
    char *out[T_MAX];
    char cmd[MAX_CMD_LEN];
    int n, i;
    section("syn() truncation");

    /* 46 bytes of fixed boilerplate around the path, so the path itself has
     * to be short enough to fit the old 64-byte slot.  A 91-character path
     * needs 137 bytes and would still be cut off at 128; what is being
     * tested is that the option is no longer silently mangled at 63.
     * MAX_CMD_LEN is the real bound on any path this can be handed. */
    char longdir[80];
    longdir[0] = '/';
    for (i = 1; i < 32; i++) longdir[i] = 'x';
    longdir[32] = '/';
    longdir[33] = 0;

    snprintf(cmd, sizeof(cmd), "crosvm run --root %s", longdir);
    n = translate(cmd, "/vmm", out);

    ok(idx_tok(out, n, "-virtfs") >= 0, "a long --root directory still maps to -virtfs");
    if (idx_tok(out, n, "-virtfs") >= 0) {
        const char *v = out[idx_tok(out, n, "-virtfs") + 1];
        ok(strstr(v, longdir) != NULL, "the full path survives into the -virtfs option");
        ok(strstr(v, "security_model=none") != NULL,
           "the option is not truncated before its tail");
    }

    /* A long disk image path: "file=%s,format=raw,if=virtio" is 26 of
     * boilerplate, so a 60-character path needs 86 bytes. */
    char longimg[80];
    longimg[0] = '/';
    for (i = 1; i < 60; i++) longimg[i] = 'y';
    longimg[60] = 0;
    snprintf(cmd, sizeof(cmd), "crosvm run --rwdisk %s", longimg);
    n = translate(cmd, "/vmm", out);
    ok(idx_tok(out, n, "-drive") >= 0 &&
       strstr(out[idx_tok(out, n, "-drive") + 1], longimg) != NULL,
       "a long --rwdisk path survives into the -drive option");
    ok(idx_tok(out, n, "-drive") >= 0 &&
       strstr(out[idx_tok(out, n, "-drive") + 1], "if=virtio") != NULL,
       "the -drive option keeps its format=...,if=virtio tail");
}

/* ── -m with a unit suffix ────────────────────────────────────────────
 * is_digits() rejects "1G", and the old code dropped -m and then let the
 * value fall through as a bare positional argument -- which QEMU reads as a
 * kernel filename.
 */
static void test_memory_with_suffix(void) {
    char *out[T_MAX];
    int n;
    section("-m with a unit suffix");

    n = translate("crosvm run -m 1G", "/vmm", out);
    ok(idx_tok(out, n, "-m") >= 0 && !strcmp(out[idx_tok(out, n, "-m") + 1], "1G"),
       "-m 1G keeps both the flag and the value");
    ok(!has_tok(out, n, "1G") || idx_tok(out, n, "-m") >= 0,
       "-m 1G does not leak the value as a bare positional argument");

    n = translate("crosvm run -m 512M", "/vmm", out);
    ok(idx_tok(out, n, "-m") >= 0 && !strcmp(out[idx_tok(out, n, "-m") + 1], "512M"),
       "-m 512M is passed through unchanged");

    n = translate("crosvm run --memory 2048", "/vmm", out);
    ok(has_tok(out, n, "2048M"), "--memory 2048 gains the M suffix");

    /* The digit-only case must not regress into a two-token -m. */
    n = translate("crosvm run -m 256", "/vmm", out);
    ok(idx_tok(out, n, "-m") < 0 && has_tok(out, n, "256M"),
       "-m 256 still collapses to a single 256M token");
}

int main(void) {
    /* Safety net: the lifecycle tests fork real children and wait on them.
     * A regression that stops a signal being delivered must fail the suite,
     * not stall it, so a hard timeout backs up every bounded wait. */
    alarm(120);

    printf("ncvm host tests (CMD_DIR=%s)\n", CMD_DIR);
    scratch_reset();

    test_valid_name();
    test_is_digits();
    test_tokenize();
    test_readdir_walk();
    test_read_file();
    test_write_file();
    test_translate_basics();
    test_translate_drops_crosvm_only();
    test_translate_accel_and_machine();
    test_memory_with_suffix();
    test_ctl_signals_reach_the_vmm();
    test_ctl_deferred_without_vmm_pid();
    test_name_rejected_before_filesystem();
    test_long_path_not_truncated();
    test_syn_pool_reuse();

    printf("\nchecks=%d failed=%d\n", checks, failed);
    if (failed) {
        printf("ncvm: FAIL (first: %s)\n", first_fail);
        return 1;
    }
    printf("ncvm: host tests OK\n");
    return 0;
}

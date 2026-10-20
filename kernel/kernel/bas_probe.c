/* bas_probe.c — boot-time smoke test for bas-built programs.
 *
 * Runs /bin/bas-selftest as a real userspace process, the same way ncvm_probe
 * runs /bin/ncvm --selftest.  It exists because a bas cross-build can be green
 * and still not run: the toolchain links an ELF, the disk image ships it, and
 * nothing has yet shown that the kernel loads it, that the freestanding libc
 * works on this kernel, or that the SDK's syscall wrappers pass their arguments
 * in the right registers.  All three fail silently in a build.
 *
 * The shell can run the same binary (`bas selftest`), but only interactively.
 * The probe runs it unconditionally on every boot, so the assertions execute
 * without a human at the console -- that is what bas_boot_check.py reads.
 *
 * When there is no bas checkout, kernel/Makefile compiles this file out (see
 * BAS_PROGS there), so nothing here has to test for a missing binary.
 */
#include "sched.h"
#include "kprintf.h"
#include "elf.h"
#include "process.h"
#include "umode.h"
#include "syscall.h"
#include "string.h"

extern uint64_t syscall_kernel_rsp;

static void bas_probe_done(void);
static volatile int bas_probe_done_flag;

static void bas_probe_thread(void) {
    uint64_t entry, stack;
    elf_auxv_info_t auxv;

    kprintf("BAS: load /bin/bas-selftest\n");
    if (elf_load("/bin/bas-selftest", &entry, &stack, &auxv) < 0) {
        /* Not a failure: on a tree with no bas checkout there is no program to
         * run, and that is the documented behaviour, not a broken image.  It is
         * still worth a line, because "the probe printed nothing at all" and
         * "the probe ran and there was no bas program" are very different
         * diagnoses and only one of them is a bug. */
        kprintf("BAS: no bas programs in this image (built without a bas "
                "checkout)\n");
        bas_probe_done_flag = 1;
        sched_exit(0);
        return;
    }

    static char *probe_argv[2] = { "bas-selftest", 0 };
    uint64_t rsp = elf_setup_stack(stack, entry, 1, probe_argv, 0, 0, &auxv);
    kprintf("BAS: start entry=0x%lx rsp=0x%lx\n", entry, rsp);

    current_process = 0;
    proc_create("/bin/bas-selftest", entry, stack, LEVEL_USER);

    user_mode_set_return(bas_probe_done);
    user_mode_begin();
    thread_t *cur = sched_current();
    if (cur && cur->syscall_stack_top)
        syscall_kernel_rsp = (uint64_t)cur->syscall_stack_top;
    user_mode_enter(entry, rsp);
}

static void bas_probe_done(void) {
    int status = user_mode_last_exit_status();
    /* bas-selftest prints each of its own assertions, and prints "done" only if
     * none of them tripped -- so reaching the exit is the pass.  A non-zero
     * status means it died before that (a fault in the freestanding libc, or
     * in an SDK wrapper) and is named rather than reported as an ordinary
     * exit. */
    kprintf(status == 0 ? "BAS: done status=0\n"
                        : "BAS: FAIL bas-selftest exited %d\n", status);
    current_process = 0;
    proc_reap();
    bas_probe_done_flag = 1;
    sched_exit(0);
}

int bas_probe_finished(void) { return bas_probe_done_flag; }

int bas_probe_run(void) {
    bas_probe_done_flag = 0;
    int tid = sched_create_thread("bas-selftest", bas_probe_thread);
    kprintf("BAS: spawned tid=%d\n", tid);
    return tid;
}
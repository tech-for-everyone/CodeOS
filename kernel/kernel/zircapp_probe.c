/* zircapp_probe.c — boot-time smoke test for the ZircApp host.
 *
 * Loads /bin/zircapp as a real CodeOS userspace process and runs it with
 * `--selftest`.  In that mode the host skips the window bridge and runs a
 * bounded console session: it registers the whole Zircon application
 * framework, launches each of the nine phone apps, broadcasts a touch/key
 * event to every registered app, and exits 0 only if all nine registered.
 *
 * This proves the Phase-2 port end to end without a compositor: the
 * framework, the ten Zircon app objects and the WM wire format all execute
 * inside the guest.  It prints `ZIRCAPP: done status=0` on success, or
 * `ZIRCAPP: FAIL selftest exited N` on assertion failure, matching the
 * ncvm_probe convention.
 */
#include "sched.h"
#include "kprintf.h"
#include "elf.h"
#include "process.h"
#include "umode.h"
#include "syscall.h"
#include "string.h"

extern uint64_t syscall_kernel_rsp;

static void zircapp_probe_done(void);

static void zircapp_probe_thread(void) {
    uint64_t entry, stack;
    elf_auxv_info_t auxv;

    kprintf("ZIRCAPP: load /bin/zircapp\n");
    if (elf_load("/bin/zircapp", &entry, &stack, &auxv) < 0) {
        kprintf("ZIRCAPP: FAIL /bin/zircapp not embedded\n");
        sched_exit(1);
        return;
    }

    static char *probe_argv[3] = { "zircapp", "--selftest", 0 };
    uint64_t rsp = elf_setup_stack(stack, entry, 2, probe_argv, 0, 0, &auxv);
    kprintf("ZIRCAPP: start entry=0x%lx rsp=0x%lx\n", entry, rsp);

    current_process = 0;
    proc_create("/bin/zircapp", entry, stack, LEVEL_USER);

    user_mode_set_return(zircapp_probe_done);
    user_mode_begin();
    thread_t *cur = sched_current();
    if (cur && cur->syscall_stack_top)
        syscall_kernel_rsp = (uint64_t)cur->syscall_stack_top;
    user_mode_enter(entry, rsp);
}

static volatile int zircapp_probe_done_flag;

static void zircapp_probe_done(void) {
    int status = user_mode_last_exit_status();
    kprintf(status == 0 ? "ZIRCAPP: done status=0\n"
                        : "ZIRCAPP: FAIL selftest exited %d\n", status);
    current_process = 0;
    proc_reap();
    zircapp_probe_done_flag = 1;
    sched_exit(0);
}

int zircapp_probe_finished(void) { return zircapp_probe_done_flag; }

int zircapp_probe_run(void) {
    zircapp_probe_done_flag = 0;
    int tid = sched_create_thread("zircapp-selftest", zircapp_probe_thread);
    kprintf("ZIRCAPP: spawned tid=%d\n", tid);
    return tid;
}

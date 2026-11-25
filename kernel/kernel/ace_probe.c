/* ace_probe.c — boot-time smoke test for the ArkUI/ACE layout engine.
 *
 * ace-host/CMakeLists.txt builds the real OpenHarmony layout code into a
 * raw-syscall static ELF, kernel/Makefile embeds it as /bin/ace-layout, and
 * this probe runs it as a real userspace process.  It is the runtime proof
 * that the engine is not just a vendored directory or a host-only test:
 * the ELF has to be in the image, the loader has to start it, and the engine's
 * own assertions have to print "ACEOS PASS: 0 failure(s)".
 *
 * The shell's `ace layout` command runs the same binary interactively.
 * ace_boot_check.py drives both paths and rejects a boot where either one is
 * missing.  On a build without cmake/arkui the absence is reported, not hidden.
 */
#include "sched.h"
#include "kprintf.h"
#include "elf.h"
#include "process.h"
#include "umode.h"
#include "syscall.h"
#include "string.h"

extern uint64_t syscall_kernel_rsp;

static void ace_probe_done(void);
static volatile int ace_probe_done_flag;

static void ace_probe_thread(void) {
    uint64_t entry, stack;
    elf_auxv_info_t auxv;

    kprintf("ACE: load /bin/ace-layout\n");
    if (elf_load("/bin/ace-layout", &entry, &stack, &auxv) < 0) {
        kprintf("ACE: no ArkUI layout engine in this image (built without "
                "cmake/arkui or on a cross host)\n");
        ace_probe_done_flag = 1;
        sched_exit(0);
        return;
    }

    static char *probe_argv[2] = { "ace-layout", 0 };
    uint64_t rsp = elf_setup_stack(stack, entry, 1, probe_argv, 0, 0, &auxv);
    kprintf("ACE: start entry=0x%lx rsp=0x%lx\n", entry, rsp);

    current_process = 0;
    proc_create("/bin/ace-layout", entry, stack, LEVEL_USER);

    user_mode_set_return(ace_probe_done);
    user_mode_begin();
    thread_t *cur = sched_current();
    if (cur && cur->syscall_stack_top)
        syscall_kernel_rsp = (uint64_t)cur->syscall_stack_top;
    user_mode_enter(entry, rsp);
}

static void ace_probe_done(void) {
    int status = user_mode_last_exit_status();
    kprintf(status == 0 ? "ACE: done status=0\n"
                        : "ACE: FAIL ace-layout exited %d\n", status);
    current_process = 0;
    proc_reap();
    ace_probe_done_flag = 1;
    sched_exit(0);
}

int ace_probe_finished(void) { return ace_probe_done_flag; }

int ace_probe_run(void) {
    ace_probe_done_flag = 0;
    int tid = sched_create_thread("ace-layout", ace_probe_thread);
    kprintf("ACE: spawned tid=%d\n", tid);
    return tid;
}

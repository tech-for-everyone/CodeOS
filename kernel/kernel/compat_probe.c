#include "sched.h"
#include "kprintf.h"
#include "elf.h"
#include "process.h"
#include "umode.h"
#include "syscall.h"
#include "string.h"

extern uint64_t syscall_kernel_rsp;

static void compat_probe_done(void);

static void compat_probe_thread(void) {
    uint64_t entry, stack;
    elf_auxv_info_t auxv;

    kprintf("LPROBE: load /bin/linux-probe\n");
    if (elf_load("/bin/linux-probe", &entry, &stack, &auxv) < 0) {
        kprintf("LPROBE: failed to load /bin/linux-probe\n");
        sched_exit(1);
        return;
    }

    uint64_t rsp = elf_setup_stack(stack, entry, 0, 0, 0, 0, &auxv);
    kprintf("LPROBE: start entry=0x%lx rsp=0x%lx\n", entry, rsp);

    {
        uint64_t phys, flags;
        if (vmm_get_mapping(entry, &phys, &flags) == 0) {
            unsigned char *p = (unsigned char*)phys_to_virt(phys) + (entry & 0xFFF);
            kprintf("LPROBE: entry page phys=0x%lx flags=0x%lx\n", phys, flags);
            kprintf("LPROBE: bytes: %02x %02x %02x %02x %02x %02x %02x %02x\n",
                    p[0], p[1], p[2], p[3], p[4], p[5], p[6], p[7]);
        } else {
            kprintf("LPROBE: entry unmapped\n");
        }
    }

    current_process = 0;
    proc_create("/bin/linux-probe", entry, stack);
    if (current_process)
        current_process->personality = PERSONALITY_LINUX;

    user_mode_set_return(compat_probe_done);
    user_mode_begin();
    thread_t *cur = sched_current();
    if (cur && cur->syscall_stack_top)
        syscall_kernel_rsp = (uint64_t)cur->syscall_stack_top;
    user_mode_enter(entry, rsp);
}

static volatile int compat_probe_done_flag;

static void compat_probe_done(void) {
    kprintf("LPROBE: done status=%d\n", user_mode_last_exit_status());
    current_process = 0;
    proc_reap();
    compat_probe_done_flag = 1;
    sched_exit(0);
}

int compat_probe_finished(void) { return compat_probe_done_flag; }

int compat_probe_run(void) {
    compat_probe_done_flag = 0;
    int tid = sched_create_thread("lp-probe", compat_probe_thread);
    kprintf("LPROBE: spawned tid=%d\n", tid);
    return tid;
}
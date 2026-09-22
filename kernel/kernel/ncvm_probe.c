/* ncvm_probe.c — boot-time smoke test for the in-guest ncvm VM backend.
 *
 * Loads /bin/ncvm as a real userspace process (the same machinery the
 * desktop and the linux-probe use to launch user programs) and runs it
 * with `--selftest`. Proves the backend binary is present in the
 * initramfs and actually executes in user mode at every boot.
 *
 * ncvm is a CodeOS-ABI ELF, so it keeps the default personality (unlike
 * /bin/linux-probe which is a Linux-ABI binary).
 */
#include "sched.h"
#include "kprintf.h"
#include "elf.h"
#include "process.h"
#include "umode.h"
#include "syscall.h"
#include "string.h"
#include "../arch/x86_64/io.h"

extern uint64_t syscall_kernel_rsp;

/* ncvm portal device ports (Rust memory handlers), see include/hw/ncvm/ncvm.h.
 * 0x740..0x747 is free I/O space on q35 (0x630 is acpi-smi there). */
#define NCVM_IOBASE 0x740u

static void ncvm_probe_done(void);

/* Kernel-mode self-check of the host emulator's ncvm portal device.
 *
 * Read-only identity/status window (I/O ports 0x740..0x746) plus the
 * guest-command byte register at 0x747 (write sets, read echoes).
 * Under an emulator without the ncvm device the ports return 0xff.
 */
static void ncvm_port_selfcheck(void) {
    uint8_t magic[4], feat, major, minor, echo;

    magic[0] = inb(NCVM_IOBASE + 0x00);
    magic[1] = inb(NCVM_IOBASE + 0x01);
    magic[2] = inb(NCVM_IOBASE + 0x02);
    magic[3] = inb(NCVM_IOBASE + 0x03);
    feat  = inb(NCVM_IOBASE + 0x04);
    major = inb(NCVM_IOBASE + 0x05);
    minor = inb(NCVM_IOBASE + 0x06);

    if (magic[0] == 'N' && magic[1] == 'C' && magic[2] == 'V' && magic[3] == 'M') {
        kprintf("NCVM: portal io=0x740 magic OK feat=0x%02x svc=%u.%u\n",
                feat, major, minor);
        outb(NCVM_IOBASE + 0x07, 0x42);
        echo = inb(NCVM_IOBASE + 0x07);
        kprintf("NCVM: cmd write/echo=0x%02x %s\n", echo,
                echo == 0x42 ? "OK" : "MISMATCH");
    } else {
        kprintf("NCVM: portal io=0x740 ABSENT (magic=%02x%02x%02x%02x) "
                "legacy/foreign emulator?\n",
                magic[0], magic[1], magic[2], magic[3]);
    }
}

static void ncvm_probe_thread(void) {
    uint64_t entry, stack;
    elf_auxv_info_t auxv;

    /* 1) kernel-mode portal self-check (host emulator identity) */
    ncvm_port_selfcheck();

    kprintf("NCVM: load /bin/ncvm\n");
    if (elf_load("/bin/ncvm", &entry, &stack, &auxv) < 0) {
        kprintf("NCVM: failed to load /bin/ncvm (not embedded?)\n");
        sched_exit(1);
        return;
    }

    /* argv = { "ncvm", "--selftest" } */
    static char *probe_argv[3] = { "ncvm", "--selftest", 0 };
    uint64_t rsp = elf_setup_stack(stack, entry, 2, probe_argv, 0, 0, &auxv);
    kprintf("NCVM: start entry=0x%lx rsp=0x%lx\n", entry, rsp);

    current_process = 0;
    proc_create("/bin/ncvm", entry, stack);

    user_mode_set_return(ncvm_probe_done);
    user_mode_begin();
    thread_t *cur = sched_current();
    if (cur && cur->syscall_stack_top)
        syscall_kernel_rsp = (uint64_t)cur->syscall_stack_top;
    user_mode_enter(entry, rsp);
}

static volatile int ncvm_probe_done_flag;

static void ncvm_probe_done(void) {
    kprintf("NCVM: done status=%d\n", user_mode_last_exit_status());
    current_process = 0;
    proc_reap();
    ncvm_probe_done_flag = 1;
    sched_exit(0);
}

int ncvm_probe_finished(void) { return ncvm_probe_done_flag; }

int ncvm_probe_run(void) {
    ncvm_probe_done_flag = 0;
    int tid = sched_create_thread("ncvm-selftest", ncvm_probe_thread);
    kprintf("NCVM: spawned tid=%d\n", tid);
    return tid;
}
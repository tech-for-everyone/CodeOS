#include "types.h"
#include "../kernel/kprintf.h"

/* Exception frame saved by vectors.S (sync_el1_spx):
 * pushes, in order: x0..x29 pairs, then elr_el1/spsr_el1, then
 * esr_el1/far_el1. The last push is at the lowest address, so sp
 * points at esr_el1. */
struct trap_frame {
    uint64_t esr_el1;
    uint64_t far_el1;
    uint64_t elr_el1;
    uint64_t spsr_el1;
    uint64_t x28, x29;
    uint64_t x26, x27;
    uint64_t x24, x25;
    uint64_t x22, x23;
    uint64_t x20, x21;
    uint64_t x18, x19;
    uint64_t x16, x17;
    uint64_t x14, x15;
    uint64_t x12, x13;
    uint64_t x10, x11;
    uint64_t x8, x9;
    uint64_t x6, x7;
    uint64_t x4, x5;
    uint64_t x2, x3;
    uint64_t x0, x1;
};

void exception_handler(struct trap_frame *frame) {
    uint64_t esr = frame->esr_el1;
    uint64_t far = frame->far_el1;
    uint64_t elr = frame->elr_el1;
    uint64_t ec = (esr >> 26) & 0x3F;

    const char *exc_name = "unknown";
    switch (ec) {
        case 0x00: exc_name = "SError (external abort)"; break;
        case 0x01: exc_name = "WFI/WFE"; break;
        case 0x02: exc_name = "debug"; break;
        case 0x03: exc_name = "breakpoint (self)"; break;
        case 0x04: exc_name = "syscall"; break;
        case 0x05: exc_name = "prefetch abort (svc)"; break;
        case 0x06: exc_name = "data abort (svc)"; break;
        case 0x0D: exc_name = "syscall (el0)"; break;
        case 0x16: exc_name = "prefetch abort (el0)"; break;
        case 0x17: exc_name = "data abort (el0)"; break;
        default: break;
    }

    kprintf("ARM64 Exception: %s (EC=0x%lx)\n", exc_name, ec);
    kprintf("  ESR=0x%lx FAR=0x%lx ELR=0x%lx\n", esr, far, elr);

    /* Spin so we can inspect registers */
    for (;;) {
        __asm__ volatile("wfi" ::: "memory");
    }
}

void irq_handler(struct trap_frame *frame) {
    (void)frame;
    /* Acknowledge IRQ — for now just print and return */
    /* On real hardware, read the GIC_ISPENDR / GIC_EOIR registers */
    /* For QEMU virt platform, PL011 UART and timer interrupts come through GIC */
    /* TODO: read GIC registers to determine source and clear interrupt */
}

/* syscall_handler is called from vectors.S (sync_el0_a64) and syscall_entry.S
 * x0 = syscall number
 * x1 = pointer to saved context (trap_frame on stack)
 * Returns value in x0
 */
uint64_t syscall_handler(uint64_t num, struct trap_frame *frame) {
    (void)frame;
    /* Minimal syscall implementation — all return -ENOSYS */
    switch (num) {
        case 0:  /* SYS_read */
        case 1:  /* SYS_write */
        case 2:  /* SYS_open */
        case 3:  /* SYS_close */
        case 60: /* SYS_exit */
            return (uint64_t)-38; /* ENOSYS */
        default:
            return (uint64_t)-38;
    }
}

/* Called from kernel_init / main to set up interrupt controller */
void irq_init(void) {
    /* GIC initialization would go here for real hardware */
    kprintf("ARM64: IRQ controller (stub)\n");
}

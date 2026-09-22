.section .text
.balign 2048
.global exception_vector

/* ARM64 Exception Vector Table (VBAR_EL1)
 * 16 entries, each 128 bytes (32 instructions)
 * Order: Sync, IRQ, FIQ, SError
 * Each group: Current EL with SP0, Current EL with SPx, Lower EL (AArch64), Lower EL (AArch32)
 */

.macro ventry label
    .align 7
    b \label
.endm

exception_vector:
    /* Current EL with SP0 */
    ventry sync_el1_sp0
    ventry irq_el1_sp0
    ventry fiq_el1_sp0
    ventry serr_el1_sp0

    /* Current EL with SPx */
    ventry sync_el1_spx
    ventry irq_el1_spx
    ventry fiq_el1_spx
    ventry serr_el1_spx

    /* Lower EL (AArch64) */
    ventry sync_el0_a64
    ventry irq_el0_a64
    ventry fiq_el0_a64
    ventry serr_el0_a64

    /* Lower EL (AArch32) */
    ventry sync_el0_a32
    ventry irq_el0_a32
    ventry fiq_el0_a32
    ventry serr_el0_a32

sync_el1_sp0:
    b sync_el1_spx

irq_el1_sp0:
    b irq_el1_spx

fiq_el1_sp0:
    b fiq_el1_spx

serr_el1_sp0:
    b serr_el1_spx

sync_el1_spx:
    /* Save registers */
    stp x0, x1, [sp, #-16]!
    stp x2, x3, [sp, #-16]!
    stp x4, x5, [sp, #-16]!
    stp x6, x7, [sp, #-16]!
    stp x8, x9, [sp, #-16]!
    stp x10, x11, [sp, #-16]!
    stp x12, x13, [sp, #-16]!
    stp x14, x15, [sp, #-16]!
    stp x16, x17, [sp, #-16]!
    stp x18, x19, [sp, #-16]!
    stp x20, x21, [sp, #-16]!
    stp x22, x23, [sp, #-16]!
    stp x24, x25, [sp, #-16]!
    stp x26, x27, [sp, #-16]!
    stp x28, x29, [sp, #-16]!
    mrs x0, elr_el1
    mrs x1, spsr_el1
    stp x0, x1, [sp, #-16]!
    mrs x0, esr_el1
    mrs x1, far_el1
    stp x0, x1, [sp, #-16]!

    /* Call exception handler */
    mov x0, sp
    bl exception_handler

    /* Restore */
    ldp x0, x1, [sp], #16
    msr far_el1, x1
    msr esr_el1, x0
    ldp x0, x1, [sp], #16
    msr spsr_el1, x1
    msr elr_el1, x0
    ldp x28, x29, [sp], #16
    ldp x26, x27, [sp], #16
    ldp x24, x25, [sp], #16
    ldp x22, x23, [sp], #16
    ldp x20, x21, [sp], #16
    ldp x18, x19, [sp], #16
    ldp x16, x17, [sp], #16
    ldp x14, x15, [sp], #16
    ldp x12, x13, [sp], #16
    ldp x10, x11, [sp], #16
    ldp x8, x9, [sp], #16
    ldp x6, x7, [sp], #16
    ldp x4, x5, [sp], #16
    ldp x2, x3, [sp], #16
    ldp x0, x1, [sp], #16
    eret

irq_el1_spx:
    /* Simple IRQ handler */
    stp x0, x1, [sp, #-16]!
    stp x2, x3, [sp, #-16]!
    stp x4, x5, [sp, #-16]!
    stp x6, x7, [sp, #-16]!
    stp x8, x9, [sp, #-16]!
    stp x10, x11, [sp, #-16]!

    bl irq_handler

    ldp x10, x11, [sp], #16
    ldp x8, x9, [sp], #16
    ldp x6, x7, [sp], #16
    ldp x4, x5, [sp], #16
    ldp x2, x3, [sp], #16
    ldp x0, x1, [sp], #16
    eret

fiq_el1_spx:
    eret

serr_el1_spx:
    eret

/* Syscalls from user mode (EL0) */
sync_el0_a64:
    /* Save all registers */
    sub sp, sp, #(32 * 16)  /* make room for 32 regs + elr + spsr */
    stp x0, x1, [sp, #(0 * 16)]
    stp x2, x3, [sp, #(1 * 16)]
    stp x4, x5, [sp, #(2 * 16)]
    stp x6, x7, [sp, #(3 * 16)]
    stp x8, x9, [sp, #(4 * 16)]
    stp x10, x11, [sp, #(5 * 16)]
    stp x12, x13, [sp, #(6 * 16)]
    stp x14, x15, [sp, #(7 * 16)]
    stp x16, x17, [sp, #(8 * 16)]
    stp x18, x19, [sp, #(9 * 16)]
    stp x20, x21, [sp, #(10 * 16)]
    stp x22, x23, [sp, #(11 * 16)]
    stp x24, x25, [sp, #(12 * 16)]
    stp x26, x27, [sp, #(13 * 16)]
    stp x28, x29, [sp, #(14 * 16)]
    mrs x0, elr_el1
    mrs x1, spsr_el1
    stp x0, x1, [sp, #(15 * 16)]

    /* x8 = syscall number, x0-x6 = arguments */
    mov x0, x8             /* syscall number */
    mov x1, sp             /* save context pointer */
    bl syscall_handler

    /* Restore elr and spsr */
    ldp x0, x1, [sp, #(15 * 16)]
    msr spsr_el1, x1
    msr elr_el1, x0

    /* Restore regs except x0 (return value) */
    ldp x2, x3, [sp, #(1 * 16)]
    ldp x4, x5, [sp, #(2 * 16)]
    ldp x6, x7, [sp, #(3 * 16)]
    ldp x8, x9, [sp, #(4 * 16)]
    ldp x10, x11, [sp, #(5 * 16)]
    ldp x12, x13, [sp, #(6 * 16)]
    ldp x14, x15, [sp, #(7 * 16)]
    ldp x16, x17, [sp, #(8 * 16)]
    ldp x18, x19, [sp, #(9 * 16)]
    ldp x20, x21, [sp, #(10 * 16)]
    ldp x22, x23, [sp, #(11 * 16)]
    ldp x24, x25, [sp, #(12 * 16)]
    ldp x26, x27, [sp, #(13 * 16)]
    ldp x28, x29, [sp, #(14 * 16)]
    ldr x1, [sp, #(1 * 16)]     /* x1 was saved at offset 1 */
    /* Actually let's keep it simple - just restore x0 from the return value */
    /* x0 already has the return value from syscall_handler */
    add sp, sp, #(32 * 16)
    eret

irq_el0_a64:
fiq_el0_a64:
serr_el0_a64:
sync_el0_a32:
irq_el0_a32:
fiq_el0_a32:
serr_el0_a32:
    eret

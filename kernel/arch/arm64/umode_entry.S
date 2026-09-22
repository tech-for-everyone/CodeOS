.section .text
.global arm64_enter_usermode

/* void arm64_enter_usermode(uint64_t entry, uint64_t stack, uint64_t arg);
 * Switch to EL0 and execute user code at 'entry' with stack 'stack' and argument 'arg'.
 *
 * x0 = entry point (PC for EL0)
 * x1 = user stack pointer (SP_EL0)
 * x2 = argument (x0 in user mode)
 */
arm64_enter_usermode:
    /* Save return address */
    mov x3, x30

    /* Set user stack */
    msr sp_el0, x1
    isb

    /* Set up SPSR_EL1 for EL0 (AArch64)
     * M[3:0] = 0 (EL0t)
     * F, I, A = 1 (mask all exceptions)
     */
    mov x4, #(0b0101 << 6)    /* DAIF masked, EL0t */
    msr spsr_el1, x4

    /* Set ELR_EL1 to entry point */
    msr elr_el1, x0

    /* Set argument in x0 for user mode */
    mov x0, x2

    /* Return to EL0 */
    eret

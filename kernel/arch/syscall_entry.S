/* syscall entry for `syscall` instruction (Linux x86_64 ABI).
 *
 * Mirrors the interrupt path (isr_common) so the full user GP state is
 * captured in the exact same trapframe layout and restored verbatim; the
 * Linux ABI promise is that only RAX/RCX/R11 change across a syscall.
 *
 * Trapframe (rsp = index 0 after all 15 pushes):
 *   0: rax, 8: rbx, 16: rcx, 24: rdx, 32: rsi, 40: rdi, 48: rbp,
 *   56: r8, 64: r9, 72: r10, 80: r11, 88: r12, 96: r13, 104: r14,
 *   112: r15
 *
 * For the `syscall` instruction RCX holds the return RIP and R11 the
 * RFLAGS, so those are used to build the outgoing iretq frame. */
.section .text
.global syscall_entry
.extern linux_syscall_handler
.extern syscall_kernel_rsp
.extern user_mode_exit_flag
.extern user_mode_force_return

syscall_entry:
    movq %rsp, saved_user_rsp(%rip)
    movq syscall_kernel_rsp(%rip), %rsp
    pushq %r15
    pushq %r14
    pushq %r13
    pushq %r12
    pushq %r11
    pushq %r10
    pushq %r9
    pushq %r8
    pushq %rbp
    pushq %rdi
    pushq %rsi
    pushq %rdx
    pushq %rcx
    pushq %rbx
    pushq %rax

    /* translate to the C handler calling convention (rsi/rdx/rcx/r8/r9
     * become C args 1..5, so only RAX is free for scratch) */
    movq %rax, %rdi            /* n = syscall number */
    movq 0x28(%rsp), %rsi      /* a1 = user RDI */
    movq 0x20(%rsp), %rdx      /* a2 = user RSI */
    movq 0x18(%rsp), %rcx      /* a3 = user RDX */
    movq 0x48(%rsp), %r8       /* a4 = user R10 */
    movq 0x38(%rsp), %r9       /* a5 = user R8  */
    movq 0x40(%rsp), %r10      /* a6 = user R9  */
    movq 0x10(%rsp), %r11      /* uip = user RCX (syscall RIP) */
    movq 0x58(%rsp), %rax      /* ufl = user R11 (syscall RFLAGS) */
    pushq %rax                 /* ufl (arg9) */
    pushq %r11                 /* uip (arg8) */
    pushq %r10                 /* a6 (arg7) */
    sti
    callq linux_syscall_handler
    addq $24, %rsp             /* drop the stack args */

    cmpb $0, user_mode_exit_flag(%rip)
    je 1f
    movb $0, user_mode_exit_flag(%rip)
    jmp user_mode_force_return

1:
    /* store the handler result into the trapframe RAX slot */
    movq %rax, (%rsp)
    popq %rax
    popq %rbx
    popq %rcx
    popq %rdx
    popq %rsi
    popq %rdi
    popq %rbp
    popq %r8
    popq %r9
    popq %r10
    popq %r11
    popq %r12
    popq %r13
    popq %r14
    popq %r15
    /* build ring3 iretq frame (SS first, RIP last per iretq pop order) */
    pushq $0x1b                /* SS = USER_DS|3  (highest address) */
    pushq saved_user_rsp(%rip) /* user RSP */
    pushq %r11                 /* RFLAGS */
    pushq $0x23                /* CS = USER_CS|3 */
    pushq %rcx                 /* RIP (lowest address = rsp) */
    iretq

.section .bss
.align 8
.global saved_user_rsp
saved_user_rsp:
    .skip 8
.global syscall_kernel_rsp
syscall_kernel_rsp:
    .skip 8

.section .note.GNU-stack,"",@progbits
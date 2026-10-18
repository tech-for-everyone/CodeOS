/* Enter ring 3 user mode via iretq */
.section .text
.global user_mode_enter
.global user_mode_force_return
.global user_mode_enter_ctx
.extern umode_rsp0_alloc
.extern umode_note_entry
.extern user_mode_return_override
.extern user_mode_exit_flag

/* void user_mode_enter(uint64_t entry, uint64_t stack_top) */

user_mode_enter:
    movq %rsp, user_mode_kernel_rsp(%rip)
    /* save callee-saved GP regs so the C resume path after user_mode_enter
     * (e.g. tail of cmd_exec / run_builtin) sees intact rbp/rbx/r12-r15 */
    movq %rbx, umode_saved_rbx(%rip)
    movq %rbp, umode_saved_rbp(%rip)
    movq %r12, umode_saved_r12(%rip)
    movq %r13, umode_saved_r13(%rip)
    movq %r14, umode_saved_r14(%rip)
    movq %r15, umode_saved_r15(%rip)
    xorq %rax, %rax
    movq user_mode_return_override(%rip), %rcx
    testq %rcx, %rcx
    jnz 1f
    movq (%rsp), %rax          /* default: C caller return address */
    jmp 2f
1:
    movq %rcx, %rax
    movq $0, user_mode_return_override(%rip)
2:
    movq %rax, user_mode_return_rip(%rip)
    movq %rdi, %r14            /* entry */
    movq %rsi, %r15            /* stack_top */

    /* This thread's TSS.RSP0 stack -- allocated once per thread and reused
     * across entries (the old code allocated 16 fresh pages here on every
     * entry and leaked the previous ones), and pointed at by the scheduler on
     * every context switch so an interrupt from ring 3 always lands on the
     * running thread's own stack. */
    pushq %rdi
    pushq %rsi
    callq umode_rsp0_alloc
    popq %rsi
    popq %rdi
    testq %rax, %rax
    jnz 1f
    lea msg_fail(%rip), %rdi
    callq kprintf
    cli
    hlt
1:
    /* Attribute this thread to the process it is about to run, so a context
     * switch restores current_process together with the thread. */
    pushq %rdi
    pushq %rsi
    callq umode_note_entry
    popq %rsi
    popq %rdi

    /* Build iretq frame on current kernel stack */
    cli
    pushq $0x1b                /* SS = USER_DS|3 */
    pushq %r15                 /* RSP = user_stack_top */
    pushq $0x202               /* RFLAGS = IF set */
    pushq $0x23                /* CS = USER_CS|3 */
    pushq %r14                 /* RIP = entry */
    iretq

/* Return to shell after user exit syscall — restore kernel stack, jump to cmd_exec */
user_mode_force_return:
    movb $0, user_mode_exit_flag(%rip)
    movq umode_saved_rbx(%rip), %rbx
    movq umode_saved_rbp(%rip), %rbp
    movq umode_saved_r12(%rip), %r12
    movq umode_saved_r13(%rip), %r13
    movq umode_saved_r14(%rip), %r14
    movq umode_saved_r15(%rip), %r15
    movq user_mode_kernel_rsp(%rip), %rsp
    jmp *user_mode_return_rip(%rip)

/* void user_mode_enter_ctx(const proc_ctx_t *ctx)
 *
 * Resume a saved user context on the current thread: the iretq frame of a
 * fork()ed child, captured from the parent's int $0x80 trapframe with rax
 * forced to 0.  proc_ctx_t (process.h) is 15 GPRs followed by rip, cs,
 * rflags, rsp and ss.  The outgoing frame is built on this thread's kernel
 * stack; kernel re-entry goes through TSS.RSP0 (umode_rsp0_alloc()).
 * Never returns. */
user_mode_enter_ctx:
    pushq 0x98(%rdi)           /* ss */
    pushq 0x90(%rdi)           /* rsp */
    pushq 0x88(%rdi)           /* rflags */
    pushq 0x80(%rdi)           /* cs */
    pushq 0x78(%rdi)           /* rip */
    /* GPRs in the struct's order; rdi is the pointer, so it goes last */
    movq 0x00(%rdi), %rax
    movq 0x08(%rdi), %rbx
    movq 0x10(%rdi), %rcx
    movq 0x18(%rdi), %rdx
    movq 0x20(%rdi), %rsi
    movq 0x30(%rdi), %rbp
    movq 0x38(%rdi), %r8
    movq 0x40(%rdi), %r9
    movq 0x48(%rdi), %r10
    movq 0x50(%rdi), %r11
    movq 0x58(%rdi), %r12
    movq 0x60(%rdi), %r13
    movq 0x68(%rdi), %r14
    movq 0x70(%rdi), %r15
    movq 0x28(%rdi), %rdi
    iretq

.section .bss
.align 8
.global user_mode_return_rip
user_mode_return_rip:
    .skip 8
.global user_mode_kernel_rsp
user_mode_kernel_rsp:
    .skip 8
.global umode_saved_rbx
umode_saved_rbx:
    .skip 8
.global umode_saved_rbp
umode_saved_rbp:
    .skip 8
.global umode_saved_r12
umode_saved_r12:
    .skip 8
.global umode_saved_r13
umode_saved_r13:
    .skip 8
.global umode_saved_r14
umode_saved_r14:
    .skip 8
.global umode_saved_r15
umode_saved_r15:
    .skip 8

.section .rodata
msg_fail:  .asciz "umode: kstack alloc failed\n"

.section .note.GNU-stack,"",@progbits

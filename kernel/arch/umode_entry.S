/* Enter ring 3 user mode via iretq */
.section .text
.global user_mode_enter
.global user_mode_force_return
.extern pmm_alloc_page
.extern pmm_alloc_pages
.extern gdt_set_rsp0
.extern user_mode_return_override
.extern phys_to_virt_base
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

    /* Allocate kernel stack for TSS.RSP0. This stack backs every int0x80/IRQ
     * taken from user mode, so it must be large enough for the deepest syscall
     * handler chain (syscall_handler -> copy_to_user -> kprintf/print_hex/...).
     * A single 4KB page overflowed onto the adjacent user stack pages and
     * corrupted them; use 16 pages (64KB). */
    pushq %rdi
    pushq %rsi
    movq $16, %rdi
    callq pmm_alloc_pages
    popq %rsi
    popq %rdi
    test %rax, %rax
    jnz 1f
    lea msg_fail(%rip), %rdi
    callq kprintf
    cli
    hlt
1:
    addq $0x10000, %rax        /* kstack_top = phys + 16*4096 */
    addq phys_to_virt_base(%rip), %rax  /* convert to virtual */
    movq %rax, %rdi            /* rsp0 = kstack_top (virtual) */
    callq gdt_set_rsp0

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

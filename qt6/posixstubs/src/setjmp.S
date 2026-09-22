/* CodeOS setjmp/longjmp - x86_64 implementation
 * Saves/restores callee-saved registers for Qt6 */

.section .text

/* int setjmp(jmp_buf buf) */
.global setjmp
setjmp:
    mov %rbx,  0(%rdi)
    mov %rbp,  8(%rdi)
    mov %r12, 16(%rdi)
    mov %r13, 24(%rdi)
    mov %r14, 32(%rdi)
    mov %r15, 40(%rdi)
    mov %rsp, 48(%rdi)
    lea 1f(%rip), %rax
    mov %rax, 56(%rdi)        /* return address */
    stmxcsr 64(%rdi)          /* save SSE control word */
    fnstcw  72(%rdi)          /* save x87 control word */
    xor %eax, %eax
    ret
1:
    ret

/* void longjmp(jmp_buf buf, int val) */
.global longjmp
longjmp:
    mov  0(%rdi), %rbx
    mov  8(%rdi), %rbp
    mov 16(%rdi), %r12
    mov 24(%rdi), %r13
    mov 32(%rdi), %r14
    mov 40(%rdi), %r15
    mov 48(%rdi), %rsp
    ldmxcsr 64(%rdi)          /* restore SSE control word */
    fldcw   72(%rdi)          /* restore x87 control word */
    mov 56(%rdi), %rax        /* return address */
    /* val is in %esi; if val == 0, return 1 */
    test %esi, %esi
    mov $1, %eax
    cmovz %eax, %esi
    mov %esi, %eax
    jmp *%rax

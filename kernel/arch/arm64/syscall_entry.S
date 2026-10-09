.section .text
.global arm64_syscall_entry
.extern syscall_handler

/* x8 = syscall number, x0-x6 = args
 * Stack setup by vectors.S
 */
arm64_syscall_entry:
    mov x0, x8
    bl syscall_handler
    eret

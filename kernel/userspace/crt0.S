.section .text
.global _start
_start:
    xorq %rbp, %rbp
    popq %rdi
    movq %rsp, %rsi
    andq $-16, %rsp
    call main
    movq %rax, %rdi
    movq $2, %rax
    int $0x80

/* Mark stack non-executable (silences linker warning, hardens binaries) */
.section .note.GNU-stack,"",@progbits

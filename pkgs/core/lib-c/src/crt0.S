.section .text
.global _start
_start:
    xorq %rbp, %rbp
    movq $0, %rdi
    movq $0, %rsi
    call main
    movq %rax, %rdi
    movq $2, %rax
    int $0x80

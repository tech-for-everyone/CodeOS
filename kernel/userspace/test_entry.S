.section .text
.global _start
_start:
    mov $0, %rax
    mov $0x405000, %rdi
    mov $1, %rsi
    int $0x80
    mov $2, %rax
    int $0x80

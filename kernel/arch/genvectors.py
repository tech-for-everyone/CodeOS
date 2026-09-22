#!/usr/bin/env python3
import sys

out = sys.stdout

out.write('.intel_syntax noprefix\n')
out.write('.code64\n\n')

# Common handler
out.write('''
.macro VEC_NOERR n
    .align 8
    .global vector\\n
vector\\n:
    push 0
    push \\n
    jmp isr_common
.endm

.macro VEC_ERR n
    .align 8
    .global vector\\n
vector\\n:
    push \\n
    jmp isr_common
.endm

.macro VEC_ADDR n
    .quad vector\\n
.endm

isr_common:
    push r15
    push r14
    push r13
    push r12
    push r11
    push r10
    push r9
    push r8
    push rbp
    push rdi
    push rsi
    push rdx
    push rcx
    push rbx
    push rax

    mov rdi, rsp
    call isr_dispatch_c

    pop rax
    pop rbx
    pop rcx
    pop rdx
    pop rsi
    pop rdi
    pop rbp
    pop r8
    pop r9
    pop r10
    pop r11
    pop r12
    pop r13
    pop r14
    pop r15

    add rsp, 16
    iretq

''')

# Exceptions 0-31 with proper error code handling
noerr = {0,1,2,3,4,5,6,7,9,15,16,18,19,20,22,23,24,25,26,27,28,29,30,31}
for i in range(32):
    if i in noerr:
        out.write(f'VEC_NOERR {i}\n')
    else:
        out.write(f'VEC_ERR {i}\n')

out.write('\n')
for i in range(32, 256):
    out.write(f'VEC_NOERR {i}\n')

out.write('\n.section .rodata\n')
out.write('.global vector_table\n')
out.write('vector_table:\n')
for i in range(256):
    out.write(f'    VEC_ADDR {i}\n')

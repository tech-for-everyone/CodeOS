.code64

/* ── Data section ── */
.section .data
.align 8
.global phys_base
phys_base:
    .quad 0
.global phys_to_virt_base
phys_to_virt_base:
    .quad 0

.align 8
.global gdt64
.global gdt64_ptr_end
gdt64:
    .quad 0                     /* 0x00 NULL */
    .quad 0x00209A0000000000    /* 0x08 Kernel Code (DPL=0, 64-bit) */
    .quad 0x0000920000000000    /* 0x10 Kernel Data (DPL=0) */
    .quad 0x0020F20000000000    /* 0x18 User Data (DPL=3) */
    .quad 0x0020FA0000000000    /* 0x20 User Code (DPL=3, 64-bit) */
    .quad 0x0000000000000000    /* 0x28 TSS low  (filled by C) */
    .quad 0x0000000000000000    /* 0x30 TSS high */
gdt64_end:
gdt64_ptr:
    .word gdt64_ptr - gdt64 - 1
    .quad gdt64
gdt64_ptr_end:
    .word gdt64_end - gdt64 - 1
    .quad gdt64

/* ── Limine entry point (64-bit long mode, paging enabled) ── */
.section .text
.global _start_limine
.extern kernel_main

_start_limine:
    /* Load our full GDT (includes TSS slots) and reload segments */
    lgdt gdt64_ptr_end

    /* Reload CS via far return */
    push $0x08
    lea 1f(%rip), %rax
    push %rax
    lretq
1:
    /* Reload data segments */
    mov $0x10, %ax
    mov %ax, %ds
    mov %ax, %es
    mov %ax, %ss
    xor %ax, %ax
    mov %ax, %fs
    mov %ax, %gs

    /* Kernel TLS: thread pointer = end of TLS block, so local-exec accesses
       at negative offsets (tl_status etc.) land in the .tls section. */
    movabs $__tls_end, %rax
    mov %rax, %rdx
    shr $32, %rdx
    mov $0xc0000100, %ecx
    wrmsr
    /* TCB self-pointer at the thread pointer ([TP] = TP) */
    movabs $__tls_end, %rax
    mov %rax, (%rax)

    lea kernel_stack_top(%rip), %rsp
    movq $_kernel_virt_base, %rax
    negq %rax
    movq %rax, phys_base(%rip)
    xor %edi, %edi
    xor %esi, %esi
    call kernel_main
    cli
.halt:
    hlt
    jmp .halt

/* ── BSS: stacks ── */
.section .bss
.align 4096
kernel_stack:
    .skip 1048576
kernel_stack_top:

.section .note.GNU-stack,"",@progbits

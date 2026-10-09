; Multiboot2 header for GRUB2 bootloader
; This file provides the multiboot2 header that GRUB2 uses to identify
; and boot the DevOS kernel. The header must appear in the first 8192
; bytes of the kernel image, 8-byte aligned.
;
; Actual implementation in arch/boot.S (.section .multiboot)

section .multiboot
align 8
    dd 0xE85250D6          ; magic: multiboot2
    dd 0                   ; architecture: 0 = i386
    dd 24                  ; header length
    dd -(0xE85250D6 + 0 + 24) ; checksum
    dw 0                   ; end tag type
    dw 0                   ; end tag flags
    dd 8                   ; end tag size

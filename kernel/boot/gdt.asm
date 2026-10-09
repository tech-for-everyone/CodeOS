; Global Descriptor Table for DevOS
; 64-bit mode uses a flat GDT with these segments:
;   0x00: NULL descriptor
;   0x08: Kernel Code (DPL=0, 64-bit)
;   0x10: Kernel Data (DPL=0)
;   0x18: User Data   (DPL=3)
;   0x20: User Code   (DPL=3, 64-bit)
;   0x28: TSS low     (filled by gdt_tss_init in kernel/gdt.c)
;   0x30: TSS high
;
; GDT loaded in arch/boot.S via lgdt instruction.
; TSS updated at runtime in kernel/gdt.c via gdt_set_rsp0().

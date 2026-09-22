; DevOS boot entry point (32-bit -> 64-bit long mode)
; This file describes the boot sequence used by DevOS.
;
; Actual boot entry in arch/boot.S (_start):
;   1. Sets up stack
;   2. Calls setup_paging to initialize 4-level paging
;   3. Calls enable_long_mode to switch to 64-bit long mode
;   4. Loads GDT with 64-bit code/data segments
;   5. Far jumps to 64-bit entry (_start64)
;   6. Sets up segment registers in 64-bit mode
;   7. Calls kernel_main(magic, mb_info)

; See arch/boot.S for the full implementation.

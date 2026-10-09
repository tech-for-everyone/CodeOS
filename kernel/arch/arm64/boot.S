.section .text
.global _start
.extern kernel_main
.extern exception_vector

.equ PHYSMEM_BASE, 0x40000000

_start:
    /* x0 may carry device tree address from QEMU, stash it */
    mov x20, x0

    /* QEMU may boot us at EL1 with the MMU already enabled (SCTLR_EL1.M=1)
     * using its own page tables. Disable MMU and caches first so we run on
     * flat physical addresses and our page-table writes land where the
     * translation walk will read them. */
    mrs x0, sctlr_el1
    bic x0, x0, #(1 << 0)      /* M: MMU off */
    bic x0, x0, #(1 << 2)      /* C: data cache off */
    bic x0, x0, #(1 << 12)     /* I: instruction cache off */
    msr sctlr_el1, x0
    isb

    /* Set up stack */
    ldr x15, =stack_top
    mov sp, x15

    /* Clear BSS */
    ldr x0, =_bss_start
    ldr x1, =_bss_end
    sub x1, x1, x0
    mov x2, xzr
    mov x3, xzr
    mov x4, xzr
    mov x5, xzr
1:  stp x2, x3, [x0], #16
    stp x4, x5, [x0], #16
    subs x1, x1, #32
    b.gt 1b

    /* Set up exception vectors */
    ldr x15, =exception_vector
    msr vbar_el1, x15
    isb

    /* Enable FP/SIMD access at EL1 (CPACR_EL1.FPEN=0b11) so NEON/FP
     * instructions emitted by the compiler don't trap. */
    mrs x0, cpacr_el1
    orr x0, x0, #(3 << 20)
    msr cpacr_el1, x0
    isb

    /* Set up page tables and enable MMU */
    bl setup_paging
    bl enable_mmu

    /* Reload stack pointer after MMU is enabled */
    ldr x15, =stack_top
    mov sp, x15

    /* Jump to kernel_main */
    mov x0, xzr          /* magic = 0 (not multiboot) */
    mov x1, xzr          /* mb_info = NULL */
    bl kernel_main

    /* Should not return */
    b .

/* Page table structures */
.section .bss
.align 12
pml4:
    .skip 4096
pdp:
    .skip 4096
pd:
    .skip 4096 * 16
stack:
    .skip 32768
stack_top:

.section .text
setup_paging:
    /* Set up page tables for 1:1 identity mapping of physical memory
     * Using 2MB block descriptors covering 0x40000000 - 0x80000000
     */

    /* PML4: entry 0 -> PDP (present + writable) */
    ldr x0, =pml4
    ldr x1, =pdp
    orr x1, x1, #0x3
    str x1, [x0]

    /* PDP: entries 0 and 1 -> PD (table descriptors at level 1)
     * Entry 0: covers VA [0x00000000, 0x40000000)
     * Entry 1: covers VA [0x40000000, 0x80000000) — kernel lives here (0x40080000)
     */
    ldr x0, =pdp
    ldr x1, =pd
    orr x1, x1, #0x3          /* [1:0]=0b11: table descriptor */
    str x1, [x0]              /* PDP[0] -> PD */
    add x0, x0, #8
    str x1, [x0]              /* PDP[1] -> PD */
    add x0, x0, #(255 * 8)    /* move to PDP[256] */
    str x1, [x0]              /* PDP[256] -> PD (PCIe ECAM at 0x4010000000) */

    /* PD: map 0x40000000 to 0x80000000 with 2MB block descriptors
     * Block descriptor (level 2, 4KB granule):
     *   [1:0] = 0b01        — block descriptor (0b11 = table, 0b00 = invalid)
     *   [4:2] = 0b001       — AttrIndx=1 (MAIR[1], Normal WB)
     *   bit 10 = 1          — AF (access flag = already accessed)
     *   [13:12] = 0b10      — SH: Inner Shareable
     *   bit 7  = 0          — XN (executable)
     */
    ldr x0, =pd
    mov x1, #PHYSMEM_BASE
    orr x1, x1, #0x1          /* [1:0]=0b01: block descriptor */
    orr x1, x1, #(1 << 2)     /* AttrIndx[0] (bit 2) -> AttrIndx=1 */
    orr x1, x1, #(1 << 10)    /* AF (accessed) — bit 10 for 4KB granule */
    orr x1, x1, #(2 << 12)    /* SH=0b10: Inner Shareable */
    mov x2, #512            /* 512 entries * 2MB = 1GB */
1:  str x1, [x0]
    add x1, x1, #0x200000   /* next 2MB block */
    add x0, x0, #8
    subs x2, x2, #1
    b.gt 1b

    /* Map UART + virt platform peripherals at 0x09000000 - 0x09100000
     * (UART PL011, PL050 KMI, PL031 RTC, fw_cfg) as device memory
     * (AttrIndx=0 -> MAIR[0] = nGnRnE). */
    ldr x0, =pd
    mov x1, #(0x09000000 >> 21)     /* PD index 72 for VA 0x09000000 */
    mov x2, #0x09000000
    orr x2, x2, #0x1                /* block descriptor */
    orr x2, x2, #(1 << 10)          /* AF */
    mov x3, #8                      /* 8 * 2MB = 16MB device space */
3:  str x2, [x0, x1, lsl #3]
    add x2, x2, #0x200000
    add x1, x1, #1
    subs x3, x3, #1
    b.gt 3b

    /* Map PCIe ECAM (config space) at 0x4010000000.
     * ECAM is 256MB (256 buses) = PD[128..255] under PDP[256].
     * Device memory (AttrIndx=0 -> MAIR[0] = nGnRnE). */
    add x0, x0, #(128 * 8)          /* PD[128] */
    movz x2, #0x1000, lsl #16
    movk x2, #0x40, lsl #32         /* x2 = 0x4010000000 */
    orr x2, x2, #0x1                /* block descriptor */
    orr x2, x2, #(1 << 10)          /* AF */
    mov x3, #128                    /* 128 * 2MB = 256MB ECAM */
2:  str x2, [x0]
    add x2, x2, #0x200000
    add x0, x0, #8
    subs x3, x3, #1
    b.gt 2b
    ret

enable_mmu:
    /* Set TTBR0_EL1 to PML4 */
    ldr x0, =pml4
    msr ttbr0_el1, x0

    /* Set TTBR1_EL1 to PML4 (same for now) */
    msr ttbr1_el1, x0

    /* Configure TCR_EL1 for 4KB granule:
     *   T0SZ=16 (48-bit VA), T1SZ=16
     *   TG0=4KB(0b00), TG1=4KB(0b10)
     *   IRGN0/1=WB(0b11), ORGN0/1=WB(0b11)
     *   SH0/1=Inner-shareable(0b11), IPS=40-bit (0b010)
     */
    ldr x0, =0x00000002BF103F10
    msr tcr_el1, x0
    isb

    /* Configure MAIR_EL1:
     * attr0 = Device memory (0x04)
     * attr1 = Normal memory, WB (0xFF) — used by AttrIndx=1
     */
    ldr x0, =0xFF04
    msr mair_el1, x0
    isb

    /* Flush TLB */
    tlbi vmalle1
    dsb sy
    isb

    /* Enable MMU in SCTLR_EL1 */
    mrs x0, sctlr_el1
    orr x0, x0, #(1 << 0)      /* M = MMU enable (only MMU, no caches yet) */
    msr sctlr_el1, x0
    isb

    ret

/* ---- UART debug helpers (PL011 @ 0x09000000) ---- */
.equ UART_BASE, 0x09000000

debug_write_char:
    ldr x1, =UART_BASE
1:  ldr w2, [x1, #0x18]     /* FR */
    tbnz w2, #5, 1b         /* wait until TXFF clear */
    strb w0, [x1]           /* DR */
    ret

debug_write_hex64:
    stp x29, x30, [sp, #-16]!
    stp x19, x20, [sp, #-16]!
    mov x29, sp
    mov x19, x0
    mov x20, #16
2:  mov x0, x19
    lsr x0, x0, #60
    and x0, x0, #0xf
    cmp x0, #9
    b.gt 3f
    add x0, x0, #48
    b 4f
3:  add x0, x0, #87
4:  bl debug_write_char
    lsl x19, x19, #4
    subs x20, x20, #1
    b.ne 2b
    mov x0, #'\r'
    bl debug_write_char
    mov x0, #'\n'
    bl debug_write_char
    ldp x19, x20, [sp], #16
    ldp x29, x30, [sp], #16
    ret

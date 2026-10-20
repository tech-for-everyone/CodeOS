ENTRY(_start_limine)

_kernel_virt_base = 0xFFFFFFFF80000000;
_kernel_phys_start = 0x200000;
_kernel_virt_start = _kernel_virt_base + _kernel_phys_start;

SECTIONS
{
    . = _kernel_virt_start;

    .limine_requests : AT(ADDR(.limine_requests) - _kernel_virt_base) {
        KEEP(*(.limine_requests))
    }

    .note : AT(ADDR(.note) - _kernel_virt_base) {
        KEEP(*(.note))
    }

    /* The `l`-prefixed catch-alls are load-bearing, not belt-and-braces.
     *
     * With the kernel's compile flags (-mcmodel=large -ffunction-sections
     * -fdata-sections) clang does NOT emit .text.foo / .data.foo / .rodata.foo;
     * it emits .ltext.foo / .ldata.foo / .lrodata.foo, marked SHF_LINK_ORDER.
     * The patterns below only matched the non-prefixed names, so every one of
     * those sections was an ORPHAN: ld placed it outside the script's own
     * sections and gave it its own segment.
     *
     * That is survivable until it is not.  The Zircon kernel link produced
     *
     *   LOAD 0x001000 vaddr 0xffffffff80200000 filesz 0x1c3010 memsz 0x4836960
     *   LOAD 0x1c405c vaddr 0xffffffff803c205c filesz 0x1c0    memsz 0x1c0
     *
     * i.e. the second LOAD (.data and the orphans) sat *inside* the first
     * one's memory range, which is an inconsistent layout.  Limine's ELF
     * loader rejects it outright:
     *
     *   PANIC: elf: Attempted to load ELF file with overlapping PHDRs
     *          (0 and 1 overlap)
     *
     * and the Zircon ISO never reached its kernel.  The CodeOS kernel linked
     * to a single LOAD by luck of its layout, which is exactly why the bug
     * looked like a Zircon-only problem.
     *
     * Catching the families here makes them ordinary script sections: they
     * are laid out with everything else, and the image has one segment.
     */
    .text : AT(ADDR(.text) - _kernel_virt_base) {
        *(.text*)
        *(.ltext*)
    }

    . = ALIGN(32);
    .rodata : AT(ADDR(.rodata) - _kernel_virt_base) {
        *(.rodata*)
        *(.lrodata*)
    }

    . = ALIGN(4096);
    .data : AT(ADDR(.data) - _kernel_virt_base) {
        *(COMMON)
        *(.data*)
        *(.ldata*)
    }

    . = ALIGN(8);
    .got : AT(ADDR(.got) - _kernel_virt_base) {
        *(.got)
    }

    . = ALIGN(8);
    .got.plt : AT(ADDR(.got.plt) - _kernel_virt_base) {
        __got_plt_base = .;
        *(.got.plt)
    }

    . = ALIGN(4096);
    .preinit_array : AT(ADDR(.preinit_array) - _kernel_virt_base) {
        PROVIDE_HIDDEN (__preinit_array_start = .);
        KEEP (*(.preinit_array*))
        PROVIDE_HIDDEN (__preinit_array_end = .);
    }

    . = ALIGN(8);
    .init_array : AT(ADDR(.init_array) - _kernel_virt_base) {
        PROVIDE_HIDDEN (__init_array_start = .);
        KEEP (*(SORT_BY_INIT_PRIORITY(.init_array.*) SORT_BY_INIT_PRIORITY(.ctors.*)))
        KEEP (*(.init_array .ctors))
        PROVIDE_HIDDEN (__init_array_end = .);
    }

    . = ALIGN(8);
    .fini_array : AT(ADDR(.fini_array) - _kernel_virt_base) {
        PROVIDE_HIDDEN (__fini_array_start = .);
        KEEP (*(.fini_array*))
        KEEP (*(.fini_array .dtors))
        PROVIDE_HIDDEN (__fini_array_end = .);
    }

    . = ALIGN(4096);
    .eh_frame : AT(ADDR(.eh_frame) - _kernel_virt_base) {
        *(.eh_frame*)
    }

    . = ALIGN(64);
    .tls : AT(ADDR(.tls) - _kernel_virt_base) {
        __tls_start = .;
        KEEP(*(.tdata .tdata.*))
        KEEP(*(.tbss .tbss.*))
        . = ALIGN(64);
        __tls_end = .;
    }

    . = ALIGN(4096);
    .bss : AT(ADDR(.bss) - _kernel_virt_base) {
        *(COMMON)
        *(.bss*)
        *(.lbss*)
    }

    _kernel_virt_end = .;
    _kernel_phys_end = _kernel_phys_start + (_kernel_virt_end - _kernel_virt_start);
    _kernel_start = _kernel_virt_start;
    _kernel_end = _kernel_virt_end;

    /DISCARD/ : {
        *(.comment)
        *(.note.GNU-stack)
        *(.note.gnu.property)
        *(.debug*)
    }
}

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

    .text : AT(ADDR(.text) - _kernel_virt_base) {
        *(.text*)
    }

    . = ALIGN(32);
    .rodata : AT(ADDR(.rodata) - _kernel_virt_base) {
        *(.rodata*)
    }

    . = ALIGN(4096);
    .data : AT(ADDR(.data) - _kernel_virt_base) {
        *(COMMON)
        *(.data*)
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
        KEEP (*(SORT_BY_INIT_PRIORITY(.fini_array.*) SORT_BY_INIT_PRIORITY(.dtors.*)))
        KEEP (*(.fini_array))
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

OUTPUT_FORMAT(elf64-littleaarch64)
OUTPUT_ARCH(aarch64)
ENTRY(_start)

PHDRS
{
    text    PT_LOAD FLAGS(5);
    rodata  PT_LOAD FLAGS(4);
    data    PT_LOAD FLAGS(6);
}

SECTIONS
{
    . = 0x40080000;

    .text : {
        *(.text*)
    } :text

    . = ALIGN(4096);
    .rodata : {
        *(.rodata*)
    } :rodata

    . = ALIGN(4096);
    .data : {
        *(.data*)
    } :data

    . = ALIGN(4096);
    .bss : {
        _bss_start = .;
        *(.bss*)
        *(COMMON)
        . = ALIGN(4096);
        _bss_end = .;
    } :data

    _kernel_start = 0x40080000;
    _kernel_end = .;
}

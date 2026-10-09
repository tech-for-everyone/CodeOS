OUTPUT_FORMAT(elf64-x86-64)
ENTRY(_start)

PHDRS
{
  text   PT_LOAD FLAGS(5); /* R-X */
  rodata PT_LOAD FLAGS(4); /* R-- */
  data   PT_LOAD FLAGS(6); /* RW- */
}

SECTIONS
{
  . = 0x400000 + SIZEOF_HEADERS;

  .text : {
    *(.text*)
  } :text

  .rodata : {
    *(.rodata*)
  } :rodata

  .data : {
    *(.data*)
  } :data

  .bss : {
    *(.bss*)
    *(COMMON)
  } :data

  /DISCARD/ : {
    *(.comment*)
    *(.eh_frame*)
    *(.debug*)
  }
}

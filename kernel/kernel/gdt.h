#ifndef GDT_H
#define GDT_H

#include "types.h"

#define GDT_KERNEL_CS 0x08
#define GDT_KERNEL_DS 0x10
#define GDT_USER_DS   0x18
#define GDT_USER_CS   0x20
#define GDT_TSS       0x28

#define USER_CS (GDT_USER_CS | 3)
#define USER_DS (GDT_USER_DS | 3)

void gdt_tss_init(void);
void gdt_set_tss(uint64_t base, uint32_t limit);
void gdt_set_rsp0(uint64_t rsp0);

void syscall_msr_init(void *handler_entry);

#endif
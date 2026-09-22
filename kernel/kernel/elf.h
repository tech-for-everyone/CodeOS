#ifndef ELF_H
#define ELF_H

#include "types.h"

#define ELF_MAGIC 0x464C457F

#define PT_LOAD    1
#define PT_INTERP  3

/* Auxiliary vector types */
#define AT_NULL     0
#define AT_PHDR     3
#define AT_PHENT    4
#define AT_PHNUM    5
#define AT_PAGESZ   6
#define AT_BASE     7
#define AT_FLAGS    8
#define AT_ENTRY    9
#define AT_UID      11
#define AT_EUID     12
#define AT_GID      13
#define AT_EGID     14
#define AT_HWCAP    16
#define AT_CLKTCK   17
#define AT_SECURE   23
#define AT_RANDOM   25

typedef struct {
    uint64_t phdr_addr;
    uint64_t phent;
    uint64_t phnum;
    uint64_t entry;
    uint64_t interp_base;
    uint64_t interp_entry;
} elf_auxv_info_t;

int elf_load(const char *path, uint64_t *entry_out, uint64_t *stack_out, elf_auxv_info_t *auxv);
uint64_t elf_setup_stack(uint64_t stack_top, uint64_t entry, int argc, char **argv, int envc, char **envp, elf_auxv_info_t *auxv);

#endif

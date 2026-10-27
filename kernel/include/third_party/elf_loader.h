#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifndef __ELF_LOADER_H
#define __ELF_LOADER_H

#if defined(__USE_LINUX_MODULES__)

#if defined(__X86__)
 #define CONFIG_ARCH_X86 1
 #undef CONFIG_ARCH_ARM
 #undef CONFIG_ARCH_RISCV
#elif defined(__ARM__)
 #define CONFIG_ARCH_ARM 1
 #undef CONFIG_ARCH_X86
 #undef CONFIG_ARCH_RISCV
#elif defined(__RISCV__)
 #define CONFIG_ARCH_RISCV 1
 #undef CONFIG_ARCH_X86
 #undef CONFIG_ARCH_ARM
#else
 #if defined(__i386__) || defined(__x86_64__)
 #define CONFIG_ARCH_X86 1
 #endif
 #if defined(__arm__) || defined(__aarch64__)
 #define CONFIG_ARCH_ARM 1
 #endif
 #if defined(__riscv) || defined(__riscv__)
 #define CONFIG_ARCH_RISCV 1
 #endif
#endif

#if defined(__32BIT__) && !defined(__64BIT__)
 #define CONFIG_ELF_32 1
 #undef CONFIG_ELF_64
#elif defined(__64BIT__) && !defined(__32BIT__)
 #define CONFIG_ELF_64 1
 #undef CONFIG_ELF_32
#else
 #define CONFIG_ELF_32 1
 #define CONFIG_ELF_64 1
#endif

#define EI_NIDENT 16
#define EI_MAG0 0
#define EI_MAG1 1
#define EI_MAG2 2
#define EI_MAG3 3
#define EI_CLASS 4
#define EI_DATA 5
#define EI_VERSION 6
#define ELFMAG0 0x7F
#define ELFMAG1 'E'
#define ELFMAG2 'L'
#define ELFMAG3 'F'
#define ELFCLASS32 1
#define ELFCLASS64 2
#define ELFDATA2LSB 1
#define ELFDATA2MSB 2
#define ET_REL 1
#define EM_386 3
#define EM_ARM 40
#define EM_X86_64 62
#define EM_AARCH64 183
#define EM_RISCV 243
#define SHT_NULL 0
#define SHT_PROGBITS 1
#define SHT_SYMTAB 2
#define SHT_STRTAB 3
#define SHT_RELA 4
#define SHT_NOBITS 8
#define SHT_REL 9
#define SHT_SYMTAB_SHNDX 18
#define SHF_WRITE (1ULL << 0)
#define SHF_ALLOC (1ULL << 1)
#define SHF_EXECINSTR (1ULL << 2)
#define SHN_UNDEF 0U
#define SHN_LORESERVE 0xFF00U
#define SHN_ABS 0xFFF1U
#define SHN_COMMON 0xFFF2U
#define SHN_XINDEX 0xFFFFU

#if defined(CONFIG_ARCH_RISCV)
#define R_RISCV_NONE 0
#define R_RISCV_32 1
#define R_RISCV_64 2
#define R_RISCV_CALL 18
#define R_RISCV_HI20 26
#define R_RISCV_LO12_I 27
#endif

#if defined(CONFIG_ARCH_ARM)
#define R_ARM_NONE 0
#define R_ARM_ABS32 2
#define R_ARM_CALL 28
#define R_ARM_JUMP24 29
#define R_AARCH64_NONE 0
#define R_AARCH64_ABS64 257
#define R_AARCH64_ABS32 258
#define R_AARCH64_CALL26 283
#endif

#if defined(CONFIG_ARCH_X86)
#define R_386_NONE 0
#define R_386_32 1
#define R_386_PC32 2
#define R_X86_64_NONE 0
#define R_X86_64_64 1
#define R_X86_64_PC32 2
#define R_X86_64_32 10
#endif

#if defined(CONFIG_ELF_32)

typedef struct {
 uint8_t e_ident[EI_NIDENT];
 uint16_t e_type;
 uint16_t e_machine;
 uint32_t e_version;
 uint32_t e_entry;
 uint32_t e_phoff;
 uint32_t e_shoff;
 uint32_t e_flags;
 uint16_t e_ehsize;
 uint16_t e_phentsize;
 uint16_t e_phnum;
 uint16_t e_shentsize;
 uint16_t e_shnum;
 uint16_t e_shstrndx;
} Elf32_Ehdr;

typedef struct {
 uint32_t sh_name;
 uint32_t sh_type;
 uint32_t sh_flags;
 uint32_t sh_addr;
 uint32_t sh_offset;
 uint32_t sh_size;
 uint32_t sh_link;
 uint32_t sh_info;
 uint32_t sh_addralign;
 uint32_t sh_entsize;
} Elf32_Shdr;

typedef struct {
 uint32_t st_name;
 uint32_t st_value;
 uint32_t st_size;
 uint8_t st_info;
 uint8_t st_other;
 uint16_t st_shndx;
} Elf32_Sym;

typedef struct {
 uint32_t r_offset;
 uint32_t r_info;
} Elf32_Rel;

typedef struct {
 uint32_t r_offset;
 uint32_t r_info;
 int32_t r_addend;
} Elf32_Rela;

#define ELF32_R_SYM(i) (((uint32_t)(i)) >> 8)
#define ELF32_R_TYPE(i) ((uint32_t)((i) & 0xFF))
#define ELF32_ST_BIND(val) (((uint8_t)(val)) >> 4)
#define ELF32_ST_TYPE(val) ((val) & 0xFU)
#endif

#if defined(CONFIG_ELF_64)
typedef struct {
 uint8_t e_ident[EI_NIDENT];
 uint16_t e_type;
 uint16_t e_machine;
 uint32_t e_version;
 uint64_t e_entry;
 uint64_t e_phoff;
 uint64_t e_shoff;
 uint32_t e_flags;
 uint16_t e_ehsize;
 uint16_t e_phentsize;
 uint16_t e_phnum;
 uint16_t e_shentsize;
 uint16_t e_shnum;
 uint16_t e_shstrndx;
} Elf64_Ehdr;

typedef struct {
 uint32_t sh_name;
 uint32_t sh_type;
 uint64_t sh_flags;
 uint64_t sh_addr;
 uint64_t sh_offset;
 uint64_t sh_size;
 uint32_t sh_link;
 uint32_t sh_info;
 uint64_t sh_addralign;
 uint64_t sh_entsize;
} Elf64_Shdr;

typedef struct {
 uint32_t st_name;
 uint8_t st_info;
 uint8_t st_other;
 uint16_t st_shndx;
 uint64_t st_value;
 uint64_t st_size;
} Elf64_Sym;

typedef struct {
 uint64_t r_offset;
 uint64_t r_info;
} Elf64_Rel;

typedef struct {
 uint64_t r_offset;
 uint64_t r_info;
 int64_t r_addend;
} Elf64_Rela;

#define ELF64_R_SYM(i) (((uint64_t)(i)) >> 32)
#define ELF64_R_TYPE(i) ((uint32_t)((i) & 0xFFFFFFFFULL))
#define ELF64_ST_BIND(val) (((uint8_t)(val)) >> 4)
#define ELF64_ST_TYPE(val) ((val) & 0xFU)
#endif

#define MODULE_NAME_LEN 64
#define MAX_MODULE_ALLOCATIONS 32

struct module_allocation {
 uintptr_t addr;
 size_t size;
 uint32_t flags;
};

struct linux_ko_module {
 char name[MODULE_NAME_LEN];
 void *base_address;
 size_t total_size;
 struct module_allocation allocs[MAX_MODULE_ALLOCATIONS];
 size_t alloc_count;
 int (*init_module)(void);
 void (*cleanup_module)(void);
 uint32_t refcnt;
 uint32_t state;
 bool is_loaded;
};

extern int load_linux_ko(const void *elf_data, size_t data_len, struct linux_ko_module *out_mod);
extern int unload_linux_ko(struct linux_ko_module *mod);
extern void *linux_kmi_lookup_symbol(const char *name);

#else

struct linux_ko_module;
static inline int load_linux_ko(const void *elf_data, size_t data_len, struct linux_ko_module *out_mod) {
 (void)elf_data;
 (void)data_len;
 (void)out_mod;
 return -1;
}
static inline int unload_linux_ko(struct linux_ko_module *mod) {
 (void)mod;
 return -1;
}
static inline void *linux_kmi_lookup_symbol(const char *name) {
 (void)name;
 return ((void *)0);
}

#endif

#endif

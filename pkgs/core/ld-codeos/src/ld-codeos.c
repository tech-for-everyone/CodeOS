/* ld-codeos.c — userspace dynamic linker for CodeOS
 *
 * Loaded by the kernel when the main ELF has PT_INTERP.  Parses the
 * auxiliary vector, maps DT_NEEDED shared libraries, resolves
 * relocations, calls init functions, and jumps to the main binary.
 *
 * Compile as a standalone PIE linked at 0x60000000 so it does not
 * collide with the main binary (usually 0x400000).
 */

/* ── ELF constants ── */
#define PT_NULL    0
#define PT_LOAD    1
#define PT_DYNAMIC 2
#define PT_INTERP  3
#define PT_PHDR    6

#define DT_NULL    0
#define DT_NEEDED  1
#define DT_PLTRELSZ  2
#define DT_PLTGOT    3
#define DT_HASH      4
#define DT_STRTAB    5
#define DT_SYMTAB    6
#define DT_RELA      7
#define DT_RELASZ    8
#define DT_RELAENT   9
#define DT_STRSZ    10
#define DT_SYMENT   11
#define DT_INIT     12
#define DT_FINI     13
#define DT_SONAME   14
#define DT_PLTREL   20
#define DT_DEBUG    21
#define DT_JMPREL   23
#define DT_INIT_ARRAY   25
#define DT_FINI_ARRAY   26
#define DT_INIT_ARRAYSZ 27
#define DT_FINI_ARRAYSZ 28

/* Relocation types */
#define R_X86_64_NONE     0
#define R_X86_64_64       1
#define R_X86_64_PC32     2
#define R_X86_64_GLOB_DAT 6
#define R_X86_64_JUMP_SLOT 7
#define R_X86_64_RELATIVE 8
#define R_X86_64_32       10
#define R_X86_64_32S      11

/* Symbol binding and type */
#define ELF64_ST_BIND(info) ((info) >> 4)
#define ELF64_ST_TYPE(info) ((info) & 0xF)

/* Auxiliary vector types */
#define AT_NULL     0
#define AT_IGNORE   1
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
#define AT_SECURE   23
#define AT_RANDOM   25
#define AT_INTERP   31

/* Syscall numbers (CodeOS native, used after clearing personality) */
#define SYS_READ     5
#define SYS_WRITE    0
#define SYS_OPEN     4
#define SYS_CLOSE    13
#define SYS_MMAP     17
#define SYS_BRK      16
#define SYS_EXIT     2
#define SYS_SET_PERSONALITY 30

#define PERSONALITY_LINUX 1

#define PAGE_SIZE 4096

/* ── Auxiliary entry on stack ── */
typedef struct { unsigned long a_type; unsigned long a_val; } auxv_entry_t;

/* ELF headers — minimal definitions to avoid external headers */
typedef struct {
    unsigned char ident[16];
    unsigned short type;
    unsigned short machine;
    unsigned int   version;
    unsigned long  entry;
    unsigned long  phoff;
    unsigned long  shoff;
    unsigned int   flags;
    unsigned short ehsize;
    unsigned short phentsize;
    unsigned short phnum;
    unsigned short shentsize;
    unsigned short shnum;
    unsigned short shstrndx;
} elf64_ehdr_t;

typedef struct {
    unsigned int   type;
    unsigned int   flags;
    unsigned long  offset;
    unsigned long  vaddr;
    unsigned long  paddr;
    unsigned long  filesz;
    unsigned long  memsz;
    unsigned long  align;
} elf64_phdr_t;

typedef struct {
    unsigned long  d_tag;
    unsigned long  d_val;
} elf64_dyn_t;

typedef struct {
    unsigned int   st_name;
    unsigned char  st_info;
    unsigned char  st_other;
    unsigned short st_shndx;
    unsigned long  st_value;
    unsigned long  st_size;
} elf64_sym_t;

typedef struct {
    unsigned long  r_offset;
    unsigned long  r_info;
    long           r_addend;
} elf64_rela_t;

#define ELF64_R_SYM(info)  ((info) >> 32)
#define ELF64_R_TYPE(info) ((unsigned int)(info))

/* ── Loaded library tracking ── */
#define MAX_LIBS 64

typedef struct {
    unsigned long base;       /* mmap base address */
    unsigned long dyn;        /* address of .dynamic section */
    unsigned long strtab;
    unsigned long symtab;
    unsigned long rela;
    unsigned long relasz;
    unsigned long relaent;
    unsigned long jmprel;
    unsigned long pltrelsz;
    unsigned long pltrel;
    unsigned long pltgot;
    unsigned long init;
    unsigned long init_array;
    unsigned long init_arraysz;
    unsigned long fini;
    unsigned long fini_array;
    unsigned long fini_arraysz;
    int           strsz;
    int           syment;
} loaded_lib_t;

static loaded_lib_t libs[MAX_LIBS];
static int lib_count = 0;

/* ── Inline assembly syscall wrappers (CodeOS ABI) ── */
static inline long dl_syscall0(long n) {
    long ret;
    asm volatile("int $0x80" : "=a"(ret) : "a"(n) : "memory");
    return ret;
}
static inline long dl_syscall1(long n, long a1) {
    long ret;
    asm volatile("int $0x80" : "=a"(ret) : "a"(n), "D"(a1) : "memory");
    return ret;
}
static inline long dl_syscall2(long n, long a1, long a2) {
    long ret;
    asm volatile("int $0x80" : "=a"(ret) : "a"(n), "D"(a1), "S"(a2) : "memory");
    return ret;
}
static inline long dl_syscall3(long n, long a1, long a2, long a3) {
    long ret;
    asm volatile("int $0x80" : "=a"(ret) : "a"(n), "D"(a1), "S"(a2), "d"(a3) : "memory");
    return ret;
}
static inline long dl_syscall6(long n, long a1, long a2, long a3, long a4, long a5, long a6) {
    long ret;
    register long r10 asm("r10") = a4;
    register long r8  asm("r8")  = a5;
    register long r9  asm("r9")  = a6;
    asm volatile("int $0x80" : "=a"(ret)
                 : "a"(n), "D"(a1), "S"(a2), "d"(a3), "r"(r10), "r"(r8), "r"(r9)
                 : "memory");
    return ret;
}

/* ── Minimal memory operations ── */
static void dl_memcpy(void *dst, const void *src, unsigned long n) {
    unsigned char *d = (unsigned char *)dst;
    const unsigned char *s = (const unsigned char *)src;
    for (unsigned long i = 0; i < n; i++) d[i] = s[i];
}

static void dl_memset(void *dst, int c, unsigned long n) {
    unsigned char *d = (unsigned char *)dst;
    for (unsigned long i = 0; i < n; i++) d[i] = (unsigned char)c;
}

static int dl_strcmp(const char *a, const char *b) {
    while (*a && *a == *b) { a++; b++; }
    return (unsigned char)*a - (unsigned char)*b;
}

/* ── File operations (raw CodeOS syscalls) ── */
static int dl_open(const char *path) {
    return (int)dl_syscall2(SYS_OPEN, (long)path, 0);
}

static int dl_close(int fd) {
    return (int)dl_syscall1(SYS_CLOSE, fd);
}

static long dl_read(int fd, void *buf, unsigned long count) {
    return dl_syscall3(SYS_READ, fd, (long)buf, count);
}

static unsigned long dl_mmap(unsigned long addr, unsigned long length,
                             int prot, int flags, int fd, unsigned long offset) {
    return (unsigned long)dl_syscall6(SYS_MMAP, addr, length, prot, flags, fd, offset);
}

/* ── Console output for debugging ── */
static void dl_putchar(char c) {
    dl_syscall1(SYS_WRITE, (unsigned char)c);
}

static void dl_puts(const char *s) {
    while (*s) dl_putchar(*s++);
}

/* ── Parse auxiliary vector from the stack ── */
/* Returns the address of the first auxv entry (after argc/argv/envp). */
static auxv_entry_t *dl_parse_auxv(unsigned long *sp) {
    unsigned long argc = *sp;
    sp++;
    /* Skip argv */
    for (unsigned long i = 0; i <= argc; i++) sp++;
    /* Skip envp */
    while (*sp) sp++;
    sp++;
    return (auxv_entry_t *)sp;
}

/* ── Read ELF file into a heap-allocated buffer ── */
static int dl_read_file(const char *path, unsigned char **buf, unsigned long *sz) {
    int fd = dl_open(path);
    if (fd < 0) return -1;

    /* Get size by seeking to end using brk trick or just read in chunks */
    unsigned char tmp[4096];
    unsigned long total = 0;

    /* Use mmap to allocate buffer — need to find size first.
     * Simple approach: read 4K at a time into a growing buffer */
    unsigned long cap = 65536;
    *buf = (unsigned char *)dl_mmap(0, cap, 3, 0x22, -1, 0); /* PROT_READ|PROT_WRITE, MAP_PRIVATE|MAP_ANONYMOUS */
    if ((long)*buf < 0) { dl_close(fd); return -1; }

    while (1) {
        long n = dl_read(fd, tmp, sizeof(tmp));
        if (n <= 0) break;
        if (total + (unsigned long)n > cap) {
            /* Realloc not supported, error */
            dl_close(fd);
            return -1;
        }
        dl_memcpy(*buf + total, tmp, (unsigned long)n);
        total += (unsigned long)n;
    }
    dl_close(fd);
    *sz = total;
    return 0;
}

/* ── Load a shared library (.so) ── */
/* Returns the base address, or 0 on failure. */
static unsigned long dl_load_library(const char *path, loaded_lib_t *lib) {
    dl_memset(lib, 0, sizeof(*lib));

    unsigned char *file_buf;
    unsigned long file_sz;
    if (dl_read_file(path, &file_buf, &file_sz) < 0) {
        dl_puts("ld-codeos: failed to read "); dl_puts(path); dl_puts("\n");
        return 0;
    }

    if (file_sz < sizeof(elf64_ehdr_t)) {
        dl_puts("ld-codeos: "); dl_puts(path); dl_puts(" too small\n");
        return 0;
    }

    elf64_ehdr_t *ehdr = (elf64_ehdr_t *)file_buf;
    if (ehdr->ident[0] != 0x7F || ehdr->ident[1] != 'E' || ehdr->ident[2] != 'L' || ehdr->ident[3] != 'F') {
        dl_puts("ld-codeos: "); dl_puts(path); dl_puts(" bad ELF magic\n");
        return 0;
    }

    unsigned long phoff = ehdr->phoff;
    unsigned int phnum = ehdr->phnum;
    unsigned int phentsize = ehdr->phentsize;

    if (phoff + (unsigned long)phnum * phentsize > file_sz) {
        dl_puts("ld-codeos: "); dl_puts(path); dl_puts(" truncated headers\n");
        return 0;
    }

    /* Find library base (lowest vaddr among PT_LOAD) and total span */
    unsigned long min_vaddr = ~0UL;
    unsigned long max_vaddr = 0;
    for (unsigned int i = 0; i < phnum; i++) {
        elf64_phdr_t *ph = (elf64_phdr_t *)(file_buf + phoff + i * phentsize);
        if (ph->type == PT_LOAD) {
            if (ph->vaddr < min_vaddr) min_vaddr = ph->vaddr;
            unsigned long end = ph->vaddr + ph->memsz;
            if (end > max_vaddr) max_vaddr = end;
        }
    }

    if (min_vaddr == ~0UL) {
        dl_puts("ld-codeos: "); dl_puts(path); dl_puts(" no PT_LOAD\n");
        return 0;
    }

    /* Allocate space for the library */
    unsigned long map_size = (max_vaddr - min_vaddr + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
    unsigned long base = dl_mmap(0, map_size, 3, 0x22, -1, 0);
    if ((long)base < 0) {
        dl_puts("ld-codeos: mmap failed for "); dl_puts(path); dl_puts("\n");
        return 0;
    }

    /* Map each PT_LOAD segment */
    for (unsigned int i = 0; i < phnum; i++) {
        elf64_phdr_t *ph = (elf64_phdr_t *)(file_buf + phoff + i * phentsize);
        if (ph->type != PT_LOAD) continue;

    unsigned long vaddr = base + ph->vaddr - min_vaddr;
    unsigned long filesz = ph->filesz;
    unsigned long memsz = ph->memsz;
    unsigned long offset = ph->offset;

    /* Copy data from file */
        if (offset < file_sz) {
            unsigned long copy = filesz;
            if (offset + copy > file_sz) copy = file_sz - offset;
            dl_memcpy((void *)vaddr, file_buf + offset, copy);
        }
        /* Zero BSS */
        if (memsz > filesz) {
            dl_memset((void *)(vaddr + filesz), 0, memsz - filesz);
        }
    }

    lib->base = base;
    lib->dyn  = 0;

    /* Find PT_DYNAMIC from the original file headers before we discard file_buf */
    unsigned long dyn_vaddr = 0;
    for (unsigned int i = 0; i < phnum; i++) {
        elf64_phdr_t *ph = (elf64_phdr_t *)(file_buf + phoff + i * phentsize);
        if (ph->type == PT_DYNAMIC) {
            dyn_vaddr = base + ph->vaddr - min_vaddr;
            break;
        }
    }

    /* Parse dynamic section */
    if (dyn_vaddr) {
        elf64_dyn_t *dyn = (elf64_dyn_t *)dyn_vaddr;
        while (dyn->d_tag != DT_NULL) {
            switch (dyn->d_tag) {
            case DT_STRTAB: lib->strtab = base + dyn->d_val - min_vaddr; break;
            case DT_STRSZ:  lib->strsz  = (int)dyn->d_val; break;
            case DT_SYMTAB: lib->symtab = base + dyn->d_val - min_vaddr; break;
            case DT_SYMENT: lib->syment = (int)dyn->d_val; break;
            case DT_RELA:   lib->rela   = base + dyn->d_val - min_vaddr; break;
            case DT_RELASZ: lib->relasz = dyn->d_val; break;
            case DT_RELAENT:lib->relaent= dyn->d_val; break;
            case DT_JMPREL: lib->jmprel = base + dyn->d_val - min_vaddr; break;
            case DT_PLTRELSZ:lib->pltrelsz = dyn->d_val; break;
            case DT_PLTREL: lib->pltrel = dyn->d_val; break;
            case DT_PLTGOT: lib->pltgot = base + dyn->d_val - min_vaddr; break;
            case DT_INIT:   lib->init   = base + dyn->d_val - min_vaddr; break;
            case DT_FINI:   lib->fini   = base + dyn->d_val - min_vaddr; break;
            case DT_INIT_ARRAY:   lib->init_array   = base + dyn->d_val - min_vaddr; break;
            case DT_INIT_ARRAYSZ: lib->init_arraysz = dyn->d_val; break;
            case DT_FINI_ARRAY:   lib->fini_array   = base + dyn->d_val - min_vaddr; break;
            case DT_FINI_ARRAYSZ: lib->fini_arraysz = dyn->d_val; break;
            }
            dyn++;
        }
    }

    lib->dyn  = dyn_vaddr;
    return base;
}

/* ── Find symbol in all loaded libraries ── */
static unsigned long dl_find_symbol(const char *name) {
    for (int l = 0; l < lib_count; l++) {
        loaded_lib_t *lib = &libs[l];
        if (!lib->symtab || !lib->strtab) continue;

        int syment = lib->syment ? lib->syment : 24;
        elf64_sym_t *sym = (elf64_sym_t *)lib->symtab;
        while (1) {
            unsigned int sym_name = sym->st_name;
            if (sym_name && sym_name < (unsigned int)lib->strsz) {
                const char *sym_str = (const char *)lib->strtab + sym_name;
                if (dl_strcmp(sym_str, name) == 0) {
                    unsigned long val = sym->st_value;
                    if (ELF64_ST_TYPE(sym->st_info) != 0) { /* STT_NOTYPE = 0, skip */
                        /* For function symbols, value is offset from base */
                        if (val) return lib->base + val;
                    }
                }
            }
            if (sym->st_name == 0 && sym->st_value == 0 && sym->st_size == 0) break;
            sym = (elf64_sym_t *)((unsigned char *)sym + syment);
        }
    }
    return 0;
}



/* ── Apply relocations for one loaded library ── */
static int dl_process_relocations(loaded_lib_t *lib) {
    /* Process RELA (non-PLT relocations) */
    if (lib->rela && lib->relasz) {
        unsigned long count = lib->relasz / (lib->relaent ? lib->relaent : 24);
        elf64_rela_t *rela = (elf64_rela_t *)lib->rela;
        for (unsigned long i = 0; i < count; i++) {
            unsigned long r_type = ELF64_R_TYPE(rela[i].r_info);
            unsigned long r_sym  = ELF64_R_SYM(rela[i].r_info);
            unsigned long *addr = (unsigned long *)rela[i].r_offset;

            switch (r_type) {
            case R_X86_64_RELATIVE:
                *addr = lib->base + rela[i].r_addend;
                break;
            case R_X86_64_GLOB_DAT:
            case R_X86_64_64: {
                /* Look up symbol and write its address */
                if (r_sym && lib->symtab && lib->strtab) {
                    elf64_sym_t *sym = (elf64_sym_t *)lib->symtab + r_sym;
                    if (sym->st_name < (unsigned int)lib->strsz) {
                        const char *sym_name = (const char *)lib->strtab + sym->st_name;
                        unsigned long sym_addr = 0;
                        /* Try to resolve in any loaded library */
                        for (int l = 0; l < lib_count && !sym_addr; l++) {
                            loaded_lib_t *l2 = &libs[l];
                            if (!l2->symtab || !l2->strtab) continue;
                            int syment = l2->syment ? l2->syment : 24;
                            elf64_sym_t *sym2 = (elf64_sym_t *)l2->symtab;
                            while (1) {
                                if (sym2->st_name < (unsigned int)l2->strsz) {
                                    const char *s2 = (const char *)l2->strtab + sym2->st_name;
                                    if (dl_strcmp(s2, sym_name) == 0) {
                                        sym_addr = l2->base + sym2->st_value;
                                        break;
                                    }
                                }
                                if (sym2->st_name == 0 && sym2->st_value == 0 && sym2->st_size == 0) break;
                                sym2 = (elf64_sym_t *)((unsigned char *)sym2 + syment);
                            }
                        }
                        if (sym_addr) *addr = sym_addr + rela[i].r_addend;
                    }
                }
                break;
            }
            case R_X86_64_32:
            case R_X86_64_32S: {
                /* Same as 64-bit but truncate to 32-bit */
                if (r_sym && lib->symtab && lib->strtab) {
                    elf64_sym_t *sym = (elf64_sym_t *)lib->symtab + r_sym;
                    if (sym->st_name < (unsigned int)lib->strsz) {
                        const char *sym_name = (const char *)lib->strtab + sym->st_name;
                        unsigned long sym_addr = dl_find_symbol(sym_name);
                        if (sym_addr) {
                            unsigned long val = sym_addr + rela[i].r_addend;
                            *(unsigned int *)addr = (unsigned int)val;
                        }
                    }
                }
                break;
            }
            }
        }
    }

    /* Process PLT relocations (JUMP_SLOT) */
    if (lib->jmprel && lib->pltrelsz) {
        unsigned long count = lib->pltrelsz / 24;
        elf64_rela_t *rela = (elf64_rela_t *)lib->jmprel;
        for (unsigned long i = 0; i < count; i++) {
            unsigned long r_type = ELF64_R_TYPE(rela[i].r_info);
            unsigned long r_sym  = ELF64_R_SYM(rela[i].r_info);
            unsigned long *addr = (unsigned long *)rela[i].r_offset;

            if (r_type == R_X86_64_JUMP_SLOT) {
                if (r_sym && lib->symtab && lib->strtab) {
                    elf64_sym_t *sym = (elf64_sym_t *)lib->symtab + r_sym;
                    if (sym->st_name < (unsigned int)lib->strsz) {
                        const char *sym_name = (const char *)lib->strtab + sym->st_name;
                        unsigned long sym_addr = dl_find_symbol(sym_name);
                        if (sym_addr) {
                            *addr = sym_addr;
                        }
                    }
                }
            }
        }
    }

    return 0;
}

/* ── Call init functions ── */
typedef void (*init_fn_t)(void);

static void dl_call_init(loaded_lib_t *lib) {
    /* Call DT_INIT if present */
    if (lib->init) {
        init_fn_t fn = (init_fn_t)lib->init;
        fn();
    }
    /* Call DT_INIT_ARRAY entries */
    if (lib->init_array && lib->init_arraysz) {
        unsigned long count = lib->init_arraysz / sizeof(void *);
        init_fn_t *arr = (init_fn_t *)lib->init_array;
        for (unsigned long i = 0; i < count; i++) {
            if (arr[i]) arr[i]();
        }
    }
}

/* ── Main dynamic linker entry ── */
/* Called from _start after clearing personality.
 * sp points to the initial stack (argc at [0]). */
void __attribute__((noreturn)) dl_main(unsigned long sp) {
    /* Parse auxv */
    auxv_entry_t *auxv = dl_parse_auxv((unsigned long *)sp);
    unsigned long at_phdr = 0;
    unsigned long at_entry = 0;
    unsigned long at_base = 0;
    unsigned long at_phent = 0;
    unsigned long at_phnum = 0;

    for (int i = 0; auxv[i].a_type != AT_NULL; i++) {
        switch (auxv[i].a_type) {
        case AT_PHDR:  at_phdr  = auxv[i].a_val; break;
        case AT_ENTRY: at_entry = auxv[i].a_val; break;
        case AT_BASE:  at_base  = auxv[i].a_val; break;
        case AT_PHENT: at_phent = auxv[i].a_val; break;
        case AT_PHNUM: at_phnum = auxv[i].a_val; break;
        }
    }

    /* If AT_BASE is 0, dynamic linker was loaded at a fixed address;
     * we still need to know our own base.  Use AT_PHDR - phoff trick
     * or fall back to computing from our own address. */
    if (at_base == 0) {
        /* Get our own address via a PC-relative call */
        unsigned long self;
        asm volatile("lea (%%rip), %0" : "=r"(self));
        /* Our base is page-aligned and below our text */
        self &= ~0xFFF;
        /* Search up to find the real base (page where our program headers start).
         * Simple: just use AT_PHDR from our own perspective — but AT_PHDR is
         * for the main binary, not us.  We'll compute from our entry point. */
        at_base = self;
    }

    /* Load libraries for the MAIN binary (pointed to by AT_PHDR) */
    unsigned long main_dyn = 0;
    unsigned long main_base = 0;
    unsigned long main_min_vaddr = at_phdr & ~0xFFF;

    /* Find the main binary's base by scanning pages below AT_PHDR */
    /* The main binary's first PT_LOAD starts at or below its phdr.
     * AT_PHDR points to the phdr in memory.  The binary's base is
     * (phdr_addr - phoff) but we don't have phoff in auxv.
     * We can find it by scanning PT_LOAD segments using at_phdr directly. */
    elf64_phdr_t *main_phdr = (elf64_phdr_t *)at_phdr;
    unsigned int main_phent = at_phent ? (unsigned int)at_phent : 56;
    unsigned int main_phnum = at_phnum ? (unsigned int)at_phnum : 0;

    /* Determine main binary's base: lowest vaddr of any PT_LOAD */
    main_base = ~0UL;
    for (unsigned int i = 0; i < main_phnum; i++) {
        elf64_phdr_t *ph = (elf64_phdr_t *)((unsigned char *)main_phdr + i * main_phent);
        if (ph->type == PT_LOAD && ph->vaddr < main_base) {
            main_base = ph->vaddr & ~0xFFF;
        }
        if (ph->type == PT_DYNAMIC) {
            main_dyn = ph->vaddr;
        }
    }
    if (main_base == ~0UL) main_base = main_min_vaddr;

    /* Read the main binary's dynamic section to find DT_NEEDED */
    if (main_dyn) {
        /* main_dyn is a vaddr from program headers.  The binary is already
         * mapped at that address. */
        unsigned long main_dyn_addr = main_dyn;

        /* Count DT_NEEDED entries first */
        elf64_dyn_t *dyn = (elf64_dyn_t *)main_dyn_addr;
        int needed_count = 0;
        while (dyn->d_tag != DT_NULL) {
            if (dyn->d_tag == DT_NEEDED) needed_count++;
            dyn++;
        }

        /* Load each needed library */
        if (needed_count > 0) {
            /* We need the main binary's string table to get library names */
            unsigned long main_strtab = 0;
            int main_strsz = 0;
            dyn = (elf64_dyn_t *)main_dyn_addr;
            while (dyn->d_tag != DT_NULL) {
                if (dyn->d_tag == DT_STRTAB) main_strtab = dyn->d_val;
                if (dyn->d_tag == DT_STRSZ)  main_strsz  = (int)dyn->d_val;
                dyn++;
            }

            /* Load each needed library */
            dyn = (elf64_dyn_t *)main_dyn_addr;
            while (dyn->d_tag != DT_NULL) {
                if (dyn->d_tag == DT_NEEDED) {
                    if (main_strtab && dyn->d_val < (unsigned long)main_strsz) {
                        const char *lib_name = (const char *)main_strtab + dyn->d_val;

                        if (lib_count < MAX_LIBS) {
                            if (dl_load_library(lib_name, &libs[lib_count])) {
                                lib_count++;
                            }
                        }
                    }
                }
                dyn++;
            }
        }
    }

    /* Process relocations for all loaded libraries */
    for (int i = 0; i < lib_count; i++) {
        dl_process_relocations(&libs[i]);
    }

    /* Call init functions for all loaded libraries */
    for (int i = 0; i < lib_count; i++) {
        dl_call_init(&libs[i]);
    }

    /* Restore Linux personality before jumping to main binary */
    dl_syscall1(SYS_SET_PERSONALITY, PERSONALITY_LINUX);

    /* Jump to the main binary's entry point */
    typedef void (*entry_fn_t)(void);
    entry_fn_t entry = (entry_fn_t)at_entry;

    /* Set up proper stack for the main binary
     * The stack still has argc/argv/envp/auxv.  The main binary
     * expects this layout.  We just need to set RSP correctly. */
    asm volatile("movq %0, %%rsp\n\t"
                 "jmp *%1\n\t"
                 :
                 : "r"(sp), "r"(entry)
                 : "memory");
    __builtin_unreachable();
}

/* ── _start entry point ──
 * Stack on entry:
 *   [sp+0] = argc
 *   [sp+8] = argv[0..argc-1], NULL
 *   [sp+...] = envp[], NULL
 *   [sp+...] = auxv[], AT_NULL, 0
 *
 * First thing: clear Linux personality so we can use CodeOS syscalls. */
void __attribute__((noreturn)) _start(void) {
    unsigned long sp;
    asm volatile("movq %%rsp, %0" : "=r"(sp));

    /* Clear Linux personality — this syscall works because the kernel
     * checks for SYSCALL_SET_PERSONALITY before personality translation. */
    dl_syscall1(SYS_SET_PERSONALITY, 0);

    dl_main(sp);
    __builtin_unreachable();
}

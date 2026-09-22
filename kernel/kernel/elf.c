#include "elf.h"
#include "pe_loader.h"
#include "pmm.h"
#include "kprintf.h"
#include "vmm.h"
#include "string.h"
#include "rng.h"
#include "fs.h"
#include "ext2.h"
#include "../arch/x86_64/io.h"

struct __attribute__((packed)) elf64_hdr {
    uint8_t  ident[16];
    uint16_t type;
    uint16_t machine;
    uint32_t version;
    uint64_t entry;
    uint64_t phoff;
    uint64_t shoff;
    uint32_t flags;
    uint16_t ehsize;
    uint16_t phentsize;
    uint16_t phnum;
    uint16_t shentsize;
    uint16_t shnum;
    uint16_t shstrndx;
};

struct __attribute__((packed)) elf64_phdr {
    uint32_t type;
    uint32_t flags;
    uint64_t offset;
    uint64_t vaddr;
    uint64_t paddr;
    uint64_t filesz;
    uint64_t memsz;
    uint64_t align;
};

/* ── Shebang support ── */
#define SHEBANG_MAX 256

/* Parse shebang line from buffer, extract interpreter path. */
/* Returns 0 if no shebang, 1 if shebang found with interpreter in out. */
static int parse_shebang(const char *buf, int buflen, char *interp, int interp_sz) {
    if (buflen < 2 || buf[0] != '#' || buf[1] != '!')
        return 0;
    int pos = 2;
    while (pos < buflen && (buf[pos] == ' ' || buf[pos] == '\t'))
        pos++;
    int i = 0;
    while (pos < buflen && buf[pos] != '\n' && buf[pos] != '\r' &&
           buf[pos] != ' ' && buf[pos] != '\t' && i < interp_sz - 1)
        interp[i++] = buf[pos++];
    interp[i] = 0;
    if (i == 0) return 0;
    return 1;
}

#define ELF_MAX_PAGES 16

/* ── Internal: map PT_LOAD segments for one ELF, return entry+phdr info ── */
/* Also checks for PT_INTERP; if found, stores path in interp_out. */
/* Returns 0 on success, -1 on error. */
static int elf_load_segments(const char *path, struct elf64_hdr *hdr,
                             uint8_t *ph_buf, int from_ramfs,
                             const char *ramfs_buf, int ramfs_len,
                             uint64_t *entry_out, elf_auxv_info_t *auxv,
                             char *interp_out) {
    uint64_t entry = hdr->entry;
    uint8_t *file_pages[ELF_MAX_PAGES];
    int page_count = 0;
    int file_len = 0;

    if (from_ramfs && ramfs_buf) {
        for (int p = 0; p < ELF_MAX_PAGES && p * 4096 < ramfs_len; p++) {
            file_pages[p] = (uint8_t*)pmm_alloc_page();
            if (!file_pages[p]) break;
            page_count++;
            int chunk = ramfs_len - p * 4096;
            if (chunk > 4096) chunk = 4096;
            memcpy((void*)phys_to_virt((uint64_t)file_pages[p]), ramfs_buf + p * 4096, chunk);
        }
        file_len = ramfs_len;
    } else {
        for (int p = 0; p < ELF_MAX_PAGES; p++) {
            file_pages[p] = (uint8_t*)pmm_alloc_page();
            if (!file_pages[p]) break;
            page_count++;
            int r = ext2_read_file_path(path, (void*)phys_to_virt((uint64_t)file_pages[p]), 4096);
            if (r <= 0) { pmm_free_page((uint64_t)file_pages[p]); page_count--; break; }
            if (r > file_len) file_len = p * 4096 + r;
            if (r < 4096) break;
        }
    }

    /* Check for PT_INTERP first */
    if (interp_out) interp_out[0] = 0;
    for (int i = 0; i < hdr->phnum; i++) {
        struct elf64_phdr *ph = (struct elf64_phdr *)(ph_buf + i * hdr->phentsize);
        if (ph->type == PT_INTERP && interp_out) {
            uint64_t off = ph->offset;
            if (off < (uint64_t)file_len) {
                int pi = off / 4096;
                int po = off % 4096;
                if (pi < page_count) {
                    const char *src = (const char*)phys_to_virt((uint64_t)file_pages[pi]) + po;
                    int k = 0;
                    while (k < SHEBANG_MAX - 1 && src[k] && src[k] != '\n') {
                        interp_out[k] = src[k]; k++;
                    }
                    interp_out[k] = 0;
                }
            }
        }
    }

    /* Track allocated pages for cleanup on error. Because adjacent PT_LOAD
     * segments routinely share the same 4KB page (e.g. a text segment ends
     * and a read-only data segment begins mid-page), the loader must map one
     * physical page per page-address and let each segment copy into its own
     * offsets, then apply the union of segment protections. */
    #define ELF_SEG_PAGES_MAX 512
    uint64_t map_pa[ELF_SEG_PAGES_MAX];
    uint64_t map_phys[ELF_SEG_PAGES_MAX];
    uint64_t map_prot[ELF_SEG_PAGES_MAX];
    int n_map_pages = 0;

    for (int i = 0; i < hdr->phnum; i++) {
        struct elf64_phdr *ph = (struct elf64_phdr *)(ph_buf + i * hdr->phentsize);
        if (ph->type != PT_LOAD) continue;

        uint64_t vaddr  = ph->vaddr;
        uint64_t memsz  = ph->memsz;
        uint64_t filesz = ph->filesz;
        uint64_t offset = ph->offset;

        if (vaddr + memsz > 0x7FFFFFFFFFFFULL || vaddr + memsz < vaddr) {
            for (int p = 0; p < n_map_pages; p++) pmm_free_page(map_phys[p]);
            for (int p = 0; p < page_count; p++) pmm_free_page((uint64_t)file_pages[p]);
            return -1;
        }

        uint64_t page_start = vaddr & ~0xFFF;
        uint64_t page_end   = (vaddr + memsz + 0xFFF) & ~0xFFF;

        uint64_t prot = PAGE_PRESENT | PAGE_USER;
        if (ph->flags & 2) prot |= PAGE_WRITE;
        if (!(ph->flags & 1)) prot |= PAGE_NX;

        /* Allocate a physical page per page-address if not already present
         * from an earlier PT_LOAD that overlaps; reuse otherwise. */
        for (uint64_t pa = page_start; pa < page_end; pa += 0x1000) {
            int m = 0;
            for (; m < n_map_pages; m++)
                if (map_pa[m] == pa) break;

            if (m == n_map_pages) {
                if (n_map_pages >= ELF_SEG_PAGES_MAX) {
                    for (int q = 0; q < n_map_pages; q++) pmm_free_page(map_phys[q]);
                    for (int q = 0; q < page_count; q++) pmm_free_page((uint64_t)file_pages[q]);
                    return -1;
                }
                uint64_t page_phys = pmm_alloc_page();
                if (!page_phys) {
                    for (int q = 0; q < n_map_pages; q++) pmm_free_page(map_phys[q]);
                    for (int q = 0; q < page_count; q++) pmm_free_page((uint64_t)file_pages[q]);
                    return -1;
                }
                map_pa[n_map_pages] = pa;
                map_phys[n_map_pages] = page_phys;
                map_prot[n_map_pages] = prot;
                n_map_pages++;
                memset((void*)phys_to_virt(page_phys), 0, 0x1000);
                /* Writable during the load copy; final (union) protection
                 * is applied once every segment has copied its content. */
                if (vmm_map_page(pa, page_phys, prot | PAGE_WRITE) < 0) {
                    pmm_free_page(page_phys);
                    n_map_pages--;
                    for (int q = 0; q < n_map_pages; q++) pmm_free_page(map_phys[q]);
                    for (int q = 0; q < page_count; q++) pmm_free_page((uint64_t)file_pages[q]);
                    return -1;
                }
            } else {
                map_prot[m] |= prot;
            }
        }

        uint64_t copy_size = filesz < memsz ? filesz : memsz;
        uint64_t j = 0;
        while (j < copy_size) {
            uint64_t file_off = offset + j;
            int page_idx = file_off / 4096;
            int page_off = file_off % 4096;
            if (page_idx >= page_count) break;
            uint64_t chunk = 4096 - page_off;
            if (chunk > copy_size - j) chunk = copy_size - j;
            smap_stac();
            memcpy((void *)(vaddr + j), (void*)(phys_to_virt((uint64_t)file_pages[page_idx]) + page_off), chunk);
            smap_clac();
            j += chunk;
        }
    }

    /* Apply final protections (union of all overlapping segment flags). */
    for (int m = 0; m < n_map_pages; m++)
        vmm_map_page(map_pa[m], map_phys[m], map_prot[m]);

    for (int p = 0; p < page_count; p++) pmm_free_page((uint64_t)file_pages[p]);

    /* Fill auxv info for this ELF */
    if (auxv) {
        auxv->phent = hdr->phentsize;
        auxv->phnum = hdr->phnum;
        auxv->entry = entry;
        uint64_t base_addr = 0;
        for (int i = 0; i < hdr->phnum; i++) {
            struct elf64_phdr *ph = (struct elf64_phdr *)(ph_buf + i * hdr->phentsize);
            if (ph->type == PT_LOAD) { base_addr = ph->vaddr & ~0xFFF; break; }
        }
        if (base_addr == 0) base_addr = 0x400000;
        auxv->phdr_addr = base_addr + hdr->phoff;
    }

    *entry_out = entry;
    return 0;
}

#define ELF_STACK_PAGES 32  /* Increased from 16 to 32 (128KB) for faster startup */

/* ── Allocate user stack ── */
static int elf_alloc_stack(uint64_t *stack_top_out) {
    int n_stack_pages = ELF_STACK_PAGES;
    uint64_t stack_pages[ELF_STACK_PAGES];
    for (int s = 0; s < n_stack_pages; s++) {
        stack_pages[s] = (uint64_t)pmm_alloc_page();
        if (!stack_pages[s]) {
            for (int f = 0; f < s; f++) pmm_free_page(stack_pages[f]);
            return -1;
        }
        memset((void*)phys_to_virt(stack_pages[s]), 0, 0x1000);
    }

    uint64_t stack_base = (0x7F000000 + (rng_next() % 0xFE00000)) & ~0xFFF;
    if (stack_base < 0x7F000000) stack_base = 0x7F000000;
    for (int s = 0; s < n_stack_pages; s++) {
        uint64_t va = stack_base + (uint64_t)s * 0x1000;
        if (vmm_map_page(va, stack_pages[s], PAGE_PRESENT | PAGE_WRITE | PAGE_USER) < 0) {
            for (int f = 0; f < n_stack_pages; f++) pmm_free_page(stack_pages[f]);
            return -1;
        }
    }
    *stack_top_out = stack_base + (uint64_t)n_stack_pages * 0x1000;
    return 0;
}

/* ── Recursion guard for shebang / PT_INTERP ── */
#define ELF_MAX_RECURSION 4

/* ── Internal ELF loader with recursion depth tracking ── */
static int elf_load_depth(const char *path, uint64_t *entry_out, uint64_t *stack_out, elf_auxv_info_t *auxv, int depth) {
    if (depth >= ELF_MAX_RECURSION) return -1;
    if (auxv) memset(auxv, 0, sizeof(*auxv));
    struct elf64_hdr hdr;
    int from_ramfs = 0;
    int ramfs_len = 0;
    char ramfs_buf[FS_CONTENT_MAX];

    int n = fs_read(path, ramfs_buf, FS_CONTENT_MAX);
    if (n > 0) {
        from_ramfs = 1;
        ramfs_len = n;
        /* Check shebang */
        char interp[SHEBANG_MAX];
        if (parse_shebang(ramfs_buf, ramfs_len, interp, sizeof(interp))) {
            return elf_load_depth(interp, entry_out, stack_out, auxv, depth + 1);
        }
        if (ramfs_len < (int)sizeof(hdr)) return -1;
        memcpy(&hdr, ramfs_buf, sizeof(hdr));
    } else {
        uint8_t first[256];
        n = ext2_read_file_path(path, first, sizeof(first));
        if (n <= 0) return -1;
        char interp[SHEBANG_MAX];
        if (parse_shebang((const char *)first, n, interp, sizeof(interp))) {
            return elf_load_depth(interp, entry_out, stack_out, auxv, depth + 1);
        }
        if (n < (int)sizeof(hdr)) return -1;
        memcpy(&hdr, first, sizeof(hdr));
    }

    /* PE/COFF detection — redirect to Wine loader */
    if (hdr.ident[0] == 'M' && hdr.ident[1] == 'Z') {
        kprintf("wine: detected PE binary: %s\n", path);
        uint64_t result = pe_load_executable(path, entry_out, stack_out, NULL);
        return (result == (uint64_t)-1) ? -1 : 0;
    }

    if (hdr.ident[0] != 0x7F || hdr.ident[1] != 'E' ||
        hdr.ident[2] != 'L'  || hdr.ident[3] != 'F') {
        return -1;
    }
    if (hdr.ident[4] != 2) return -1;
    if (hdr.machine != 0x3E) return -1;

    /* Read program headers */
    uint64_t ph_end = hdr.phoff + (uint64_t)hdr.phnum * hdr.phentsize;
    if (ph_end > 4096) { kprintf("elf: too many program headers\n"); return -1; }
    uint8_t ph_buf[4096];

    if (from_ramfs) {
        if (ph_end > (uint64_t)ramfs_len) return -1;
        memcpy(ph_buf, ramfs_buf + hdr.phoff, ph_end - hdr.phoff);
    } else {
        if (ext2_read_file_path(path, ph_buf, ph_end) < (int)ph_end) {
            return -1;
        }
    }

    /* Map main binary's segments and check for PT_INTERP */
    uint64_t main_entry;
    elf_auxv_info_t main_auxv;
    char interp_path[SHEBANG_MAX];
    memset(&main_auxv, 0, sizeof(main_auxv));

    if (elf_load_segments(path, &hdr, ph_buf, from_ramfs, ramfs_buf, ramfs_len,
                          &main_entry, &main_auxv, interp_path) < 0)
        return -1;

    /* If PT_INTERP, load the interpreter instead */
    if (interp_path[0]) {
        uint64_t interp_entry;
        uint64_t interp_stack;
        elf_auxv_info_t interp_auxv;
        memset(&interp_auxv, 0, sizeof(interp_auxv));

        /* Load interpreter (its segments get mapped, also checks for PT_INTERP recursively) */
        if (elf_load_depth(interp_path, &interp_entry, &interp_stack, &interp_auxv, depth + 1) < 0) {
            return -1;
        }

        /* Return interpreter's entry/stack, but with main binary's auxv info */
        *entry_out = interp_entry;
        *stack_out = interp_stack;
        if (auxv) {
            auxv->phdr_addr    = main_auxv.phdr_addr;
            auxv->phent        = main_auxv.phent;
            auxv->phnum        = main_auxv.phnum;
            auxv->entry        = main_entry;
            auxv->interp_base  = interp_auxv.phdr_addr & ~0xFFF;
            auxv->interp_entry = interp_entry;
        }
        return 0;
    }

    /* No interpreter: allocate stack and return */
    uint64_t stack_top;
    if (elf_alloc_stack(&stack_top) < 0) return -1;

    *entry_out = main_entry;
    *stack_out = stack_top;
    if (auxv) {
        auxv->phdr_addr = main_auxv.phdr_addr;
        auxv->phent     = main_auxv.phent;
        auxv->phnum     = main_auxv.phnum;
        auxv->entry     = main_entry;
    }

    /* Loaded successfully */
    return 0;
}

/* ── High-level ELF loader ── */
int elf_load(const char *path, uint64_t *entry_out, uint64_t *stack_out, elf_auxv_info_t *auxv) {
    return elf_load_depth(path, entry_out, stack_out, auxv, 0);
}

/* ─── Push one string onto the growing-down stack; updates *sp ─── */
static void push_str(uint64_t stack_top, int *sp, const char *s) {
    int len = strlen(s) + 1;
    *sp -= len;
    *sp &= ~0xF;
    memcpy((void *)(stack_top + *sp), s, len);
}

/* ─── Set up Linux ABI stack: argc, argv[], envp[], auxv[], strings ─── */
uint64_t elf_setup_stack(uint64_t stack_top, uint64_t entry,
                          int argc, char **argv, int envc, char **envp,
                          elf_auxv_info_t *auxv) {
    int sp = 0;
    uint64_t argv_ptrs[64];
    uint64_t envp_ptrs[64];
    if (argc > 64) argc = 64;
    if (envc > 64) envc = 64;

    /* 1. Push environment strings */
    for (int i = envc - 1; i >= 0; i--) {
        push_str(stack_top, &sp, envp[i]);
        envp_ptrs[i] = stack_top + sp;
    }

    /* 2. Push argument strings */
    for (int i = argc - 1; i >= 0; i--) {
        push_str(stack_top, &sp, argv[i]);
        argv_ptrs[i] = stack_top + sp;
    }

    /* 3. Align to 16 bytes */
    sp &= ~0xF;

    /* 4. Push 16 random bytes for AT_RANDOM */
    sp -= 16;
    sp &= ~0xF;
    {
        uint8_t random_bytes[16];
        for (int i = 0; i < 16; i++) random_bytes[i] = (uint8_t)rng_next();
        memcpy((void*)(stack_top + sp), random_bytes, 16);
    }
    uint64_t random_addr = stack_top + sp;

    /* 5. Build auxv array */
    #define AUXV_MAX 32
    uint64_t auxv_pairs[AUXV_MAX * 2];
    int ac = 0;
    #define AUXV_ADD(t, v) do { if (ac < AUXV_MAX) { auxv_pairs[ac*2] = (t); auxv_pairs[ac*2+1] = (v); ac++; } } while(0)

    AUXV_ADD(AT_PHDR,    auxv ? auxv->phdr_addr : 0);
    AUXV_ADD(AT_PHENT,   auxv ? auxv->phent : 64);
    AUXV_ADD(AT_PHNUM,   auxv ? auxv->phnum : 0);
    AUXV_ADD(AT_PAGESZ,  4096);
    AUXV_ADD(AT_BASE,    (auxv && auxv->interp_base) ? auxv->interp_base : 0);
    AUXV_ADD(AT_FLAGS,   0);
    AUXV_ADD(AT_ENTRY,   (auxv && auxv->entry) ? auxv->entry : entry);
    AUXV_ADD(AT_UID,    0);
    AUXV_ADD(AT_EUID,   0);
    AUXV_ADD(AT_GID,    0);
    AUXV_ADD(AT_EGID,   0);
    AUXV_ADD(AT_SECURE, 0);
    AUXV_ADD(AT_RANDOM, random_addr);
    AUXV_ADD(AT_HWCAP,  0);
    AUXV_ADD(AT_CLKTCK, 100);
    AUXV_ADD(AT_NULL,   0);
    #undef AUXV_ADD

    /* Push auxv entries (reverse order) */
    for (int i = ac - 1; i >= 0; i--) {
        sp -= 16;
        *(uint64_t*)(stack_top + sp)     = auxv_pairs[i*2];
        *(uint64_t*)(stack_top + sp + 8) = auxv_pairs[i*2+1];
    }

    /* 6. NULL end-of-envp */
    sp -= 8;
    *(uint64_t*)(stack_top + sp) = 0;

    /* 7. envp pointers (reverse) */
    for (int i = envc - 1; i >= 0; i--) {
        sp -= 8;
        *(uint64_t*)(stack_top + sp) = envp_ptrs[i];
    }

    /* 8. NULL end-of-argv */
    sp -= 8;
    *(uint64_t*)(stack_top + sp) = 0;

    /* 9. argv pointers (reverse) */
    for (int i = argc - 1; i >= 0; i--) {
        sp -= 8;
        *(uint64_t*)(stack_top + sp) = argv_ptrs[i];
    }

    /* 10. argc */
    sp -= 8;
    *(uint64_t*)(stack_top + sp) = (uint64_t)argc;

    return stack_top + sp;
}

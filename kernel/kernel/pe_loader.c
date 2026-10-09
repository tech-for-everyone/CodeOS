#include "pe_loader.h"
#include "wine_stubs.h"
#include "pmm.h"
#include "vmm.h"
#include "kprintf.h"
#include "string.h"
#include "fs.h"
#include "ext2.h"
#include "rng.h"
#include "../arch/x86_64/io.h"

/* ── Validate PE file ── */
int pe_is_valid(const void *data, int len) {
    if (len < 64) return 0;
    const uint8_t *p = (const uint8_t *)data;
    if (p[0] != 'M' || p[1] != 'Z') return 0;
    uint32_t pe_off = *(uint32_t *)(p + 60);
    if (pe_off + 4 > (uint32_t)len) return 0;
    if (p[pe_off] != 'P' || p[pe_off+1] != 'E' ||
        p[pe_off+2] != 0 || p[pe_off+3] != 0) return 0;
    return 1;
}

/* ── Read entire PE file into pages (same as ELF loader) ── */
#define PE_MAX_PAGES 64

static int pe_read_file(const char *path, uint8_t **pages_out, int *page_count_out, int *file_len_out) {
    uint8_t *file_pages[PE_MAX_PAGES];
    int page_count = 0;
    int file_len = 0;

    char ramfs_buf[FS_CONTENT_MAX];
    int n = fs_read(path, ramfs_buf, FS_CONTENT_MAX);
    if (n > 0) {
        for (int p = 0; p < PE_MAX_PAGES && p * 4096 < n; p++) {
            file_pages[p] = (uint8_t *)pmm_alloc_page();
            if (!file_pages[p]) break;
            page_count++;
            int chunk = n - p * 4096;
            if (chunk > 4096) chunk = 4096;
            memcpy((void *)phys_to_virt((uint64_t)file_pages[p]), ramfs_buf + p * 4096, chunk);
        }
        file_len = n;
    } else {
        for (int p = 0; p < PE_MAX_PAGES; p++) {
            file_pages[p] = (uint8_t *)pmm_alloc_page();
            if (!file_pages[p]) break;
            page_count++;
            int r = ext2_read_file_path(path, (void *)phys_to_virt((uint64_t)file_pages[p]), 4096);
            if (r <= 0) { pmm_free_page((uint64_t)file_pages[p]); page_count--; break; }
            if (r > file_len) file_len = p * 4096 + r;
            if (r < 4096) break;
        }
    }

    if (page_count == 0) return -1;

    *pages_out = (uint8_t *)pmm_alloc_page();
    if (!*pages_out) {
        for (int p = 0; p < page_count; p++) pmm_free_page((uint64_t)file_pages[p]);
        return -1;
    }

    /* Flatten into contiguous kernel buffer */
    uint8_t *kbuf = (uint8_t *)phys_to_virt((uint64_t)*pages_out);
    int copied = 0;
    for (int p = 0; p < page_count; p++) {
        int chunk = file_len - copied;
        if (chunk > 4096) chunk = 4096;
        if (chunk <= 0) break;
        memcpy(kbuf + copied, (void *)phys_to_virt((uint64_t)file_pages[p]), chunk);
        copied += chunk;
    }
    for (int p = 0; p < page_count; p++) pmm_free_page((uint64_t)file_pages[p]);

    *page_count_out = (file_len + 4095) / 4096;
    *file_len_out = file_len;
    return 0;
}

/* ── Parse PE headers ── */
static int pe_parse_headers(const uint8_t *buf, int file_len,
                            struct pe_coff_header *coff,
                            struct pe_optional_header64 *opt,
                            struct pe_section_header *sections,
                            int *num_sections) {
    if (file_len < 64) return -1;

    uint32_t pe_off = *(uint32_t *)(buf + 60);
    if (pe_off + 24 > (uint32_t)file_len) return -1;

    /* Verify PE signature */
    if (buf[pe_off] != 'P' || buf[pe_off+1] != 'E' ||
        buf[pe_off+2] != 0 || buf[pe_off+3] != 0) return -1;

    memcpy(coff, buf + pe_off + 4, sizeof(*coff));

    if (coff->machine != PE_MACHINE_AMD64) return -1;

    int opt_off = pe_off + 4 + sizeof(struct pe_coff_header);
    if (opt_off + coff->opt_header_size > file_len) return -1;
    memcpy(opt, buf + opt_off, sizeof(*opt));

    if (opt->magic != 0x20B) return -1;  /* PE32+ only */

    int sec_off = opt_off + coff->opt_header_size;
    *num_sections = coff->num_sections;
    if (*num_sections > PE_MAX_SECTIONS) *num_sections = PE_MAX_SECTIONS;
    int sec_size = *num_sections * sizeof(struct pe_section_header);
    if (sec_off + sec_size > file_len) return -1;
    memcpy(sections, buf + sec_off, sec_size);

    return 0;
}

/* ── Map PE sections into user address space ── */
static int pe_map_sections(uint64_t image_base, uint32_t section_align,
                           struct pe_section_header *sections, int num_sections,
                           const uint8_t *file_buf, int file_len) {
    uint64_t seg_pages[PE_MAX_SECTIONS * 16];
    int n_seg_pages = 0;

    for (int i = 0; i < num_sections; i++) {
        struct pe_section_header *sec = &sections[i];
        if (sec->virtual_size == 0) continue;

        uint64_t vaddr = image_base + sec->virtual_addr;
        uint64_t memsz = sec->virtual_size;
        uint64_t filesz = sec->size_of_raw_data;

        if (vaddr + memsz > 0x7FFFFFFFFFFFULL) {
            for (int p = 0; p < n_seg_pages; p++) pmm_free_page(seg_pages[p]);
            return -1;
        }

        uint64_t page_start = vaddr & ~0xFFFULL;
        uint64_t page_end = (vaddr + memsz + 0xFFF) & ~0xFFFULL;

        /* Determine page protection from section flags */
        uint64_t prot = PAGE_PRESENT | PAGE_USER;
        if (sec->characteristics & PE_SEC_MEM_WRITE) prot |= PAGE_WRITE;
        if (!(sec->characteristics & PE_SEC_MEM_EXEC)) prot |= PAGE_NX;

        /* Map pages */
        for (uint64_t pa = page_start; pa < page_end; pa += 0x1000) {
            uint64_t page_phys = pmm_alloc_page();
            if (!page_phys) {
                for (int p = 0; p < n_seg_pages; p++) pmm_free_page(seg_pages[p]);
                return -1;
            }
            seg_pages[n_seg_pages++] = page_phys;
            memset((void *)phys_to_virt(page_phys), 0, 0x1000);
            if (vmm_map_page(pa, page_phys, prot) < 0) {
                pmm_free_page(page_phys);
                n_seg_pages--;
                for (int p = 0; p < n_seg_pages; p++) pmm_free_page(seg_pages[p]);
                return -1;
            }
        }

        /* Copy section data from file buffer */
        uint64_t copy_size = filesz < memsz ? filesz : memsz;
        if (copy_size > 0 && sec->pointer_to_raw_data + copy_size <= (uint64_t)file_len) {
            uint64_t j = 0;
            while (j < copy_size) {
                smap_stac();
                memcpy((void *)(vaddr + j), file_buf + sec->pointer_to_raw_data + j,
                       copy_size - j);
                smap_clac();
                j += copy_size;
            }
        }
    }

    return 0;
}

/* ── Apply base relocations ── */
int pe_apply_relocations(uint64_t base, uint64_t desired_base, uint64_t size) {
    (void)size;
    uint64_t delta = desired_base - base;
    if (delta == 0) return 0;

    /* Scan relocation directory — we need the PE headers in kernel memory.
     * Since sections are already mapped, we just need to iterate. For now
     * we return 0 (no relocations needed if image loaded at preferred base). */
    (void)delta;
    return 0;
}

/* ── Import stub (triggers #UD, caught by syscall handler) ── */
void pe_import_stub(void) {
    asm volatile("ud2");
}

/* ── Resolve imports ── */
static int pe_resolve_imports(uint64_t image_base,
                              struct pe_optional_header64 *opt,
                              const uint8_t *file_buf, int file_len) {
    if (opt->num_rva_and_sizes <= PE_DD_IMPORT) return 0;

    struct pe_data_directory *imp_dd = &opt->data_directories[PE_DD_IMPORT];
    if (imp_dd->virtual_addr == 0 || imp_dd->size == 0) return 0;

    uint64_t imp_rva = imp_dd->virtual_addr;
    uint32_t imp_size = imp_dd->size;

    /* Walk import descriptors */
    uint32_t desc_offset = 0;
    while (desc_offset + sizeof(struct pe_import_descriptor) <= imp_size) {
        struct pe_import_descriptor desc;
        uint64_t desc_va = image_base + imp_rva + desc_offset;
        smap_stac();
        memcpy(&desc, (void *)desc_va, sizeof(desc));
        smap_clac();

        if (desc.name_rva == 0) break;  /* End of imports */

        /* Get DLL name */
        char dll_name[PE_MAX_DLL_NAME];
        uint64_t name_va = image_base + desc.name_rva;
        smap_stac();
        int k = 0;
        const char *src = (const char *)name_va;
        while (k < PE_MAX_DLL_NAME - 1 && src[k]) {
            dll_name[k] = src[k]; k++;
        }
        dll_name[k] = 0;
        smap_clac();

        kprintf("wine: import DLL: %s\n", dll_name);

        /* Walk Import Name Table (INT) or Import Address Table (IAT) */
        uint64_t thunk_rva = desc.original_first_thunk ? desc.original_first_thunk : desc.first_thunk;
        uint64_t iat_rva = desc.first_thunk;

        uint32_t thunk_idx = 0;
        while (1) {
            uint64_t thunk_va = image_base + thunk_rva + thunk_idx * 8;
            uint64_t iat_va = image_base + iat_rva + thunk_idx * 8;

            uint64_t thunk_val;
            smap_stac();
            memcpy(&thunk_val, (void *)thunk_va, 8);
            smap_clac();

            if (thunk_val == 0) break;  /* End of thunks */

            const char *func_name = NULL;
            uint16_t ordinal = 0;

            if (thunk_val & 0x8000000000000000ULL) {
                /* Import by ordinal */
                ordinal = (uint16_t)(thunk_val & 0xFFFF);
                kprintf("wine:   import by ordinal %d\n", ordinal);
            } else {
                /* Import by name */
                uint64_t hint_va = image_base + thunk_val;
                smap_stac();
                struct pe_import_by_name *ibn = (struct pe_import_by_name *)hint_va;
                func_name = ibn->name;
                smap_clac();
                kprintf("wine:   import: %s\n", func_name);
            }

            /* TODO: Resolve function address via wineserver or DLL lookup.
             * For now, write a stub (0xDEAD...) so the IAT is populated. */
            uint64_t resolved = 0;
            if (func_name) {
                /* Check if it's a known Win32 stub */
                resolved = wine_resolve_import(dll_name, func_name);
            }
            if (!resolved && ordinal) {
                resolved = wine_resolve_import_ordinal(dll_name, ordinal);
            }
            if (!resolved) {
                /* Placeholder stub — will trap if called */
                resolved = (uint64_t)&pe_import_stub;
            }

            smap_stac();
            memcpy((void *)iat_va, &resolved, 8);
            smap_clac();

            thunk_idx++;
        }

        desc_offset += sizeof(struct pe_import_descriptor);
    }

    return 0;
}

/* ── Allocate user stack (same pattern as ELF loader) ── */
static int pe_alloc_stack(uint64_t *stack_top_out) {
    uint64_t stack_pages[PE_STACK_PAGES];
    for (int s = 0; s < PE_STACK_PAGES; s++) {
        stack_pages[s] = (uint64_t)pmm_alloc_page();
        if (!stack_pages[s]) {
            for (int f = 0; f < s; f++) pmm_free_page(stack_pages[f]);
            return -1;
        }
        memset((void *)phys_to_virt(stack_pages[s]), 0, 0x1000);
    }

    uint64_t stack_base = (0x7F000000 + (rng_next() % 0xFE00000)) & ~0xFFFULL;
    if (stack_base < 0x7F000000) stack_base = 0x7F000000;
    for (int s = 0; s < PE_STACK_PAGES; s++) {
        uint64_t va = stack_base + (uint64_t)s * 0x1000;
        if (vmm_map_page(va, stack_pages[s], PAGE_PRESENT | PAGE_WRITE | PAGE_USER) < 0) {
            for (int f = 0; f < PE_STACK_PAGES; f++) pmm_free_page(stack_pages[f]);
            return -1;
        }
    }
    *stack_top_out = stack_base + (uint64_t)PE_STACK_PAGES * 0x1000;
    return 0;
}

/* ── High-level PE loader ── */
uint64_t pe_load_executable(const char *path, uint64_t *entry_out,
                            uint64_t *stack_out, wine_process_info_t *info) {
    uint8_t *file_buf_page;
    int page_count, file_len;

    if (pe_read_file(path, &file_buf_page, &page_count, &file_len) < 0) {
        kprintf("pe: failed to read %s\n", path);
        return (uint64_t)-1;
    }

    uint8_t *buf = (uint8_t *)phys_to_virt((uint64_t)file_buf_page);

    /* Parse headers */
    struct pe_coff_header coff;
    struct pe_optional_header64 opt;
    struct pe_section_header sections[PE_MAX_SECTIONS];
    int num_sections;

    if (pe_parse_headers(buf, file_len, &coff, &opt, sections, &num_sections) < 0) {
        kprintf("pe: invalid PE headers in %s\n", path);
        pmm_free_page((uint64_t)file_buf_page);
        return (uint64_t)-1;
    }

    kprintf("pe: loaded %s — entry=0x%x, image_base=0x%lx, subsystem=%d\n",
            path, opt.address_of_entry, opt.image_base, opt.subsystem);

    uint64_t image_base = opt.image_base;
    uint64_t image_size = opt.size_of_image;

    /* Check if image base is already in use; if so, relocate */
    /* For Wine, we'll use a randomized base to avoid conflicts */
    if (image_base == 0x140000000ULL || image_base < 0x10000ULL) {
        /* Default Windows base — relocate to user range */
        image_base = 0x10000000ULL + (rng_next() % 0x0F000000ULL) & ~0xFFFFFULL;
    }

    /* Map PE sections */
    if (pe_map_sections(image_base, opt.section_align, sections, num_sections, buf, file_len) < 0) {
        kprintf("pe: failed to map sections for %s\n", path);
        pmm_free_page((uint64_t)file_buf_page);
        return (uint64_t)-1;
    }

    /* Apply relocations if needed */
    pe_apply_relocations(opt.image_base, image_base, image_size);

    /* Resolve imports */
    if (pe_resolve_imports(image_base, &opt, buf, file_len) < 0) {
        kprintf("pe: failed to resolve imports for %s\n", path);
    }

    /* Free file pages */
    pmm_free_page((uint64_t)file_buf_page);

    /* Calculate entry point in mapped image */
    uint64_t entry = image_base + opt.address_of_entry;

    /* Allocate stack */
    uint64_t stack_top;
    if (pe_alloc_stack(&stack_top) < 0) return (uint64_t)-1;

    /* Fill wine process info */
    if (info) {
        info->wine_stack_top = stack_top;
        info->wine_pid = 0;
    }

    *entry_out = entry;
    *stack_out = stack_top;

    kprintf("pe: entry=0x%lx, stack=0x%lx, image=0x%lx+0x%lx\n",
            entry, stack_top, image_base, image_size);

    return image_base;
}

/* ── Load a DLL (for runtime loading) ── */
uint64_t pe_load_library(const char *path) {
    uint8_t *file_buf_page;
    int page_count, file_len;

    if (pe_read_file(path, &file_buf_page, &page_count, &file_len) < 0)
        return 0;

    uint8_t *buf = (uint8_t *)phys_to_virt((uint64_t)file_buf_page);

    struct pe_coff_header coff;
    struct pe_optional_header64 opt;
    struct pe_section_header sections[PE_MAX_SECTIONS];
    int num_sections;

    if (pe_parse_headers(buf, file_len, &coff, &opt, sections, &num_sections) < 0) {
        pmm_free_page((uint64_t)file_buf_page);
        return 0;
    }

    uint64_t image_base = opt.image_base;
    if (image_base == 0x140000000ULL || image_base < 0x10000ULL) {
        image_base = 0x20000000ULL + (rng_next() % 0x0F000000ULL) & ~0xFFFFFULL;
    }

    if (pe_map_sections(image_base, opt.section_align, sections, num_sections, buf, file_len) < 0) {
        pmm_free_page((uint64_t)file_buf_page);
        return 0;
    }

    pe_apply_relocations(opt.image_base, image_base, opt.size_of_image);
    pe_resolve_imports(image_base, &opt, buf, file_len);

    pmm_free_page((uint64_t)file_buf_page);
    return image_base;
}

/* ── Export lookup ── */
uint64_t pe_get_export(uint64_t base, const char *name) {
    /* Read PE headers from mapped image */
    uint16_t mz;
    smap_stac();
    memcpy(&mz, (void *)base, 2);
    smap_clac();
    if (mz != PE_MZ_MAGIC) return 0;

    uint32_t pe_off;
    smap_stac();
    memcpy(&pe_off, (void *)(base + 60), 4);
    smap_clac();

    struct pe_coff_header coff;
    smap_stac();
    memcpy(&coff, (void *)(base + pe_off + 4), sizeof(coff));
    smap_clac();

    struct pe_optional_header64 opt;
    smap_stac();
    memcpy(&opt, (void *)(base + pe_off + 4 + sizeof(coff)), sizeof(opt));
    smap_clac();

    if (opt.num_rva_and_sizes <= PE_DD_EXPORT) return 0;
    struct pe_data_directory *exp_dd = &opt.data_directories[PE_DD_EXPORT];
    if (exp_dd->virtual_addr == 0) return 0;

    struct pe_export_directory exp_dir;
    smap_stac();
    memcpy(&exp_dir, (void *)(base + exp_dd->virtual_addr), sizeof(exp_dir));
    smap_clac();

    /* Search by name */
    uint32_t *name_rvas = (uint32_t *)(base + exp_dir.addr_of_names);
    uint16_t *ordinals = (uint16_t *)(base + exp_dir.addr_of_ordinals);
    uint32_t *func_rvas = (uint32_t *)(base + exp_dir.addr_of_functions);

    for (uint32_t i = 0; i < exp_dir.num_names; i++) {
        const char *export_name;
        smap_stac();
        export_name = (const char *)(base + name_rvas[i]);
        smap_clac();

        int match = 1;
        for (int j = 0; name[j] && export_name[j]; j++) {
            if (name[j] != export_name[j]) { match = 0; break; }
        }
        if (match && name[0] == export_name[0]) {
            uint16_t ord = ordinals[i];
            uint32_t func_rva = func_rvas[ord];
            return base + func_rva;
        }
    }
    return 0;
}

uint64_t pe_get_export_ordinal(uint64_t base, uint16_t ordinal) {
    uint16_t mz;
    smap_stac();
    memcpy(&mz, (void *)base, 2);
    smap_clac();
    if (mz != PE_MZ_MAGIC) return 0;

    uint32_t pe_off;
    smap_stac();
    memcpy(&pe_off, (void *)(base + 60), 4);
    smap_clac();

    struct pe_coff_header coff;
    smap_stac();
    memcpy(&coff, (void *)(base + pe_off + 4), sizeof(coff));
    smap_clac();

    struct pe_optional_header64 opt;
    smap_stac();
    memcpy(&opt, (void *)(base + pe_off + 4 + sizeof(coff)), sizeof(opt));
    smap_clac();

    if (opt.num_rva_and_sizes <= PE_DD_EXPORT) return 0;
    struct pe_data_directory *exp_dd = &opt.data_directories[PE_DD_EXPORT];
    if (exp_dd->virtual_addr == 0) return 0;

    struct pe_export_directory exp_dir;
    smap_stac();
    memcpy(&exp_dir, (void *)(base + exp_dd->virtual_addr), sizeof(exp_dir));
    smap_clac();

    uint32_t *func_rvas = (uint32_t *)(base + exp_dir.addr_of_functions);
    uint16_t idx = ordinal - exp_dir.ordinal_base;
    if (idx >= exp_dir.num_functions) return 0;

    uint32_t func_rva = func_rvas[idx];
    if (func_rva == 0) return 0;
    return base + func_rva;
}

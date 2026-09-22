#ifndef PE_LOADER_H
#define PE_LOADER_H

#include "types.h"

/* PE magic */
#define PE_MZ_MAGIC      0x5A4D      /* "MZ" */
#define PE_SIGNATURE     0x00004550  /* "PE\0\0" */

/* Machine types */
#define PE_MACHINE_AMD64 0x8664

/* Section flags */
#define PE_SEC_CODE      0x00000020
#define PE_SEC_INIT_DATA 0x00000040
#define PE_SEC_UNINIT    0x00000080
#define PE_SEC_MEM_DISC  0x02000000
#define PE_SEC_MEM_READ  0x40000000
#define PE_SEC_MEM_WRITE 0x80000000
#define PE_SEC_MEM_EXEC  0x20000000

/* Data directory indices */
#define PE_DD_EXPORT     0
#define PE_DD_IMPORT     1
#define PE_DD_RESOURCE   2
#define PE_DD_EXCEPTION  3
#define PE_DD_SECURITY   4
#define PE_DD_BASERELOC  5
#define PE_DD_DEBUG      6
#define PE_DD_TLS        9
#define PE_DD_LOADCFG    10
#define PE_DD_BOUND_IAT  11
#define PE_DD_IAT        12

/* Relocation types */
#define PE_REL_BASED_ABSOLUTE  0
#define PE_REL_BASED_HIGHLOW   3
#define PE_REL_BASED_DIR64   10

/* Maximum limits */
#define PE_MAX_SECTIONS     96
#define PE_MAX_IMPORTS      256
#define PE_MAX_IMPORT_FUNCS 1024
#define PE_MAX_RELOC_PAGES  256
#define PE_STACK_PAGES      32
#define PE_MAX_DLL_NAME     256
#define PE_MAX_FUNC_NAME    256

/* ── Packed PE structures ── */

struct __attribute__((packed)) pe_dos_header {
    uint16_t e_magic;
    uint16_t e_cblp;
    uint16_t e_cp;
    uint16_t e_crlc;
    uint16_t e_cparhdr;
    uint16_t e_minalloc;
    uint16_t e_maxalloc;
    uint16_t e_ss;
    uint16_t e_sp;
    uint16_t e_csum;
    uint16_t e_ip;
    uint16_t e_cs;
    uint16_t e_lfarlc;
    uint16_t e_ovno;
    uint16_t e_res[4];
    uint16_t e_oemid;
    uint16_t e_oeminfo;
    uint16_t e_res2[10];
    uint32_t e_lfanew;
};

struct __attribute__((packed)) pe_coff_header {
    uint16_t machine;
    uint16_t num_sections;
    uint32_t timestamp;
    uint32_t symbol_table_ptr;
    uint32_t num_symbols;
    uint16_t opt_header_size;
    uint16_t characteristics;
};

struct __attribute__((packed)) pe_data_directory {
    uint32_t virtual_addr;
    uint32_t size;
};

struct __attribute__((packed)) pe_optional_header64 {
    uint16_t magic;
    uint8_t  linker_major;
    uint8_t  linker_minor;
    uint32_t size_of_code;
    uint32_t size_of_init_data;
    uint32_t size_of_uninit_data;
    uint32_t address_of_entry;
    uint32_t base_of_code;
    uint64_t image_base;
    uint32_t section_align;
    uint32_t file_align;
    uint16_t os_major;
    uint16_t os_minor;
    uint16_t image_major;
    uint16_t image_minor;
    uint16_t subsystem_major;
    uint16_t subsystem_minor;
    uint32_t win32_version;
    uint32_t size_of_image;
    uint32_t size_of_headers;
    uint32_t checksum;
    uint16_t subsystem;
    uint16_t dll_characteristics;
    uint64_t size_of_stack_reserve;
    uint64_t size_of_stack_commit;
    uint64_t size_of_heap_reserve;
    uint64_t size_of_heap_commit;
    uint32_t loader_flags;
    uint32_t num_rva_and_sizes;
    struct pe_data_directory data_directories[16];
};

struct __attribute__((packed)) pe_section_header {
    char     name[8];
    uint32_t virtual_size;
    uint32_t virtual_addr;
    uint32_t size_of_raw_data;
    uint32_t pointer_to_raw_data;
    uint32_t pointer_to_relocs;
    uint32_t pointer_to_linenumbers;
    uint16_t num_relocs;
    uint16_t num_linenumbers;
    uint32_t characteristics;
};

struct __attribute__((packed)) pe_import_descriptor {
    uint32_t original_first_thunk;
    uint32_t timestamp;
    uint32_t forwarder_chain;
    uint32_t name_rva;
    uint32_t first_thunk;
};

struct __attribute__((packed)) pe_import_by_name {
    uint16_t hint;
    char     name[1];
};

struct __attribute__((packed)) pe_thunk_data64 {
    uint64_t u1;
};

struct __attribute__((packed)) pe_base_reloc_block {
    uint32_t page_rva;
    uint32_t block_size;
};

struct __attribute__((packed)) pe_export_directory {
    uint32_t characteristics;
    uint32_t timestamp;
    uint16_t major_version;
    uint16_t minor_version;
    uint32_t name_rva;
    uint32_t ordinal_base;
    uint32_t num_functions;
    uint32_t num_names;
    uint32_t addr_of_functions;
    uint32_t addr_of_names;
    uint32_t addr_of_ordinals;
};

/* ── Loaded module info ── */

typedef struct pe_loaded_module {
    char name[PE_MAX_DLL_NAME];
    uint64_t base;
    uint64_t size;
    uint64_t entry;
    uint64_t image_base;
    struct pe_loaded_module *next;
} pe_loaded_module_t;

/* ── Wine process info (stored per-process) ── */

typedef struct {
    char prefix_path[256];
    uint64_t wine_stack_top;
    int wine_pid;
    int wine_server_fd;
} wine_process_info_t;

/* ── API ── */

/* Check if a buffer starts with MZ (PE file) */
int pe_is_valid(const void *data, int len);

/* Load a PE executable into the current process address space.
 * Returns entry point address, or -1 on error. */
uint64_t pe_load_executable(const char *path, uint64_t *entry_out,
                            uint64_t *stack_out, wine_process_info_t *info);

/* Load a PE DLL (shared library).
 * Returns base address of loaded DLL, or 0 on error. */
uint64_t pe_load_library(const char *path);

/* Get address of an exported function by name.
 * Returns function address, or 0 if not found. */
uint64_t pe_get_export(uint64_t base, const char *name);

/* Get address of an exported function by ordinal.
 * Returns function address, or 0 if not found. */
uint64_t pe_get_export_ordinal(uint64_t base, uint16_t ordinal);

/* Apply base relocations to a loaded PE image */
int pe_apply_relocations(uint64_t base, uint64_t desired_base, uint64_t size);

#endif

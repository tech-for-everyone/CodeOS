#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifndef __KERNEL_H
#define __KERNEL_H

#define print(message) print_character(message)
#ifndef set_csrs
#if defined(__RISCV__) || defined(__riscv)
#define set_csrs(reg, bit) asm volatile("csrs " #reg ", %0" :: "r"(bit))
#else
#define set_csrs(reg, bit) ((void)0)
#endif
#endif

extern uintptr_t mtime;
extern uintptr_t mtimemcp;
typedef void (*handler_t)(void);
extern handler_t interrupt_handlers[16];
extern handler_t exception_handlers[16];

extern void trap_vector();
struct trap_frame;
extern struct trap_frame *trap_handler(struct trap_frame *frame);
extern void init_traps();

extern void init_memory(void);
extern void init_virtual_memory(void);

extern void init_mtimecmp(void);
extern void set_mtimecmp(void);

extern void unknown_error(void);
extern void handle_illegal_instruction(void);
extern void handle_breakpoint(void);
extern void handle_load_access_error(void);
extern void handle_store_access_error(void);
extern void handle_clock_error(void);
extern void handle_load_page_error(void);
extern void handle_store_page_error(void);
extern void handle_instruction_page_error(void);

extern void fatal(char *reason);
extern void debug(char *reason);
extern void success(char *reason);
extern void info(char *reason);
extern void error(char *reason);
extern void fataln(char *reason);
extern void debugn(char *reason);
extern void successn(char *reason);
extern void infon(char *reason);
extern void errorn(char *reason);
extern void printn(char *message);
extern void start_screen(void);
extern void clear_screen();
extern void print_character(char *message);

extern void print_asccii_art(void);
struct ext2_dir_entry;
extern bool dir_entry_matches(const struct ext2_dir_entry *entry, const char *name, uint32_t name_len);
extern char *concat(char *hardcoded_text, char *reason);
extern void *memcpy(void *dest, const void *src, size_t n);
extern int strcmp(const char *s1, const char *s2);
extern size_t strlen(const char *s);
extern void *memset(void *dest, int val, size_t len);
extern size_t parse_number(char *str);
extern uint32_t round_page(uint32_t number, uint32_t page);
extern void print_hex(uint32_t val);
extern void print_dec(uint32_t val);

extern char *get_input(void);
extern char get_char(void);
extern bool test_keyboard(void);
extern void print_input(void);

extern void init_fs(void);
extern void init_ext2(void);
extern int virtio_write_sector(uint64_t sector, void *buffer);
extern int virtio_read_sector(uint64_t sector, void *buffer);
extern int virtio_blk_transfer(uint64_t sector, void *buffer, bool is_write);
extern uint32_t probe_mmio();
extern void ext2_read_block(uint32_t block, void *buffer);
extern void ext2_write_block(uint32_t block, void *buffer);
struct ext2_inode;
extern uint32_t ext2_read_inode(uint32_t inode_no, struct ext2_inode *out);
extern uint32_t ext2_write_inode(uint32_t inode_no, const struct ext2_inode *in);
extern uint32_t ext2_lookup(uint32_t dir_inode_no, const char *name);
extern uint32_t ext2_resolve_path(const char *path);
typedef void (*ext2_dir_cb)(const char *name, uint32_t name_len, uint32_t inode, uint8_t file_type);
extern int ext2_iterate_dir(const char *path, ext2_dir_cb callback);
extern int ext2_list_dir(const char *path);
extern int ext2_read(const char *path, void *buffer, uint32_t count, uint32_t offset);
extern int ext2_write(const char *path, const void *buffer, uint32_t count, uint32_t offset);
extern int ext2_update(const char *path, uint32_t mode, uint32_t mtime);
extern int ext2_remove(const char *path);
extern uint32_t ext2_create_file(const char *path);
extern uint32_t ext2_mkdir(const char *path);
extern void kmv(char *origin, char *destiny);
extern int kcp(const char *origin, const char *destiny);
extern void ktouch(char *path);
extern int kmkdir(const char *path);

extern void create_dirs();
extern void create_slot_dirs();
extern void create_update_process(void);

extern void shutdown();
extern void kreboot();
extern void reboot();

extern void test_call();
extern void _enteruserspace(uintptr_t user_stack_top);

extern void divide_memory(uintptr_t start, uintptr_t end);
extern uintptr_t allocate_memory();
extern void free_memory(uintptr_t *current_full_page);

extern void test_user_program(void);
extern void userspace_init(void);

extern void create_virtio_process(void);
extern void create_screen_process(void);
extern void create_keyboard_process(void);
extern void create_antartica_process(void);
extern void init_modules(void);

extern bool start_update(const char *update_file);
extern char get_active_slot(void);
extern void flip_active_slot(void);
extern bool get_slot_status(void);
extern void init_slot_status(void);
extern void mark_slot_successful(void);

extern int memcmp(const void *str1, const void *str2, size_t count);

extern void boot_neo(void *mailbox_addr,
                     uintptr_t kernel_base, uintptr_t kernel_len, const uint8_t *expected_kernel_hash);
                     
enum module_status {
   MODULE_STATUS_READY = 0,
   MODULE_STATUS_LOADING,
   MODULE_STATUS_NOT_LOADED,
   MODULE_STATUS_ERROR
};

struct module {
   char *name;
   char *author;
   char *version;
   char *description;
   enum module_status status;
   uint32_t id;
   uint32_t secid;
   bool is_kernel;
   bool is_priv;
   uint32_t sid;
   uint32_t gid;
   uint32_t is_hw_access;
   bool is_temporal;
};

struct kernel_module {
   const char *name;
   uint32_t gid;
   uint32_t flags;
   int (*init)(void);
   void (*exit)(void);
   void *symbols;
   size_t symbol_count;
   bool is_loaded;
   bool is_userspace_accessible;
};
#endif

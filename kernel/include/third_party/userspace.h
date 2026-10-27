#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#ifndef __USERSPACE_H
#define __USERSPACE_H

#define print(message) print_character(message)

#define MAX_PROCESSES 16    
#define KCALL_YIELD 0
#define SYSTEM_GID 0
#define MIN_PID 64
#define MAX_PID 1024

#define KCALL_IPC_SEND 1
#define KCALL_IPC_RECV 2
#define KCALL_IPC_CALL 3
#define PKCALL_MAP_DEVICE 10
#define KCALL_GET_PID_BY_GID 4
#define PID_SLOT_RANGE (MAX_PID - MIN_PID)
#define PID_SLOT_UNUSED 0xFF

extern uint8_t pid_to_slot[PID_SLOT_RANGE];
struct gid_cache_entry {
   uint32_t gid;
   uint32_t pid;
   bool valid;
};
extern struct gid_cache_entry gid_cache;
extern int current_process_idx;

extern uint32_t ext2_mkdir(const char *path);

extern uint32_t get_pid_by_gid(uint32_t gid);
extern uint32_t user_get_pid_by_gid(uint32_t gid);
extern void create_print_process(void);

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

extern void printu(char *message);


struct ext2_dir_entry;
extern void print_asccii_art(void);
extern bool dir_entry_matches(const struct ext2_dir_entry *entry, const char *name, uint32_t name_len);
extern char *concat(char *hardcoded_text, char *reason);
extern void *memcpy(void *dest, const void *src, size_t n);
extern int strcmp(const char *s1, const char *s2);
extern void *memset(void *dest, int val, size_t len);
extern size_t parse_number(char *str);
extern uint32_t round_page(uint32_t number, uint32_t page);
extern void print_hex(uint32_t val);
extern void print_dec(uint32_t val);

extern char *get_input(void);
extern char get_char(void);
extern bool test_keyboard(void);
extern void print_input(void);

extern int update(const char *path, uint32_t mode, uint32_t mtime);
extern int remove(const char *path);
extern int write(const char *path, const void *buffer, uint32_t count, uint32_t offset);
extern int read(const char *path, void *buffer, uint32_t count, uint32_t offset);

extern void enter_userspace(uintptr_t entry, uintptr_t user_stack_top);
extern void test_call(void);

extern uint32_t sys_ipc_send(uint32_t dest_pid, void *message_ptr);
extern uint32_t sys_ipc_recv(uint32_t src_pid, void *message_buf);
extern uint32_t sys_ipc_call(uint32_t dest_pid, void *msg, void *reply);
extern void sys_map_device(uintptr_t phys_addr, uintptr_t virt_addr);

extern void test_user_program(void);
extern void userspace_init(void);

extern uint32_t pkcall_map_device(uintptr_t phys_addr, uintptr_t virt_addr);
extern bool verify_pkcall(uint32_t gid, uint32_t sid);
extern uint32_t generate_sid(uint32_t gid, uintptr_t entry);
extern uint32_t get_pid_by_gid(uint32_t gid);
struct trap_frame {
   uintptr_t mepc;
   uintptr_t ra;
   uintptr_t sp;
   uintptr_t gp;
   uintptr_t tp;
   uintptr_t t0;
   uintptr_t t1;
   uintptr_t t2;
   uintptr_t s0;
   uintptr_t s1;
   uintptr_t a0;
   uintptr_t a1;
   uintptr_t a2;
   uintptr_t a3;
   uintptr_t a4;
   uintptr_t a5;
   uintptr_t a6;
   uintptr_t a7;
   uintptr_t s2;
   uintptr_t s3;
   uintptr_t s4;
   uintptr_t s5;
   uintptr_t s6;
   uintptr_t s7;
   uintptr_t s8;
   uintptr_t s9;
   uintptr_t s10;
   uintptr_t s11;
   uintptr_t t3;
   uintptr_t t4;
   uintptr_t t5;
   uintptr_t t6;
};

typedef enum {
   PROCESS_UNUSED = 0,
   PROCESS_READY,
   PROCESS_RUNNING,
   PROCESS_BLOCKED_SEND,
   PROCESS_BLOCKED_RECV
} p_state_t;

struct process {
   uint32_t pid;
   uint32_t gid;
   uint32_t sid;
   p_state_t state;
   uintptr_t kernel_stack;
   uintptr_t user_stack;
   struct trap_frame *frame;
   uint32_t ipc_target_pid;
   void *ipc_msg_buf;
};

struct fs_ipc_req {
   uint32_t cmd;
   char path[64];
};

struct updates_ipc_req {
   uint32_t start;
   char path[64];
};
extern struct process processes[MAX_PROCESSES];
extern int current_pid;

extern uint32_t kcall_ipc_send(uint32_t dest_pid, void *message_ptr);
extern uint32_t kcall_ipc_recv(uint32_t src_pid, void *message_buf);
extern uint32_t kcall_ipc_call(uint32_t dest_pid, void *msg, void *reply);
extern void kcall_analyze(struct trap_frame *frame);

extern bool check_gid_permission(uint32_t sender_pid, uint32_t receiver_pid);
extern uint32_t create_process(void *entry, uint32_t gid);
extern uint32_t get_free_pid(void);
extern struct process *get_process_by_pid(uint32_t pid);
extern struct process *get_process_by_frame(void *frame);
extern struct trap_frame *schedule(struct trap_frame *current_frame);
extern static inline void invalidate_gid_cache(void)
extern uint32_t user_ipc_send(uint32_t dest_pid, void *message_ptr);
extern uint32_t user_ipc_recv(uint32_t src_pid, void *message_buf);
extern uint32_t user_ipc_call(uint32_t dest_pid, void *msg, void *reply);
extern uint32_t user_yield(void);
extern int direct_handoff_idx;
static inline void copy_ipc_msg_64(void *dest, const void *src) {
   if ((((uintptr_t)dest | (uintptr_t)src) & (sizeof(uint32_t) - 1)) == 0) {
      uint32_t *d = (uint32_t *)dest;
      const uint32_t *s = (const uint32_t *)src;
      for (size_t i = 0; i < 16; i++) {
         d[i] = s[i];
      }
   } else {
      memcpy(dest, src, 64);
   }
}

#endif

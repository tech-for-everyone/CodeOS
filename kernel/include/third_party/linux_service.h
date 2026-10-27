#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifndef __LINUX_SERVICE_H
#define __LINUX_SERVICE_H

#if defined(__USE_LINUX_MODULES__)

#include "elf_loader.h"

#define LINUX_MODULE_GID 16
#define LINUX_MOD_CMD_LOAD 1
#define LINUX_MOD_CMD_UNLOAD 2
#define LINUX_MOD_CMD_LOOKUP 3
#define LINUX_MOD_CMD_LIST 4

struct linux_mod_ipc_req {
   uint32_t cmd;
   const void *data_ptr;
   size_t data_len;
   char name[64];
};

struct linux_mod_ipc_resp {
   int32_t status;
   void *symbol_addr;
   uint32_t module_count;
};

struct linux_fast_dispatch {
   void *(*kmalloc)(size_t size, int flags);
   void *(*kzalloc)(size_t size, int flags);
   void (*kfree)(const void *ptr);
   int (*printk)(const char *fmt, ...);
   void *(*ioremap)(uintptr_t phys_addr, size_t size);
   void (*iounmap)(volatile void *addr);
   void (*spin_lock)(void *sl);
   void (*spin_unlock)(void *sl);
   int (*request_irq)(uint32_t irq, void *handler, uint32_t flags, const char *name, void *dev);
   void (*free_irq)(uint32_t irq, void *dev);
};

extern struct linux_fast_dispatch g_linux_fast_dispatch;

extern void create_linux_module_process(void);
extern void linux_module_service_main(void);
extern int user_load_linux_module(const void *ko_data, size_t size);
extern int user_unload_linux_module(const char *name);

#else

static inline void create_linux_module_process(void) {
}

#endif

#endif

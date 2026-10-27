// Placeholder file, updates are OS dependant

#include <stdbool.h>
#include <stdint.h>

#ifndef __NOAB_UPDATE_H
#define __NOAB_UPDATE_H

#define PATCH_MAGIC 0x4669677572653039ULL

extern uint32_t install_update(char use_slot, const char *update_file);
extern char get_active_slot(void);
extern void flip_active_slot(void);
extern bool get_slot_status(void);
extern void init_slot_status(void);
extern int apply_patch(uint8_t *base_buf, uint32_t base_size, const uint8_t *patch_buf, uint32_t patch_size);

struct patch_header {
   uint64_t magic;
   uint64_t signature;
   uint32_t old_size;
   uint32_t new_size;
   uint32_t chunk_count;
};

#endif

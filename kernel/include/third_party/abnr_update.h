#include <stdbool.h>
#include <stdint.h>

#ifndef __ABNR_UPDATE_H
#define __ABNR_UPDATE_H

#define PATCH_MAGIC 0x4669677572653039ULL

extern void make_security_copy(void);
extern uint32_t revert_changes(void);
extern uint32_t install_update(char active_slot, const char *update_file);
extern int apply_patch(uint8_t *base_buf, uint32_t base_size, const uint8_t *patch_buf, uint32_t patch_size);

struct patch_header {
   uint64_t magic;
   uint64_t signature;
   uint32_t old_size;
   uint32_t new_size;
   uint32_t chunk_count;
};

#endif

#ifndef __NTAR_H
#define __NTAR_H

#include <stdint.h>
#include <stdbool.h>

#define NTAR_MAX_RAM_SECTORS 512
#define NTAR_HASH_BUCKETS 64

struct ntar_sector {
   uint64_t sector_num;
   uint8_t data[512];
   bool in_use;
};

extern void ntar_init(void);
extern bool ntar_is_active(void);
extern int ntar_ram_write_sector(uint64_t sector, const void *buffer);
extern int ntar_ram_read_sector(uint64_t sector, void *buffer);
extern uint32_t ntar_get_used_sectors(void);
extern uint32_t ntar_get_used_count(void);
extern void ntar_reset(void);

#endif

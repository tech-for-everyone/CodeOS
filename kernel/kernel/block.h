#ifndef BLOCK_H
#define BLOCK_H

#include "types.h"

#define BLOCK_SECTOR_SIZE 512

enum block_type { BLOCK_NONE, BLOCK_ATA, BLOCK_NVME };

int  block_init(void);
int  block_read_sectors(uint32_t lba, uint8_t count, void *buf);
int  block_write_sectors(uint32_t lba, uint8_t count, const void *buf);
int  block_available(void);
void block_get_info(int *sectors, int *is_lba);
enum block_type block_backend(void);
const char *block_backend_name(void);

#endif

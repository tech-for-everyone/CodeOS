#ifndef NVME_H
#define NVME_H

#include "types.h"

#define NVME_SECTOR_SIZE 512

int  nvme_init(void);
int  nvme_read_sectors(uint64_t lba, uint32_t count, void *buf);
int  nvme_write_sectors(uint64_t lba, uint32_t count, const void *buf);
int  nvme_available(void);
void nvme_get_info(uint64_t *sectors);

#endif

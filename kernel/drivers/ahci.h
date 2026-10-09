#ifndef AHCI_H
#define AHCI_H

#include "types.h"

#define AHCI_SECTOR_SIZE 512

int  ahci_init(void);
int  ahci_read_sectors(uint64_t lba, uint32_t count, void *buf);
int  ahci_write_sectors(uint64_t lba, uint32_t count, const void *buf);
int  ahci_available(void);
void ahci_get_info(uint64_t *sectors);

#endif

#ifndef PART_H
#define PART_H

#include "types.h"

#define PART_MAX 8

typedef struct {
    uint8_t  bootable;
    uint8_t  type;
    uint32_t start_lba;
    uint32_t sector_count;
} partition_t;

int  part_init(void);
int  part_count(void);
int  part_get(int idx, partition_t *p);

#endif

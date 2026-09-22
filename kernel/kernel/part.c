#include "part.h"
#include "kprintf.h"
#include "string.h"
#include "block.h"

static partition_t partitions[PART_MAX];
static int part_count_val;

int part_init(void) {
    if (!block_available()) return 0;

    uint8_t mbr[BLOCK_SECTOR_SIZE];
    if (block_read_sectors(0, 1, mbr) < 0) return 0;

    if (mbr[510] != 0x55 || mbr[511] != 0xAA) {
        kprintf("part: no valid MBR\n");
        return 0;
    }

    part_count_val = 0;
    for (int i = 0; i < 4 && i < PART_MAX; i++) {
        uint8_t *e = mbr + 0x1BE + i * 16;
        if (e[4] == 0) continue;
        partitions[part_count_val].bootable = (e[0] != 0);
        partitions[part_count_val].type     = e[4];
        partitions[part_count_val].start_lba = *(uint32_t*)(e + 8);
        partitions[part_count_val].sector_count = *(uint32_t*)(e + 12);
        part_count_val++;
    }

    return part_count_val;
}

int part_count(void) {
    return part_count_val;
}

int part_get(int idx, partition_t *p) {
    if (idx < 0 || idx >= part_count_val) return -1;
    *p = partitions[idx];
    return 0;
}

#include "block.h"
#include "../drivers/ata.h"
#include "../drivers/nvme.h"
#include "kprintf.h"

static enum block_type backend = BLOCK_NONE;
static int block_sectors;
static int block_lba;

int block_init(void) {
    if (nvme_init()) {
        backend = BLOCK_NVME;
        uint64_t sectors = 0;
        nvme_get_info(&sectors);
        block_sectors = sectors > 0x7fffffffULL ? 0x7fffffff : (int)sectors;
        block_lba = 1;
        kprintf("block: NVMe disk detected (%d MB, LBA)\n",
                (int)(((uint64_t)block_sectors * 512) / 1048576));
        return 1;
    }

    if (ata_init()) {
        backend = BLOCK_ATA;
        int is_lba;
        ata_get_info(&block_sectors, &is_lba);
        block_lba = is_lba;
        int mb = (int)((uint64_t)block_sectors * 512 / 1048576);
        kprintf("block: ATA disk detected (%d MB, LBA%c)\n",
                mb, block_lba ? ' ' : '2');
        return 1;
    }

    kprintf("block: no disk detected\n");
    return 0;
}

int block_read_sectors(uint32_t lba, uint8_t count, void *buf) {
    switch (backend) {
        case BLOCK_NVME:
            return nvme_read_sectors(lba, count, buf);
        case BLOCK_ATA:
            return ata_read_sectors(lba, count, buf);
        default:
            return -1;
    }
}

int block_write_sectors(uint32_t lba, uint8_t count, const void *buf) {
    switch (backend) {
        case BLOCK_NVME:
            return nvme_write_sectors(lba, count, buf);
        case BLOCK_ATA:
            return ata_write_sectors(lba, count, buf);
        default:
            return -1;
    }
}

int block_available(void) {
    return backend != BLOCK_NONE;
}

void block_get_info(int *sectors, int *is_lba) {
    if (sectors) *sectors = block_sectors;
    if (is_lba)  *is_lba  = block_lba;
}

enum block_type block_backend(void) {
    return backend;
}

const char *block_backend_name(void) {
    switch (backend) {
        case BLOCK_NVME: return "NVMe";
        case BLOCK_ATA:  return "ATA  (PATA/IDE)";
        default:         return "none";
    }
}

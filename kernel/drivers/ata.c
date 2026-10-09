#include "ata.h"
#include "../arch/x86_64/io.h"
#include "../arch/x86_64/pci.h"
#include "timer.h"

#define ATA_DATA     0x00
#define ATA_FEATURES 0x01
#define ATA_SECTOR   0x02
#define ATA_LBA_LO   0x03
#define ATA_LBA_MID  0x04
#define ATA_LBA_HI   0x05
#define ATA_DRIVE    0x06
#define ATA_CMD      0x07
#define ATA_STATUS   0x07

#define ATA_CMD_READ_PIO   0x20
#define ATA_CMD_WRITE_PIO  0x30
#define ATA_CMD_IDENTIFY   0xEC

#define ATA_SR_BSY 0x80
#define ATA_SR_DRQ 0x08
#define ATA_SR_ERR 0x01

static int ata_initialized;
static int ata_lba_capable;
static int ata_total_sectors;
static uint16_t ata_io_base;
static uint16_t ata_ctrl_base;
static int ata_master;

int ata_init(void) {
    ata_initialized = 0;
    ata_lba_capable = 0;
    ata_total_sectors = 0;
    ata_io_base = 0x1F0;
    ata_ctrl_base = 0x3F6;
    ata_master = 1;

    /* Select master drive */
    outb(ata_io_base + ATA_DRIVE, 0xA0 | (ata_master ? 0 : 0x10));

    /* Give the drive time to assert BSY */
    for (volatile int w = 0; w < 10000; w++) asm volatile("pause");

    /* Check for floating bus (no controller at all) */
    uint8_t st = inb(ata_io_base + ATA_STATUS);
    if (st == 0xFF) return 0;

    /* Send IDENTIFY command */
    outb(ata_io_base + ATA_CMD, ATA_CMD_IDENTIFY);

    /* Poll status until BSY clears or timeout */
    int timeout = 1000000;
    while (timeout--) {
        st = inb(ata_io_base + ATA_STATUS);
        if (st == 0) { asm volatile("pause"); continue; }
        if (!(st & ATA_SR_BSY)) break;
        asm volatile("pause");
    }
    if (timeout <= 0) return 0;

    /* Check for error */
    if (st & ATA_SR_ERR) return 0;

    /* Wait for DRQ */
    timeout = 1000000;
    while (timeout--) {
        st = inb(ata_io_base + ATA_STATUS);
        if (!(st & ATA_SR_BSY) && (st & ATA_SR_DRQ)) break;
        asm volatile("pause");
    }
    if (timeout <= 0) return 0;

    uint16_t id[256];
    for (int i = 0; i < 256; i++)
        id[i] = inw(ata_io_base + ATA_DATA);

    ata_lba_capable = (id[49] >> 9) & 1;
    if (ata_lba_capable) {
        uint32_t lo = id[100], hi = id[101];
        ata_total_sectors = lo | ((uint32_t)hi << 16);
    } else {
        uint32_t lo = id[60], hi = id[61];
        ata_total_sectors = lo | ((uint32_t)hi << 16);
    }

    if (ata_total_sectors == 0) return 0;

    ata_initialized = 1;
    return 1;
}

int ata_available(void) {
    return ata_initialized;
}

void ata_get_info(int *sectors, int *is_lba) {
    if (sectors) *sectors = ata_total_sectors;
    if (is_lba)  *is_lba  = ata_lba_capable;
}

static int ata_poll(void) {
    for (int i = 0; i < 1000000; i++) {
        uint8_t st = inb(ata_io_base + ATA_STATUS);
        if (!(st & ATA_SR_BSY) && (st & ATA_SR_DRQ)) return 1;
        if (st & ATA_SR_ERR) return -1;
        if (i % 100 == 0) asm volatile("pause");
    }
    return -1;
}

int ata_read_sectors(uint32_t lba, uint8_t count, void *buf) {
    if (!ata_initialized) return -1;
    if (!buf) return -1;
    if (count == 0) return 0;

    outb(ata_io_base + ATA_DRIVE, 0xE0 | (ata_master ? 0 : 0x10) |
         ((ata_lba_capable ? (lba >> 24) & 0x0F : 0)));
    outb(ata_io_base + ATA_FEATURES, 0);
    outb(ata_io_base + ATA_SECTOR, count);
    outb(ata_io_base + ATA_LBA_LO, lba & 0xFF);
    outb(ata_io_base + ATA_LBA_MID, (lba >> 8) & 0xFF);
    outb(ata_io_base + ATA_LBA_HI, (lba >> 16) & 0xFF);
    outb(ata_io_base + ATA_CMD, ATA_CMD_READ_PIO);

    for (int s = 0; s < count; s++) {
        if (ata_poll() < 0) return -1;
        for (int i = 0; i < 256; i++)
            ((uint16_t*)buf)[s * 256 + i] = inw(ata_io_base + ATA_DATA);
    }
    return count * ATA_SECTOR_SIZE;
}

int ata_write_sectors(uint32_t lba, uint8_t count, const void *buf) {
    if (!ata_initialized) return -1;
    if (!buf) return -1;
    if (count == 0) return 0;

    outb(ata_io_base + ATA_DRIVE, 0xE0 | (ata_master ? 0 : 0x10) |
         ((ata_lba_capable ? (lba >> 24) & 0x0F : 0)));
    outb(ata_io_base + ATA_FEATURES, 0);
    outb(ata_io_base + ATA_SECTOR, count);
    outb(ata_io_base + ATA_LBA_LO, lba & 0xFF);
    outb(ata_io_base + ATA_LBA_MID, (lba >> 8) & 0xFF);
    outb(ata_io_base + ATA_LBA_HI, (lba >> 16) & 0xFF);
    outb(ata_io_base + ATA_CMD, ATA_CMD_WRITE_PIO);

    for (int s = 0; s < count; s++) {
        if (ata_poll() < 0) return -1;
        for (int i = 0; i < 256; i++)
            outw(ata_io_base + ATA_DATA, ((uint16_t*)buf)[s * 256 + i]);
        int timeout = 1000000;
        while (timeout--) {
            uint8_t st = inb(ata_io_base + ATA_STATUS);
            if (!(st & ATA_SR_BSY)) break;
            asm volatile("pause");
        }
        if (timeout <= 0) return -1;
    }
    return count * ATA_SECTOR_SIZE;
}

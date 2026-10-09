/* QEMU firmware config (fw_cfg) driver for the ARM64 virt platform.
 *
 * MMIO layout (QEMU virt):
 *   0x09020000  data   (read: selected entry bytes, big-endian stream)
 *   0x09020008  ctl    (write 16-bit selector)
 *   0x09020010  dma    (write guest-phys address of a FWCfgDmaAccess struct)
 *
 * DMA protocol (QEMU >= 2.9, all fields big-endian):
 *   control bits: ERROR=0x01 READ=0x02 SKIP=0x04 SELECT=0x08 WRITE=0x10
 *   selector is in control bits [31:16] when SELECT is set
 *   length holds the transfer size; QEMU writes the final control word
 *   back into the access struct in guest RAM.
 */
#include "fw_cfg.h"
#include "../kernel/string.h"
#include "kprintf.h"
#include "io.h"

#define FW_CFG_SIGNATURE    0x00
#define FW_CFG_FILE_DIR     0x19
#define FW_CFG_FILE_FIRST   0x20

#define FW_CFG_DMA_CTL_ERROR   0x01
#define FW_CFG_DMA_CTL_READ    0x02
#define FW_CFG_DMA_CTL_SKIP    0x04
#define FW_CFG_DMA_CTL_SELECT  0x08
#define FW_CFG_DMA_CTL_WRITE   0x10

typedef struct {
    uint32_t control;
    uint32_t length;
    uint64_t address;
} FWCfgDmaAccess;

typedef struct {
    uint32_t size;
    uint16_t select;
    uint16_t reserved;
    char name[56];
} FWCfgFile;

static int fw_cfg_ok;
static FWCfgDmaAccess dma_access;

static inline uint16_t bswap16(uint16_t v) { return __builtin_bswap16(v); }
static inline uint32_t bswap32(uint32_t v) { return __builtin_bswap32(v); }
static inline uint64_t bswap64(uint64_t v) { return __builtin_bswap64(v); }

/* The fw_cfg MMIO registers are declared big-endian devices; the guest must
 * present values in big-endian byte order (byte-swapped on a LE core). */
static void fw_cfg_select(uint16_t sel) {
    *(volatile uint16_t *)FW_CFG_CTL = bswap16(sel);
}

static void fw_cfg_read_current(uint8_t *buf, uint32_t len) {
    for (uint32_t i = 0; i < len; i++)
        buf[i] = *(volatile uint8_t *)FW_CFG_DATA;
}

void fw_cfg_read_bytes(uint16_t sel, uint8_t *buf, uint32_t len) {
    fw_cfg_select(sel);
    fw_cfg_read_current(buf, len);
}

static uint32_t fw_cfg_read_be32(void) {
    uint8_t b[4];
    fw_cfg_read_current(b, 4);
    return ((uint32_t)b[0] << 24) | ((uint32_t)b[1] << 16) |
           ((uint32_t)b[2] << 8) | b[3];
}

int fw_cfg_init(void) {
    uint8_t sig[4];
    fw_cfg_read_bytes(FW_CFG_SIGNATURE, sig, 4);
    if (sig[0] == 'Q' && sig[1] == 'E' && sig[2] == 'M' && sig[3] == 'U') {
        fw_cfg_ok = 1;
        kprintf("fw_cfg: QEMU firmware config detected (DMA @ 0x%016lx)\n",
                (uint64_t)FW_CFG_DMA);
    } else {
        fw_cfg_ok = 0;
        kprintf("fw_cfg: not present (sig=%c%c%c%c)\n",
                sig[0], sig[1], sig[2], sig[3]);
    }
    return fw_cfg_ok;
}

int fw_cfg_available(void) { return fw_cfg_ok; }

int fw_cfg_find_file(const char *name, uint16_t *sel_out, uint32_t *size_out) {
    if (!fw_cfg_ok) return 0;
    fw_cfg_select(FW_CFG_FILE_DIR);
    uint32_t count = fw_cfg_read_be32();
    for (uint32_t i = 0; i < count; i++) {
        FWCfgFile f;
        fw_cfg_read_current((uint8_t *)&f, sizeof(f));
        if (strncmp(f.name, name, 55) == 0) {
            *sel_out = bswap16(f.select);
            *size_out = bswap32(f.size);
            return 1;
        }
    }
    return 0;
}

int fw_cfg_dma_write(uint16_t sel, const void *data, uint32_t len) {
    if (!fw_cfg_ok) return 0;
    uint32_t ctl = ((uint32_t)sel << 16) | FW_CFG_DMA_CTL_SELECT | FW_CFG_DMA_CTL_WRITE;

    dma_access.control = bswap32(ctl);
    dma_access.length  = bswap32(len);
    dma_access.address = bswap64((uint64_t)data);

    /* Make sure the struct contents are visible to the device before we
     * poke the DMA register (cache clean, then ordering barrier). */
    __asm__ volatile("dc civac, %0" :: "r"(&dma_access));
    dmb();

    *(volatile uint64_t *)FW_CFG_DMA = bswap64((uint64_t)&dma_access);

    /* QEMU completes synchronously and stores the result control word back
     * into the access struct; invalidate our cached copy before reading. */
    dmb();
    __asm__ volatile("dc civac, %0" :: "r"(&dma_access.control));
    dmb();

    uint32_t result = bswap32(dma_access.control);
    if (result & FW_CFG_DMA_CTL_ERROR) {
        kprintf("fw_cfg: DMA write error (sel=%u)\n", sel);
        return 0;
    }
    return 1;
}

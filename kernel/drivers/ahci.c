#include "ahci.h"
#include "../arch/x86_64/pci.h"
#include "../arch/x86_64/io.h"
#include "../kernel/pmm.h"
#include "../kernel/vmm.h"
#include "../kernel/string.h"
#include "../kernel/kprintf.h"
#include "../kernel/idt.h"

/* ABAR offsets */
#define AHCI_CAP      0x00
#define AHCI_GHC      0x04
#define AHCI_IS       0x08
#define AHCI_PI       0x0C
#define AHCI_VS       0x10
#define AHCI_CAP2     0x24
#define AHCI_BOHC     0x28

/* Port register offsets (relative to port base = 0x100 + port*0x80) */
#define POR_PxCLB     0x00
#define POR_PxCLBU    0x04
#define POR_PxFB      0x08
#define POR_PxFBU     0x0C
#define POR_PxIS      0x10
#define POR_PxIE      0x14
#define POR_PxCMD     0x18
#define POR_PxTFD     0x20
#define POR_PxSIG     0x24
#define POR_PxSSTS    0x28
#define POR_PxSCTL    0x2C
#define POR_PxSERR    0x30
#define POR_PxSACT    0x34
#define POR_PxCI      0x38
#define POR_PxSNTF    0x3C
#define POR_PxFBS     0x40

/* GHC bits */
#define GHC_AE        (1U << 31)
#define GHC_IE        (1U << 1)
#define GHC_HR        (1U << 0)

/* PxCMD bits */
#define CMD_ST        (1U << 0)
#define CMD_FRE       (1U << 1)
#define CMD_FR        (1U << 14)
#define CMD_CR        (1U << 15)
#define CMD_ATAPI     (1U << 24)
#define CMD_ICC       (1U << 28)
#define CMD_ASP       (1U << 29)
#define CMD_POD       (1U << 30)
#define CMD_SUD       (1U << 31)

/* Port interrupt status bits */
#define PIS_DHRS      (1U << 0)
#define PIS_PSS       (1U << 1)
#define PIS_DSS       (1U << 2)
#define PIS_SDBS      (1U << 3)
#define PIS_UFS       (1U << 4)
#define PIS_DPS       (1U << 5)
#define PIS_PCS       (1U << 6)
#define PIS_DMPS      (1U << 7)
#define PIS_PRCS      (1U << 22)
#define PIS_IPMS      (1U << 23)
#define PIS_OFS       (1U << 24)
#define PIS_IFS       (1U << 25)
#define PIS_HBFS      (1U << 26)
#define PIS_HBDS      (1U << 27)
#define PIS_TFES      (1U << 30)
#define PIS_CPDS      (1U << 31)
#define PIS_ERROR     (PIS_TFES | PIS_HBDS | PIS_HBFS | PIS_IFS | PIS_OFS | PIS_IPMS)
#define PIS_READY     (PIS_DHRS | PIS_PSS)

/* ATA commands */
#define ATA_IDENTIFY          0xEC
#define ATA_READ_DMA_EXT      0x25
#define ATA_WRITE_DMA_EXT     0x35

/* FIS types */
#define FIS_H2D      0x27
#define FIS_D2H      0x34
#define FIS_DATA     0x46

/* SATA signature */
#define SIG_ATA      0x00000101
#define SIG_ATAPI    0xEB140101

#define MAX_PORTS    32

typedef struct {
    uint8_t  fis_type;
    uint8_t  pm_port:4;
    uint8_t  rsv0:3;
    uint8_t  c:1;
    uint8_t  command;
    uint8_t  feature_low;
    uint8_t  lba0;
    uint8_t  lba1;
    uint8_t  lba2;
    uint8_t  device;
    uint8_t  lba3;
    uint8_t  lba4;
    uint8_t  lba5;
    uint8_t  feature_high;
    uint8_t  count_low;
    uint8_t  count_high;
    uint8_t  icc;
    uint8_t  control;
    uint8_t  rsv1[4];
} __attribute__((packed)) fis_h2d_t;

typedef struct {
    uint16_t cfl:5;
    uint16_t a:1;
    uint16_t w:1;
    uint16_t p:1;
    uint16_t r:1;
    uint16_t b:1;
    uint16_t c:1;
    uint16_t rsv0:5;
    uint16_t prdtl;
    uint32_t prdbc;
    uint32_t ctba;
    uint32_t ctbau;
    uint32_t rsv1[4];
} __attribute__((packed)) cmd_hdr_t;

typedef struct {
    uint32_t dba;
    uint32_t dbau;
    uint32_t rsv0;
    uint32_t dbc_i;
} __attribute__((packed)) prdt_entry_t;

typedef struct {
    uint8_t  cfis[64];
    uint8_t  atapi[32];
    uint8_t  rsv[32];
    prdt_entry_t prdt[8];
} __attribute__((packed)) cmd_table_t;

/* state */
static uint8_t    ahci_bus, ahci_slot, ahci_func;
static uint64_t   ahci_abar;
static int        ahci_initialized;
static int        ahci_port;
static uint64_t   ahci_sector_count;
static int        ahci_irq;
static volatile int ahci_cmd_done;

/* Per-port DMA memory — keep both virtual (for CPU) and physical (for AHCI DMA) */
static cmd_hdr_t   *cmd_list;
static uint8_t     *recv_fis;
static cmd_table_t *cmd_tbl;
static uint64_t     cmd_list_phys;
static uint64_t     recv_fis_phys;
static uint64_t     cmd_tbl_phys;
static int          ahci_irq_line;

/* MMIO helpers */
static inline uint32_t ahci_read(uint64_t off) {
    return *(volatile uint32_t*)(uintptr_t)(ahci_abar + off);
}
static inline void ahci_write(uint64_t off, uint32_t v) {
    *(volatile uint32_t*)(uintptr_t)(ahci_abar + off) = v;
}
static inline uint32_t port_read(int port, uint64_t reg) {
    return ahci_read(0x100 + port * 0x80 + reg);
}
static inline void port_write(int port, uint64_t reg, uint32_t v) {
    ahci_write(0x100 + port * 0x80 + reg, v);
}

static void msleep(int ms) {
    for (volatile int i = 0; i < ms * 50000; i++) asm volatile("pause");
}

static int port_spin_while(int port, uint64_t reg, uint32_t mask, int timeout_ms) {
    for (int i = 0; i < timeout_ms; i++) {
        if (!(port_read(port, reg) & mask)) return 0;
        msleep(1);
    }
    return -1;
}

static void ahci_irq_handler(int_frame_t *frame) {
    (void)frame;
    uint32_t is = ahci_read(AHCI_IS);
    if (!is) return;
    ahci_write(AHCI_IS, is);

    for (int p = 0; p < MAX_PORTS; p++) {
        if (!(is & (1U << p))) continue;
        uint32_t pis = port_read(p, POR_PxIS);
        if (pis) {
            port_write(p, POR_PxIS, pis);
            if (pis & PIS_READY)
                ahci_cmd_done = 1;
            if (pis & PIS_ERROR)
                ahci_cmd_done = -1;
        }
    }
}

static int find_free_slot(int port) {
    uint32_t ci = port_read(port, POR_PxCI);
    uint32_t sact = port_read(port, POR_PxSACT);
    uint32_t busy = ci | sact;
    for (int i = 0; i < 32; i++) {
        if (!(busy & (1U << i))) return i;
    }
    return -1;
}

static int ahci_issue_cmd(int port, int slot) {
    ahci_cmd_done = 0;
    port_write(port, POR_PxCI, 1U << slot);

    if (ahci_irq >= 0) {
        for (volatile int i = 0; i < 5000000; i++) {
            if (ahci_cmd_done != 0) break;
            asm volatile("pause");
        }
        return (ahci_cmd_done > 0) ? 0 : -1;
    }

    return port_spin_while(port, POR_PxCI, 1U << slot, 5000);
}

static int ahci_send_fis(int port, fis_h2d_t *fis, int w,
                          uint64_t buf_phys, uint32_t byte_count) {
    int slot = find_free_slot(port);
    if (slot < 0) return -1;

    cmd_hdr_t *hdr = &cmd_list[slot];
    memset(hdr, 0, sizeof(*hdr));
    hdr->cfl = sizeof(fis_h2d_t) / 4;
    hdr->w = w;
    hdr->p = (w == 0) ? 1 : 0;
    hdr->c = 1;
    hdr->prdtl = (byte_count > 0) ? 1 : 0;
    hdr->ctba = (uint32_t)(cmd_tbl_phys & 0xFFFFFFFF);
    hdr->ctbau = (uint32_t)((cmd_tbl_phys >> 32) & 0xFFFFFFFF);

    memset(cmd_tbl, 0, sizeof(*cmd_tbl));
    memcpy(cmd_tbl->cfis, fis, sizeof(*fis));

    if (byte_count > 0) {
        cmd_tbl->prdt[0].dba = (uint32_t)(buf_phys & 0xFFFFFFFF);
        cmd_tbl->prdt[0].dbau = (uint32_t)((buf_phys >> 32) & 0xFFFFFFFF);
        cmd_tbl->prdt[0].dbc_i = (byte_count - 1) & 0x3FFFFF;
    }

    return ahci_issue_cmd(port, slot);
}

static int ahci_identify(int port) {
    fis_h2d_t fis;
    memset(&fis, 0, sizeof(fis));
    fis.fis_type = FIS_H2D;
    fis.c = 1;
    fis.command = ATA_IDENTIFY;
    fis.device = 0;

    uint64_t id_buf_phys = (uint64_t)pmm_alloc_pages(1);
    if (!id_buf_phys) return -1;
    uint16_t *id = (uint16_t*)phys_to_virt(id_buf_phys);
    memset(id, 0, 4096);

    int ret = ahci_send_fis(port, &fis, 0, id_buf_phys, AHCI_SECTOR_SIZE);

    uint32_t lo = id[100], mid = id[101], hi = id[102];
    ahci_sector_count = lo | ((uint64_t)mid << 16) | ((uint64_t)hi << 32);
    if (ahci_sector_count == 0) {
        lo = id[60]; hi = id[61];
        ahci_sector_count = lo | ((uint64_t)hi << 16);
    }

    pmm_free_pages(id_buf_phys, 1);
    return (ret == 0 && ahci_sector_count > 0) ? 0 : -1;
}

static int ahci_port_init(int port) {
    port_write(port, POR_PxCMD, 0);
    if (port_spin_while(port, POR_PxCMD, CMD_FR | CMD_CR, 100) < 0)
        return -1;

    cmd_list_phys = (uint64_t)pmm_alloc_pages(1);
    if (!cmd_list_phys) return -1;
    cmd_list = (cmd_hdr_t*)phys_to_virt(cmd_list_phys);
    memset(cmd_list, 0, 4096);

    recv_fis_phys = (uint64_t)pmm_alloc_pages(1);
    if (!recv_fis_phys) return -1;
    recv_fis = (uint8_t*)phys_to_virt(recv_fis_phys);
    memset(recv_fis, 0, 4096);

    port_write(port, POR_PxCLB, (uint32_t)(cmd_list_phys & 0xFFFFFFFF));
    port_write(port, POR_PxCLBU, (uint32_t)((cmd_list_phys >> 32) & 0xFFFFFFFF));
    port_write(port, POR_PxFB, (uint32_t)(recv_fis_phys & 0xFFFFFFFF));
    port_write(port, POR_PxFBU, (uint32_t)((recv_fis_phys >> 32) & 0xFFFFFFFF));

    port_write(port, POR_PxSERR, 0xFFFFFFFF);

    port_write(port, POR_PxCMD, CMD_POD | CMD_SUD | CMD_ST | CMD_FRE);

    /* Quick device detection — don't hang boot if no device */
    int dev_present = 0;
    uint32_t ssts = port_read(port, POR_PxSSTS);
    if ((ssts & 0x0F) == 3) { dev_present = 1; }
    if (!dev_present) {
        for (int i = 0; i < 50; i++) {
            ssts = port_read(port, POR_PxSSTS);
            if ((ssts & 0x0F) == 3) { dev_present = 1; break; }
            for (volatile int j = 0; j < 10000; j++) asm volatile("pause");
        }
    }
    if (!dev_present) return -1;

    msleep(10);

    uint32_t sig = port_read(port, POR_PxSIG);
    if (sig != SIG_ATA) return -1;

    cmd_tbl_phys = (uint64_t)pmm_alloc_pages(1);
    if (!cmd_tbl_phys) return -1;
    cmd_tbl = (cmd_table_t*)phys_to_virt(cmd_tbl_phys);
    memset(cmd_tbl, 0, 4096);

    /* Enable port interrupts */
    port_write(port, POR_PxIE, PIS_READY | PIS_ERROR);

    return ahci_identify(port);
}

int ahci_init(void) {
    ahci_initialized = 0;
    ahci_port = -1;
    ahci_sector_count = 0;

    for (int idx = 0;; idx++) {
        ahci_irq = -1;

        if (!pci_find_class_idx(0x01, 0x06, idx, &ahci_bus, &ahci_slot, &ahci_func))
            break;

        kprintf("ahci: trying controller %d at %d:%d.%d\n", idx, ahci_bus, ahci_slot, ahci_func);

        uint32_t bar5 = pci_config_read(ahci_bus, ahci_slot, ahci_func, 0x24);
        ahci_abar = bar5 & ~0x1F;
        if (bar5 & 0x04) {
            uint32_t bar5u = pci_config_read(ahci_bus, ahci_slot, ahci_func, 0x28);
            ahci_abar |= (uint64_t)bar5u << 32;
        }

        uint32_t cmd_reg = pci_config_read(ahci_bus, ahci_slot, ahci_func, 0x04);
        cmd_reg |= 0x06;
        pci_config_write(ahci_bus, ahci_slot, ahci_func, 0x04, cmd_reg);

        uint64_t abar_size = 0x2000;
        for (uint64_t off = 0; off < abar_size; off += 0x1000) {
            vmm_map_page(ahci_abar + off, ahci_abar + off,
                         PAGE_PRESENT | PAGE_WRITE | PAGE_NX);
        }

        uint32_t ghc = ahci_read(AHCI_GHC);
        ghc |= GHC_AE;
        ahci_write(AHCI_GHC, ghc);
        msleep(10);

        /* Try MSI first */
        if (pci_msi_enable(ahci_bus, ahci_slot, ahci_func, 0x20 + 11, 0xFEE00000) == 0) {
            ahci_irq = 11;
            ahci_irq_line = -1;
            kprintf("ahci: using MSI vector %d\n", ahci_irq);
        } else {
            ahci_irq_line = (uint8_t)(pci_config_read(ahci_bus, ahci_slot, ahci_func, 0x3C) & 0xFF);
            ahci_irq = ahci_irq_line;
            kprintf("ahci: using INTx IRQ %d\n", ahci_irq);
        }

        if (ahci_irq >= 0 && ahci_irq < 16)
            irq_register(ahci_irq, ahci_irq_handler);

        ghc |= GHC_IE;
        ahci_write(AHCI_GHC, ghc);

        uint32_t pi = ahci_read(AHCI_PI);
        kprintf("ahci: ABAR=0x%lx PI=0x%x\n", ahci_abar, pi);

        for (int p = 0; p < MAX_PORTS; p++) {
            if (!(pi & (1U << p))) continue;
            kprintf("ahci: probing port %d ...\n", p);
            if (ahci_port_init(p) == 0) {
                ahci_port = p;
                ahci_initialized = 1;
                kprintf("ahci: port %d ready, %llu sectors\n", p, ahci_sector_count);
                return 0;
            }
        }
    }

    kprintf("ahci: no ATA device found\n");
    return -1;
}

int ahci_read_sectors(uint64_t lba, uint32_t count, void *buf) {
    if (!ahci_initialized || ahci_port < 0 || !buf) return -1;
    if (count == 0 || count > 256) return -1;

    uint64_t buf_phys = (uint64_t)pmm_alloc_pages((count * AHCI_SECTOR_SIZE + 4095) / 4096);
    if (!buf_phys) return -1;
    void *buf_virt = (void*)phys_to_virt(buf_phys);

    fis_h2d_t fis;
    memset(&fis, 0, sizeof(fis));
    fis.fis_type = FIS_H2D;
    fis.c = 1;
    fis.command = ATA_READ_DMA_EXT;
    fis.lba0 = lba & 0xFF;
    fis.lba1 = (lba >> 8) & 0xFF;
    fis.lba2 = (lba >> 16) & 0xFF;
    fis.device = 0x40;
    fis.lba3 = (lba >> 24) & 0xFF;
    fis.lba4 = (lba >> 32) & 0xFF;
    fis.lba5 = (lba >> 40) & 0xFF;
    fis.count_low = count & 0xFF;
    fis.count_high = (count >> 8) & 0xFF;

    int ret = ahci_send_fis(ahci_port, &fis, 0, buf_phys, count * AHCI_SECTOR_SIZE);
    if (ret == 0)
        memcpy(buf, buf_virt, count * AHCI_SECTOR_SIZE);

    pmm_free_pages(buf_phys, (count * AHCI_SECTOR_SIZE + 4095) / 4096);
    return (ret == 0) ? (int)(count * AHCI_SECTOR_SIZE) : -1;
}

int ahci_write_sectors(uint64_t lba, uint32_t count, const void *buf) {
    if (!ahci_initialized || ahci_port < 0 || !buf) return -1;
    if (count == 0 || count > 256) return -1;

    uint64_t buf_phys = (uint64_t)pmm_alloc_pages((count * AHCI_SECTOR_SIZE + 4095) / 4096);
    if (!buf_phys) return -1;
    void *buf_virt = (void*)phys_to_virt(buf_phys);
    memcpy(buf_virt, buf, count * AHCI_SECTOR_SIZE);

    fis_h2d_t fis;
    memset(&fis, 0, sizeof(fis));
    fis.fis_type = FIS_H2D;
    fis.c = 1;
    fis.command = ATA_WRITE_DMA_EXT;
    fis.lba0 = lba & 0xFF;
    fis.lba1 = (lba >> 8) & 0xFF;
    fis.lba2 = (lba >> 16) & 0xFF;
    fis.device = 0x40;
    fis.lba3 = (lba >> 24) & 0xFF;
    fis.lba4 = (lba >> 32) & 0xFF;
    fis.lba5 = (lba >> 40) & 0xFF;
    fis.count_low = count & 0xFF;
    fis.count_high = (count >> 8) & 0xFF;

    int ret = ahci_send_fis(ahci_port, &fis, 1, buf_phys, count * AHCI_SECTOR_SIZE);
    pmm_free_pages(buf_phys, (count * AHCI_SECTOR_SIZE + 4095) / 4096);
    return (ret == 0) ? (int)(count * AHCI_SECTOR_SIZE) : -1;
}

int ahci_available(void) { return ahci_initialized; }
void ahci_get_info(uint64_t *sectors) { if (sectors) *sectors = ahci_sector_count; }

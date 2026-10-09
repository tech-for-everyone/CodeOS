#include "nvme.h"
#include "../arch/x86_64/pci.h"
#include "../kernel/pmm.h"
#include "../kernel/vmm.h"
#include "../kernel/string.h"
#include "../kernel/kprintf.h"

/* NVMe 1.x controller registers. This driver deliberately uses polling so
 * it is safe during early boot and does not consume an interrupt vector. */
#define NVME_CAP      0x00
#define NVME_VS       0x08
#define NVME_CC       0x14
#define NVME_CSTS     0x1C
#define NVME_AQA      0x24
#define NVME_ASQ      0x28
#define NVME_ACQ      0x30

#define NVME_CC_EN    (1U << 0)
#define NVME_CSTS_RDY (1U << 0)
#define NVME_CSTS_CFS (1U << 1)

#define NVME_ADMIN_IDENTIFY 0x06
#define NVME_ADMIN_CREATE_CQ 0x05
#define NVME_ADMIN_CREATE_SQ 0x01
#define NVME_IO_WRITE 0x01
#define NVME_IO_READ  0x02

#define NVME_ADMIN_QID 0
#define NVME_IO_QID    1
#define NVME_QDEPTH    16
#define NVME_TIMEOUT   100000

typedef struct {
    uint32_t opc_cid;
    uint32_t nsid;
    uint64_t rsv0[2];
    uint64_t mptr;
    uint64_t prp1;
    uint64_t prp2;
    uint32_t cdw10;
    uint32_t cdw11;
    uint32_t cdw12;
    uint32_t cdw13;
    uint32_t cdw14;
    uint32_t cdw15;
} __attribute__((packed)) nvme_cmd_t;

typedef struct {
    uint32_t result;
    uint32_t rsv;
    uint16_t sq_head;
    uint16_t sq_id;
    uint16_t cid;
    uint16_t status;
} __attribute__((packed)) nvme_cqe_t;

static volatile uint8_t *nvme_regs;
static uint64_t nvme_bar_phys;
static uint64_t nvme_admin_sq_phys, nvme_admin_cq_phys;
static uint64_t nvme_io_sq_phys, nvme_io_cq_phys;
static nvme_cmd_t *nvme_admin_sq, *nvme_io_sq;
static nvme_cqe_t *nvme_admin_cq, *nvme_io_cq;
static uint16_t nvme_admin_tail, nvme_admin_head;
static uint16_t nvme_io_tail, nvme_io_head;
static uint8_t nvme_admin_phase = 1, nvme_io_phase = 1;
static uint16_t nvme_admin_cid, nvme_io_cid;
static uint32_t nvme_db_stride = 4;
static uint64_t nvme_sector_count;
static int nvme_ready;

static uint32_t nvme_read32(uint32_t off) {
    return *(volatile uint32_t *)(nvme_regs + off);
}

static uint64_t nvme_read64(uint32_t off) {
    return *(volatile uint64_t *)(nvme_regs + off);
}

static void nvme_write32(uint32_t off, uint32_t value) {
    *(volatile uint32_t *)(nvme_regs + off) = value;
}

static void nvme_write64(uint32_t off, uint64_t value) {
    *(volatile uint64_t *)(nvme_regs + off) = value;
}

static int nvme_wait_ready(int ready) {
    for (int i = 0; i < NVME_TIMEOUT; i++) {
        uint32_t csts = nvme_read32(NVME_CSTS);
        if (csts & NVME_CSTS_CFS) return -1;
        if (!!(csts & NVME_CSTS_RDY) == ready) return 0;
        if ((i & 255) == 0) asm volatile("pause");
    }
    return -1;
}

static int nvme_wait_cqe(nvme_cqe_t *cq, uint16_t *head, uint8_t *phase,
                         uint16_t cid, uint32_t db_off) {
    for (int i = 0; i < NVME_TIMEOUT; i++) {
        nvme_cqe_t *cqe = &cq[*head];
        if ((cqe->status & 1) == *phase) {
            uint16_t status = cqe->status >> 1;
            *head = (uint16_t)(*head + 1);
            if (*head == NVME_QDEPTH) {
                *head = 0;
                *phase ^= 1;
            }
            nvme_write32(db_off, *head);
            return (cqe->cid == cid && status == 0) ? 0 : -1;
        }
        if ((i & 255) == 0) asm volatile("pause");
    }
    return -1;
}

static int nvme_admin_submit(nvme_cmd_t *cmd) {
    uint16_t cid = nvme_admin_cid++;
    cmd->opc_cid = (cmd->opc_cid & 0xFFFFU) | ((uint32_t)cid << 16);
    nvme_admin_sq[nvme_admin_tail] = *cmd;
    nvme_admin_tail = (uint16_t)((nvme_admin_tail + 1) % NVME_QDEPTH);
    nvme_write32(0x1000 + 2 * nvme_db_stride * NVME_ADMIN_QID, nvme_admin_tail);
    return nvme_wait_cqe(nvme_admin_cq, &nvme_admin_head, &nvme_admin_phase,
                         cid, 0x1000 + (2 * NVME_ADMIN_QID + 1) * nvme_db_stride);
}

static int nvme_io_submit(uint8_t opcode, uint64_t lba, uint16_t count,
                          uint64_t data_phys) {
    nvme_cmd_t *cmd = &nvme_io_sq[nvme_io_tail];
    uint16_t cid = nvme_io_cid++;
    memset(cmd, 0, sizeof(*cmd));
    cmd->opc_cid = opcode | ((uint32_t)cid << 16);
    cmd->nsid = 1;
    cmd->prp1 = data_phys;
    cmd->cdw10 = (uint32_t)lba;
    cmd->cdw11 = (uint32_t)(lba >> 32);
    cmd->cdw12 = (uint32_t)(count - 1);
    nvme_io_tail = (uint16_t)((nvme_io_tail + 1) % NVME_QDEPTH);
    nvme_write32(0x1000 + 2 * nvme_db_stride * NVME_IO_QID, nvme_io_tail);
    return nvme_wait_cqe(nvme_io_cq, &nvme_io_head, &nvme_io_phase,
                         cid, 0x1000 + (2 * NVME_IO_QID + 1) * nvme_db_stride);
}

static int nvme_create_queue(uint8_t opcode, uint64_t cq_phys, uint64_t sq_phys) {
    nvme_cmd_t cmd;
    memset(&cmd, 0, sizeof(cmd));
    cmd.opc_cid = opcode;
    cmd.prp1 = (opcode == NVME_ADMIN_CREATE_CQ) ? cq_phys : sq_phys;
    cmd.cdw10 = (NVME_QDEPTH - 1) | (NVME_IO_QID << 16);
    cmd.cdw11 = (opcode == NVME_ADMIN_CREATE_CQ) ? 1 : (1 << 16) | NVME_IO_QID;
    return nvme_admin_submit(&cmd);
}

static int nvme_identify(uint64_t buf_phys) {
    nvme_cmd_t cmd;
    uint8_t *identify = (uint8_t *)phys_to_virt(buf_phys);
    memset(&cmd, 0, sizeof(cmd));
    cmd.opc_cid = NVME_ADMIN_IDENTIFY;
    cmd.prp1 = buf_phys;
    cmd.cdw10 = 1; /* Identify controller */
    if (nvme_admin_submit(&cmd) < 0) return -1;

    uint32_t nn = *(uint32_t *)(identify + 516);
    if (nn == 0) return -1;
    memset(&cmd, 0, sizeof(cmd));
    cmd.opc_cid = NVME_ADMIN_IDENTIFY;
    cmd.nsid = 1;
    cmd.prp1 = buf_phys;
    cmd.cdw10 = 0; /* Identify namespace 1 */
    if (nvme_admin_submit(&cmd) < 0) return -1;

    nvme_sector_count = *(uint64_t *)identify;
    return nvme_sector_count ? 0 : -1;
}

int nvme_init(void) {
    uint8_t bus, slot, func;
    nvme_ready = 0;
    nvme_sector_count = 0;
    if (!pci_find_class(0x01, 0x08, &bus, &slot, &func)) return 0;
    kprintf("nvme: found controller at %d:%d.%d\n", bus, slot, func);

    uint32_t bar_lo = pci_config_read(bus, slot, func, 0x10);
    uint32_t bar_hi = pci_config_read(bus, slot, func, 0x14);
    if (!(bar_lo & 0x4)) {
        kprintf("nvme: unsupported non-64-bit BAR0 0x%x\n", bar_lo);
        return 0;
    }
    nvme_bar_phys = (bar_lo & ~0xFULL) | ((uint64_t)bar_hi << 32);
    uint32_t pci_cmd = pci_config_read(bus, slot, func, 0x04);
    pci_config_write(bus, slot, func, 0x04, pci_cmd | 0x0006); /* MEM + bus master */
    /* Limine's HHDM does not guarantee mappings for PCI MMIO windows. */
    for (uint64_t off = 0; off < 0x4000; off += PAGE_SIZE)
        vmm_map_page(phys_to_virt(nvme_bar_phys + off), nvme_bar_phys + off,
                     PAGE_PRESENT | PAGE_WRITE | PAGE_NX);
    kprintf("nvme: BAR0 mapped at 0x%llx\n", (unsigned long long)phys_to_virt(nvme_bar_phys));
    nvme_regs = (volatile uint8_t *)(uintptr_t)phys_to_virt(nvme_bar_phys);
    uint64_t cap = nvme_read64(NVME_CAP);
    kprintf("nvme: CAP=0x%llx\n", (unsigned long long)cap);
    nvme_db_stride = 4U << ((cap >> 32) & 0xF);
    if ((cap & 0xFFFF) < NVME_QDEPTH - 1) {
        kprintf("nvme: controller queue limit too small (%u)\n", (unsigned)(cap & 0xFFFF));
        return 0;
    }
    /* QEMU commonly exposes a controller that is already disabled. */
    uint32_t csts = nvme_read32(NVME_CSTS);
    kprintf("nvme: CSTS=0x%x\n", csts);
    if (csts & NVME_CSTS_RDY) {
        nvme_write32(NVME_CC, 0);
        if (nvme_wait_ready(0) < 0) {
            kprintf("nvme: controller failed to disable\n");
            return 0;
        }
    }

    nvme_admin_sq_phys = pmm_alloc_pages(1);
    nvme_admin_cq_phys = pmm_alloc_pages(1);
    nvme_io_sq_phys = pmm_alloc_pages(1);
    nvme_io_cq_phys = pmm_alloc_pages(1);
    if (!nvme_admin_sq_phys || !nvme_admin_cq_phys || !nvme_io_sq_phys || !nvme_io_cq_phys) return 0;
    nvme_admin_sq = (nvme_cmd_t *)phys_to_virt(nvme_admin_sq_phys);
    nvme_admin_cq = (nvme_cqe_t *)phys_to_virt(nvme_admin_cq_phys);
    nvme_io_sq = (nvme_cmd_t *)phys_to_virt(nvme_io_sq_phys);
    nvme_io_cq = (nvme_cqe_t *)phys_to_virt(nvme_io_cq_phys);
    memset(nvme_admin_sq, 0, 4096); memset(nvme_admin_cq, 0, 4096);
    memset(nvme_io_sq, 0, 4096); memset(nvme_io_cq, 0, 4096);
    nvme_admin_tail = nvme_admin_head = nvme_io_tail = nvme_io_head = 0;
    nvme_admin_phase = nvme_io_phase = 1;
    nvme_admin_cid = nvme_io_cid = 1;

    nvme_write32(NVME_AQA, (NVME_QDEPTH - 1) | ((NVME_QDEPTH - 1) << 16));
    nvme_write64(NVME_ASQ, nvme_admin_sq_phys);
    nvme_write64(NVME_ACQ, nvme_admin_cq_phys);
    nvme_write32(NVME_CC, (6 << 16) | (4 << 20) | NVME_CC_EN);
    if (nvme_wait_ready(1) < 0) {
        kprintf("nvme: controller failed to become ready\n");
        return 0;
    }

    uint64_t identify_phys = pmm_alloc_pages(1);
    if (!identify_phys || nvme_identify(identify_phys) < 0 ||
        nvme_create_queue(NVME_ADMIN_CREATE_CQ, nvme_io_cq_phys, 0) < 0 ||
        nvme_create_queue(NVME_ADMIN_CREATE_SQ, 0, nvme_io_sq_phys) < 0) {
        if (identify_phys) pmm_free_pages(identify_phys, 1);
        kprintf("nvme: identify or I/O queue setup failed\n");
        return 0;
    }
    pmm_free_pages(identify_phys, 1);
    nvme_ready = 1;
    kprintf("nvme: controller %d:%d.%d ready, %llu sectors\n", bus, slot, func,
            (unsigned long long)nvme_sector_count);
    return 1;
}

static int nvme_transfer(uint8_t opcode, uint64_t lba, uint32_t count,
                         void *buf) {
    if (!nvme_ready || !buf) return -1;
    while (count) {
        uint16_t n = count > 8 ? 8 : (uint16_t)count;
        uint64_t bounce_phys = pmm_alloc_pages(1);
        if (!bounce_phys) return -1;
        void *bounce = (void *)(uintptr_t)phys_to_virt(bounce_phys);
        size_t bytes = (size_t)n * NVME_SECTOR_SIZE;
        if (opcode == NVME_IO_WRITE) memcpy(bounce, buf, bytes);
        int ret = nvme_io_submit(opcode, lba, n, bounce_phys);
        if (ret == 0 && opcode == NVME_IO_READ) memcpy(buf, bounce, bytes);
        pmm_free_pages(bounce_phys, 1);
        if (ret < 0) return -1;
        lba += n; count -= n; buf = (uint8_t *)buf + bytes;
    }
    return 0;
}

int nvme_read_sectors(uint64_t lba, uint32_t count, void *buf) {
    return nvme_transfer(NVME_IO_READ, lba, count, buf);
}

int nvme_write_sectors(uint64_t lba, uint32_t count, const void *buf) {
    return nvme_transfer(NVME_IO_WRITE, lba, count, (void *)buf);
}

int nvme_available(void) { return nvme_ready; }

void nvme_get_info(uint64_t *sectors) {
    if (sectors) *sectors = nvme_sector_count;
}

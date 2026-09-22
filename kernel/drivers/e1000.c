#include "e1000.h"
#include "nic.h"
#include "../arch/x86_64/io.h"
#include "../arch/x86_64/pci.h"
#include "../kernel/kprintf.h"
#include "../kernel/string.h"
#include "../kernel/vmm.h"
#include "../kernel/idt.h"
#include "timer.h"

#define REG_CTRL      0x0000
#define REG_STATUS    0x0008
#define REG_EECD      0x0010
#define REG_ICR       0x00C0
#define REG_ITR       0x00C4
#define REG_ICS       0x00C8
#define REG_IMS       0x00D0
#define REG_IMC       0x00D8
#define REG_RCTL      0x0100
#define REG_TCTL      0x0400
#define REG_TIPG      0x0410
#define REG_TXDBAL    0x3800
#define REG_TXDBAH    0x3804
#define REG_TXDLEN    0x3808
#define REG_TDH       0x3810
#define REG_TDT       0x3818
#define REG_RXDBAL    0x2800
#define REG_RXDBAH    0x2804
#define REG_RXDLEN    0x2808
#define REG_RDH       0x2810
#define REG_RDT       0x2818
#define REG_MTA       0x5200
#define REG_RAL       0x5400
#define REG_RAH       0x5404

#define CTRL_FD       (1 << 0)
#define CTRL_RST      (1 << 26)
#define CTRL_SLU      (1 << 6)
#define CTRL_ASDE     (1 << 5)
#define STATUS_LU     (1 << 1)
#define RCTL_EN       (1 << 1)
#define RCTL_SBP      (1 << 2)
#define RCTL_UPE      (1 << 3)
#define RCTL_MPE      (1 << 4)
#define RCTL_BAM      (1 << 15)
#define RCTL_BSIZE_2048 (0 << 16)
#define RCTL_SECRC    (1 << 26)
#define TCTL_EN       (1 << 1)
#define TCTL_PSP      (1 << 3)
#define TCTL_CT       (0x0F << 4)
#define TCTL_COLD     (0x3F << 12)
#define CMD_EOP       (1 << 0)
#define CMD_IFCS      (1 << 1)
#define CMD_RS        (1 << 3)
#define TX_DD         (1 << 0)
#define RX_DD         (1 << 0)

#define ICR_TXDW   0x00000001
#define ICR_LSC    0x00000004
#define ICR_RXDMT0 0x00000010

static uint8_t  e1000_bus, e1000_slot, e1000_func;
static uintptr_t e1000_mmio_base;
static uint8_t  e1000_mac[6];
static int      e1000_ok;
static int      tx_cur, rx_cur;
static int      e1000_irq;
static int      e1000_rx_trace;

static volatile struct e1000_tx_desc tx_descs[E1000_NUM_TX] __attribute__((aligned(16)));
static volatile struct e1000_rx_desc rx_descs[E1000_NUM_RX] __attribute__((aligned(16)));
static uint8_t tx_bufs[E1000_NUM_TX][E1000_BUF_LEN] __attribute__((aligned(16)));
static uint8_t rx_bufs[E1000_NUM_RX][E1000_BUF_LEN] __attribute__((aligned(16)));

static uint32_t mmio_read(uint16_t reg) {
    return *(volatile uint32_t *)(uintptr_t)(e1000_mmio_base + reg);
}
static void mmio_write(uint16_t reg, uint32_t val) {
    *(volatile uint32_t *)(uintptr_t)(e1000_mmio_base + reg) = val;
}

static void e1000_irq_handler(int_frame_t *frame) {
    (void)frame;
    uint32_t icr = mmio_read(REG_ICR);
    if (!icr) return;

    if (icr & ICR_LSC) {
        kprintf("e1000: link status changed\n");
    }
    mmio_read(REG_ICR);
}

static int find_e1000(void) {
    static const uint16_t devs[] = {
        E1000_DEV_82540EM, E1000_DEV_82545EM, E1000_DEV_82546EB,
        E1000_DEV_82574L, E1000_DEV_82575EB,
        E1000_DEV_82579LM, E1000_DEV_82579V,
        E1000_DEV_82576, E1000_DEV_82571EB, E1000_DEV_82572EI,
        E1000_DEV_82573E, E1000_DEV_82541PI, E1000_DEV_82541EI,
        E1000_DEV_82547EI
    };
    for (unsigned i = 0; i < sizeof(devs) / sizeof(devs[0]); i++) {
        if (pci_find_device(E1000_VENDOR_INTEL, devs[i],
                            &e1000_bus, &e1000_slot, &e1000_func))
            return 1;
    }
    return 0;
}

int e1000_init(void) {
    if (!find_e1000()) {
        kprintf("e1000: not found\n");
        return -1;
    }

    uint32_t bar0 = pci_config_read(e1000_bus, e1000_slot, e1000_func, 0x10);
    e1000_mmio_base = (uintptr_t)phys_to_virt(bar0 & ~0xF);

    pci_config_write(e1000_bus, e1000_slot, e1000_func, 0x04, 0x0006 | 0x0004);

    {
        uint32_t ral = mmio_read(REG_RAL);
        uint32_t rah = mmio_read(REG_RAH);
        if (ral || rah) {
            e1000_mac[0] = ral & 0xFF;
            e1000_mac[1] = (ral >> 8) & 0xFF;
            e1000_mac[2] = (ral >> 16) & 0xFF;
            e1000_mac[3] = (ral >> 24) & 0xFF;
            e1000_mac[4] = rah & 0xFF;
            e1000_mac[5] = (rah >> 8) & 0xFF;
        } else {
            e1000_mac[0] = 0x52; e1000_mac[1] = 0x54;
            e1000_mac[2] = 0x00; e1000_mac[3] = 0x12;
            e1000_mac[4] = 0x34; e1000_mac[5] = 0x56;
        }
    }

    mmio_write(REG_CTRL, mmio_read(REG_CTRL) | CTRL_RST);
    for (volatile int i = 0; i < 100000; i++) asm volatile("pause");

    mmio_write(REG_CTRL, mmio_read(REG_CTRL) | CTRL_SLU | CTRL_ASDE | CTRL_FD);

    /* VirtualBox's 82540EM models PHY autoneg with a ~5s LinkUpDelay: the
     * device refuses to enqueue TX descriptors (STATUS.LU=0, TDH frozen) until
     * the link is genuinely up.  Wait a bounded wall-clock window for LU. */
    {
        uint64_t link_deadline = timer_get_milliseconds() + 6000;
        while (timer_get_milliseconds() < link_deadline) {
            if (mmio_read(REG_STATUS) & STATUS_LU) break;
            timer_sleep_ms(50);
        }
    }
    kprintf("e1000: link %s\n",
            mmio_read(REG_STATUS) & STATUS_LU ? "UP" : "DOWN (forcing)");

    mmio_write(REG_RAL, e1000_mac[0] | (e1000_mac[1] << 8) |
               (e1000_mac[2] << 16) | (e1000_mac[3] << 24));
    mmio_write(REG_RAH, e1000_mac[4] | (e1000_mac[5] << 8) | (1 << 31));

    /* Set up RX descriptors */
    for (int i = 0; i < E1000_NUM_RX; i++) {
        memset((void *)rx_bufs[i], 0, E1000_BUF_LEN);
        rx_descs[i].addr = virt_to_phys((uintptr_t)rx_bufs[i]);
        rx_descs[i].status = 0;
    }
    mmio_write(REG_RXDBAL, (uint32_t)virt_to_phys((uintptr_t)rx_descs));
    mmio_write(REG_RXDBAH, 0);
    mmio_write(REG_RXDLEN, E1000_NUM_RX * (int)sizeof(struct e1000_rx_desc));
    mmio_write(REG_RDH, 0);
    mmio_write(REG_RDT, E1000_NUM_RX - 1);

    mmio_write(REG_RCTL, RCTL_EN | RCTL_SBP | RCTL_UPE | RCTL_MPE |
               RCTL_BAM | RCTL_BSIZE_2048 | RCTL_SECRC);

    /* Set up TX descriptors */
    for (int i = 0; i < E1000_NUM_TX; i++) {
        memset((void *)tx_bufs[i], 0, E1000_BUF_LEN);
        tx_descs[i].addr = virt_to_phys((uintptr_t)tx_bufs[i]);
        tx_descs[i].cmd = 0;
        tx_descs[i].status = TX_DD;
    }
    mmio_write(REG_TXDBAL, (uint32_t)virt_to_phys((uintptr_t)tx_descs));
    mmio_write(REG_TXDBAH, 0);
    mmio_write(REG_TXDLEN, E1000_NUM_TX * (int)sizeof(struct e1000_tx_desc));
    mmio_write(REG_TDH, 0);
    mmio_write(REG_TDT, 0);

    mmio_write(REG_TCTL, TCTL_EN | TCTL_PSP | TCTL_CT | TCTL_COLD);
    mmio_write(REG_TIPG, 0x0060200A);

    /* Setup interrupts: prefer MSI now that the local APIC is configured. */
    e1000_irq = -1;
    if (pci_msi_enable(e1000_bus, e1000_slot, e1000_func, 0x20 + 12, 0xFEE00000) == 0) {
        e1000_irq = 12;
        kprintf("e1000: using MSI IRQ %d\n", e1000_irq);
    } else {
        e1000_irq = pci_config_read(e1000_bus, e1000_slot, e1000_func, 0x3C) & 0xFF;
        kprintf("e1000: using INTx IRQ %d\n", e1000_irq);
    }

    if (e1000_irq >= 0 && e1000_irq < 16)
        irq_register(e1000_irq, e1000_irq_handler);

    /* The driver is poll-driven (it checks TX_DD / RX_DD on the descriptors
     * directly), so no device interrupts are unmasked here. MSI is still
     * configured so the vector is claimed and future interrupt-driven modes
     * can enable IMS. Clear pending causes first to leave a clean state. */
    mmio_read(REG_ICR);
    mmio_write(REG_IMS, 0);
    mmio_read(REG_ICR);

    tx_cur = 0;
    rx_cur = 0;
    e1000_ok = 1;

    kprintf("e1000: NIC at %02x:%02x.%x BAR0=0x%x MAC=%02x:%02x:%02x:%02x:%02x:%02x\n",
            e1000_bus, e1000_slot, e1000_func, bar0 & ~0xF,
            e1000_mac[0], e1000_mac[1], e1000_mac[2],
            e1000_mac[3], e1000_mac[4], e1000_mac[5]);
    return 0;
}

int e1000_send(const void *data, int len) {
    if (!e1000_ok || !data || len <= 0) return -1;
    if (len > E1000_BUF_LEN) len = E1000_BUF_LEN;

    /* VBox: TX only runs once the emulated PHY reports link up (<= ~5s). */
    if (!(mmio_read(REG_STATUS) & STATUS_LU)) {
        uint64_t lu_deadline = timer_get_milliseconds() + 6000;
        while (timer_get_milliseconds() < lu_deadline) {
            if (mmio_read(REG_STATUS) & STATUS_LU) break;
            timer_sleep_ms(50);
        }
        if (!(mmio_read(REG_STATUS) & STATUS_LU)) return -1;
    }

    int desc = tx_cur;
    if (!(tx_descs[desc].status & TX_DD)) {
        uint64_t deadline = timer_get_milliseconds() + 500;
        while (timer_get_milliseconds() < deadline) {
            if (tx_descs[desc].status & TX_DD) break;
            timer_sleep_ms(1);
        }
        if (!(tx_descs[desc].status & TX_DD)) return -1;
    }

    memcpy((void *)tx_bufs[desc], data, len);
    tx_descs[desc].length = (uint16_t)len;
    tx_descs[desc].cmd = CMD_EOP | CMD_IFCS | CMD_RS;
    tx_descs[desc].status = 0;

    tx_cur = (tx_cur + 1) % E1000_NUM_TX;
    __asm__ volatile("mfence" ::: "memory");
    mmio_write(REG_TDT, tx_cur);

    {
        uint64_t deadline = timer_get_milliseconds() + 500;
        while (timer_get_milliseconds() < deadline) {
            if (tx_descs[desc].status & TX_DD) break;
            timer_sleep_ms(1);
        }
    }

    if (!(tx_descs[desc].status & TX_DD)) {
        kprintf("e1000: TX_DD timeout desc=%d status=0x%x TDT=0x%x TDH=0x%x TCTL=0x%x\n",
                desc, tx_descs[desc].status,
                mmio_read(REG_TDT), mmio_read(REG_TDH), mmio_read(REG_TCTL));
        return -1;
    }
    return len;
}

int e1000_recv(void *buf, int max_len) {
    if (!e1000_ok || !buf || max_len <= 0) return -1;

    int desc = rx_cur;
    if (!(rx_descs[desc].status & RX_DD)) return 0;

    int len = rx_descs[desc].length;
    if (len > max_len) len = max_len;
    if (e1000_rx_trace && len >= 14) {
        const uint8_t *f = (const uint8_t *)rx_bufs[desc];
        kprintf("e1000 rx: dst=%02x:%02x:%02x:%02x:%02x:%02x src=%02x:%02x:%02x:%02x:%02x:%02x type=%04x\n",
                f[0],f[1],f[2],f[3],f[4],f[5], f[6],f[7],f[8],f[9],f[10],f[11],
                (f[12]<<8)|f[13]);
    }
    memcpy(buf, (void *)rx_bufs[desc], len);

    rx_descs[desc].status = 0;
    rx_cur = (rx_cur + 1) % E1000_NUM_RX;
    mmio_write(REG_RDT, rx_cur == 0 ? E1000_NUM_RX - 1 : rx_cur - 1);

    return len;
}

void e1000_get_mac(uint8_t *mac) {
    memcpy(mac, e1000_mac, 6);
}

nic_driver_t e1000_nic = {
    .name = "e1000",
    .probe = e1000_init,
    .send = e1000_send,
    .recv = e1000_recv,
    .get_mac = e1000_get_mac,
};

int e1000_dbg_rx_status(int idx) {
    return rx_descs[idx].status;
}

void e1000_dbg_set_rx_trace(int on) {
    e1000_rx_trace = on;
}

void e1000_dbg_clear_stats(void) {
    e1000_rx_trace = 0;
}

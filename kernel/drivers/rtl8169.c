#include "rtl8169.h"
#include "nic.h"
#include "../arch/x86_64/io.h"
#include "../arch/x86_64/pci.h"
#include "../kernel/kprintf.h"
#include "../kernel/string.h"
#include "../kernel/vmm.h"


static uint8_t  gig_bus, gig_slot, gig_func;
static uintptr_t gig_mmio_base;
static uint8_t  gig_mac[6];
static int      gig_ok;

static volatile struct rtl8169_desc tx_descs[RTL_GIG_NUM_TX] __attribute__((aligned(16)));
static volatile struct rtl8169_desc rx_descs[RTL_GIG_NUM_RX] __attribute__((aligned(16)));
static uint8_t tx_bufs[RTL_GIG_NUM_TX][RTL_GIG_BUF_LEN] __attribute__((aligned(16)));
static uint8_t rx_bufs[RTL_GIG_NUM_RX][RTL_GIG_BUF_LEN] __attribute__((aligned(16)));
static int tx_cur, rx_cur;

static uint32_t gig_mmio_read(uint16_t reg) {
    return *(volatile uint32_t *)(uintptr_t)(gig_mmio_base + reg);
}

static void gig_mmio_write(uint16_t reg, uint32_t val) {
    *(volatile uint32_t *)(uintptr_t)(gig_mmio_base + reg) = val;
}

static uint8_t gig_mmio_readb(uint16_t reg) {
    return *(volatile uint8_t *)(uintptr_t)(gig_mmio_base + reg);
}

static void gig_mmio_writeb(uint16_t reg, uint8_t val) {
    *(volatile uint8_t *)(uintptr_t)(gig_mmio_base + reg) = val;
}

static uint16_t gig_mmio_readw(uint16_t reg) {
    return *(volatile uint16_t *)(uintptr_t)(gig_mmio_base + reg);
}

static void gig_mmio_writew(uint16_t reg, uint16_t val) {
    *(volatile uint16_t *)(uintptr_t)(gig_mmio_base + reg) = val;
}

static int rtl8169_probe(void) {
    static const uint16_t devs[] = {
        RTL8169_DEV_8169, RTL8169_DEV_8168,
        RTL8169_DEV_8101, RTL8169_DEV_8125
    };
    int found = 0;
    for (unsigned i = 0; i < sizeof(devs) / sizeof(devs[0]); i++) {
        if (pci_find_device(RTL8169_VENDOR, devs[i],
                            &gig_bus, &gig_slot, &gig_func)) {
            found = 1;
            break;
        }
    }
    if (!found) return -1;

    uint32_t bar0 = pci_config_read(gig_bus, gig_slot, gig_func, 0x10);
    gig_mmio_base = (uintptr_t)phys_to_virt(bar0 & ~0xF);

    pci_config_write(gig_bus, gig_slot, gig_func, 0x04, 0x0006);

    /* Read MAC address */
    uint32_t mac_low = gig_mmio_read(RTL_GIG_REG_IDR0);
    uint16_t mac_high = gig_mmio_readw(RTL_GIG_REG_IDR4);
    gig_mac[0] = mac_low & 0xFF;
    gig_mac[1] = (mac_low >> 8) & 0xFF;
    gig_mac[2] = (mac_low >> 16) & 0xFF;
    gig_mac[3] = (mac_low >> 24) & 0xFF;
    gig_mac[4] = mac_high & 0xFF;
    gig_mac[5] = (mac_high >> 8) & 0xFF;

    /* Software reset */
    gig_mmio_writeb(RTL_GIG_REG_CMD, RTL_GIG_CMD_RESET);
    for (volatile int i = 0; i < 1000000; i++) {
        if (!(gig_mmio_readb(RTL_GIG_REG_CMD) & RTL_GIG_CMD_RESET)) break;
    }

    /* Unlock config registers */
    gig_mmio_writeb(0x51, 0xC0);

    /* Set C+ Command: disable RX/TX checksum offload for simplicity */
    gig_mmio_writew(RTL_GIG_REG_CPLUS_CMD, 0);

    /* Set TX config: max DMA burst 1024, interframe gap */
    gig_mmio_write(RTL_GIG_REG_TCR, 0x03000700);

    /* Set RX config: max RX buffer size, no overflow */
    gig_mmio_write(RTL_GIG_REG_RCR, RTL_GIG_RCR_AAP | RTL_GIG_RCR_APM |
                   RTL_GIG_RCR_AM | RTL_GIG_RCR_AB);

    /* Setup RX descriptors */
    for (int i = 0; i < RTL_GIG_NUM_RX; i++) {
        memset((void *)rx_bufs[i], 0, RTL_GIG_BUF_LEN);
        rx_descs[i].status = RTL_GIG_RX_OWN;
        rx_descs[i].addr_low = (uint32_t)virt_to_phys((uintptr_t)rx_bufs[i]);
        rx_descs[i].addr_high = 0;
        rx_descs[i].vlan = 0;
        if (i == RTL_GIG_NUM_RX - 1)
            rx_descs[i].status |= RTL_GIG_RX_EOR;
    }
    gig_mmio_write(RTL_GIG_REG_RBSTART, (uint32_t)virt_to_phys((uintptr_t)rx_descs));

    /* Setup TX descriptors */
    for (int i = 0; i < RTL_GIG_NUM_TX; i++) {
        memset((void *)tx_bufs[i], 0, RTL_GIG_BUF_LEN);
        tx_descs[i].status = 0;
        tx_descs[i].addr_low = (uint32_t)virt_to_phys((uintptr_t)tx_bufs[i]);
        tx_descs[i].addr_high = 0;
        if (i == RTL_GIG_NUM_TX - 1)
            tx_descs[i].status |= RTL_GIG_TX_EOR;
    }
    gig_mmio_write(RTL_GIG_REG_TXADDR, (uint32_t)virt_to_phys((uintptr_t)tx_descs));

    /* Enable TX/RX */
    gig_mmio_writeb(RTL_GIG_REG_CMD, RTL_GIG_CMD_RE | RTL_GIG_CMD_TE);

    tx_cur = 0;
    rx_cur = 0;
    gig_ok = 1;

    kprintf("rtl8169: at %02x:%02x.%x MMIO=0x%x MAC=%02x:%02x:%02x:%02x:%02x:%02x\n",
            gig_bus, gig_slot, gig_func, gig_mmio_base,
            gig_mac[0], gig_mac[1], gig_mac[2],
            gig_mac[3], gig_mac[4], gig_mac[5]);
    return 0;
}

static int rtl8169_send(const void *data, int len) {
    if (!gig_ok) return -1;
    if (!data) return -1;
    if (len <= 0) return -1;
    if (len > RTL_GIG_BUF_LEN) len = RTL_GIG_BUF_LEN;

    int desc = tx_cur;
    if (tx_descs[desc].status & RTL_GIG_TX_OWN) return -1;

    memcpy((void *)tx_bufs[desc], data, len);

    tx_descs[desc].status = RTL_GIG_TX_OWN | RTL_GIG_TX_FS | RTL_GIG_TX_LS |
                            (len & RTL_GIG_TX_LEN_MASK);

    tx_cur = (tx_cur + 1) % RTL_GIG_NUM_TX;

    for (volatile int i = 0; i < 100000; i++) {
        if (!(tx_descs[desc].status & RTL_GIG_TX_OWN)) break;
    }

    return (tx_descs[desc].status & RTL_GIG_TX_OWN) ? -1 : len;
}

static int rtl8169_recv(void *buf, int max_len) {
    if (!gig_ok) return -1;
    if (!buf) return -1;
    if (max_len <= 0) return -1;

    int desc = rx_cur;
    if (rx_descs[desc].status & RTL_GIG_RX_OWN) return -1;

    int len = rx_descs[desc].status & RTL_GIG_RX_LEN_MASK;
    if (len < 4) return -1;
    len -= 4;

    if (len > max_len) len = max_len;
    memcpy(buf, (void *)rx_bufs[desc], len);

    rx_descs[desc].status = RTL_GIG_RX_OWN;
    if (desc == RTL_GIG_NUM_RX - 1)
        rx_descs[desc].status |= RTL_GIG_RX_EOR;

    rx_cur = (rx_cur + 1) % RTL_GIG_NUM_RX;

    return len;
}

static void rtl8169_get_mac(uint8_t *mac) {
    memcpy(mac, gig_mac, 6);
}

nic_driver_t rtl8169_nic = {
    .name = "rtl8169",
    .probe = rtl8169_probe,
    .send  = rtl8169_send,
    .recv  = rtl8169_recv,
    .get_mac = rtl8169_get_mac,
};

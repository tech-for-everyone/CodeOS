#include "rtl8139.h"
#include "nic.h"
#include "../arch/x86_64/io.h"
#include "../arch/x86_64/pci.h"
#include "../kernel/kprintf.h"
#include "../kernel/string.h"
#include "../kernel/pmm.h"
#include "../kernel/vmm.h"


static uint8_t  rtl_bus, rtl_slot, rtl_func;
static uint16_t rtl_io_base;
static uint8_t  rtl_mac[6];
static int      rtl_ok;

static uint8_t tx_bufs[RTL_NUM_TX][RTL_BUF_LEN] __attribute__((aligned(16)));
static int      tx_cur;

static uint8_t *rx_ring;
static int      rx_offset;

static uint8_t inb_reg(uint16_t reg) {
    return inb(rtl_io_base + reg);
}

static uint16_t inw_reg(uint16_t reg) {
    return inw(rtl_io_base + reg);
}

static uint32_t inl_reg(uint16_t reg) {
    return inl(rtl_io_base + reg);
}

static void outb_reg(uint16_t reg, uint8_t val) {
    outb(rtl_io_base + reg, val);
}

static void outw_reg(uint16_t reg, uint16_t val) {
    outw(rtl_io_base + reg, val);
}

static void outl_reg(uint16_t reg, uint32_t val) {
    outl(rtl_io_base + reg, val);
}

static void rtl_reset(void) {
    outb_reg(RTL_REG_CR, RTL_CR_RST);
    for (volatile int i = 0; i < 1000000; i++) {
        if (!(inb_reg(RTL_REG_CR) & RTL_CR_RST)) break;
    }
}

static int rtl8139_probe(void) {
    if (!pci_find_device(RTL8139_VENDOR, RTL8139_DEVICE,
                         &rtl_bus, &rtl_slot, &rtl_func)) {
        return -1;
    }

    uint32_t bar0 = pci_config_read(rtl_bus, rtl_slot, rtl_func, 0x10);
    if (!(bar0 & 1)) return -1;
    rtl_io_base = bar0 & ~0x3;

    /* PCI command: I/O space | Memory space | Bus master.
       Bus mastering (bit 2) is REQUIRED — without it the NIC cannot DMA
       RX packets into the ring (and TX descriptors never complete). */
    pci_config_write(rtl_bus, rtl_slot, rtl_func, 0x04, 0x0007);

    /* Read MAC address */
    uint32_t mac_low = inl_reg(RTL_REG_IDR0);
    uint16_t mac_high = inw_reg(RTL_REG_IDR4);
    rtl_mac[0] = mac_low & 0xFF;
    rtl_mac[1] = (mac_low >> 8) & 0xFF;
    rtl_mac[2] = (mac_low >> 16) & 0xFF;
    rtl_mac[3] = (mac_low >> 24) & 0xFF;
    rtl_mac[4] = mac_high & 0xFF;
    rtl_mac[5] = (mac_high >> 8) & 0xFF;

    rtl_reset();

    /* Allocate RX ring (page-aligned, physically contiguous) */
    void *rx_phys = (void*)pmm_alloc_pages(1);
    if (!rx_phys) return -1;
    rx_ring = (uint8_t *)phys_to_virt((uint64_t)rx_phys);
    memset(rx_ring, 0, RTL_RX_BUF_SIZE);

    /* Set RX buffer address (physical address for DMA) */
    outl_reg(RTL_REG_RBSTART, (uint32_t)(uint64_t)rx_phys);

    /* Enable receiver and transmitter */
    outb_reg(RTL_REG_CR, RTL_CR_RE | RTL_CR_TE);

    /* Accept all packets (promiscuous + broadcast) */
    /* DEBUG: accept error/runt frames too so we can see what arrives */
    outl_reg(RTL_REG_RCR, RTL_RCR_AAP | RTL_RCR_APM | RTL_RCR_AM |
                          RTL_RCR_AB | RTL_RCR_WRAP | 0x30);

    /* Clear any pending interrupts */
    outw_reg(RTL_REG_ISR, 0xFFFF);

    tx_cur = 0;
    rx_offset = 0;
    rtl_ok = 1;

    kprintf("rtl8139: at %02x:%02x.%x IO=0x%x MAC=%02x:%02x:%02x:%02x:%02x:%02x\n",
            rtl_bus, rtl_slot, rtl_func, rtl_io_base,
            rtl_mac[0], rtl_mac[1], rtl_mac[2],
            rtl_mac[3], rtl_mac[4], rtl_mac[5]);
    return 0;
}

static int rtl8139_send(const void *data, int len) {
    if (!rtl_ok) return -1;
    if (!data || len <= 0) return -1;
    if (len > RTL_BUF_LEN) len = RTL_BUF_LEN;

    int desc = tx_cur;
    uint32_t tsd = inl_reg(RTL_REG_TXSTATUS0 + desc * 4);
    (void)tsd;

    memcpy(tx_bufs[desc], data, len);

    outl_reg(RTL_REG_TXADDR0 + desc * 4,
             (uint32_t)virt_to_phys((uintptr_t)tx_bufs[desc]));
    /* QEMU rtl8139 semantics: writing TSD with the size starts the
       transmit and clears bit13; hardware sets bit13 again when the
       frame is out.  Reset default is 0x2000 (idle/done). */
    outl_reg(RTL_REG_TXSTATUS0 + desc * 4, (uint32_t)(len & 0x1FFF));

    tx_cur = (tx_cur + 1) % RTL_NUM_TX;

    for (volatile int i = 0; i < 5000000; i++) {
        tsd = inl_reg(RTL_REG_TXSTATUS0 + desc * 4);
        if (tsd & (1u << 13)) return len;   /* completed */
    }
    return -1;
}

static int rtl8139_recv(void *buf, int max_len) {
    if (!rtl_ok) return -1;
    if (!buf) return -1;
    if (max_len <= 0) return -1;

    if (rx_offset >= RTL_RX_BUF_SIZE) rx_offset = 0;

    uint16_t capr = inw_reg(RTL_REG_CAPR);
    uint16_t cbr  = inw_reg(RTL_REG_CBR);

    if (cbr == capr) return -1;

    uint16_t rx_status = *(volatile uint16_t *)(rx_ring + rx_offset);
    uint16_t rx_len    = *(volatile uint16_t *)(rx_ring + rx_offset + 2);

    if (!(rx_status & RTL_RX_OK)) {
        rx_offset = (rx_offset + rx_len + 4 + 3) & ~3;
        if (rx_offset >= RTL_RX_BUF_SIZE) rx_offset -= RTL_RX_BUF_SIZE;
        if (rx_offset >= 16)
            outw_reg(RTL_REG_CAPR, rx_offset - 16);
        return -1;
    }

    rx_len &= 0x3FFF;
    if (rx_len < 4) return -1;
    rx_len -= 4;
    if (rx_len > max_len)
        rx_len = max_len;

    int data_off = rx_offset + 4;
    for (int i = 0; i < rx_len; i++) {
        ((uint8_t *)buf)[i] = *(volatile uint8_t *)(rx_ring + (data_off + i) % RTL_RX_BUF_SIZE);
    }

    rx_offset = (rx_offset + rx_len + 4 + 3) & ~3;
    if (rx_offset >= RTL_RX_BUF_SIZE) rx_offset -= RTL_RX_BUF_SIZE;

    if (rx_offset >= 16)
        outw_reg(RTL_REG_CAPR, rx_offset - 16);
    else
        outw_reg(RTL_REG_CAPR, 0);

    return rx_len;
}

static void rtl8139_get_mac(uint8_t *mac) {
    memcpy(mac, rtl_mac, 6);
}

nic_driver_t rtl8139_nic = {
    .name = "rtl8139",
    .probe = rtl8139_probe,
    .send  = rtl8139_send,
    .recv  = rtl8139_recv,
    .get_mac = rtl8139_get_mac,
};

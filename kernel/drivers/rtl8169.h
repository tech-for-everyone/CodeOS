#ifndef RTL8169_H
#define RTL8169_H

#include "types.h"
#include "nic.h"

#define RTL8169_VENDOR 0x10EC
#define RTL8169_DEV_8169 0x8169
#define RTL8169_DEV_8168 0x8168
#define RTL8169_DEV_8101 0x8101
#define RTL8169_DEV_8125 0x8125

/* MMIO register offsets */
#define RTL_GIG_REG_IDR0      0x00
#define RTL_GIG_REG_IDR4      0x04
#define RTL_GIG_REG_TXSTATUS  0x10
#define RTL_GIG_REG_TXADDR    0x20
#define RTL_GIG_REG_RXSTATUS  0x30
#define RTL_GIG_REG_RBSTART   0x40
#define RTL_GIG_REG_CMD       0x48
#define RTL_GIG_REG_IMR       0x50
#define RTL_GIG_REG_ISR       0x52
#define RTL_GIG_REG_TCR       0x60
#define RTL_GIG_REG_RCR       0x68
#define RTL_GIG_REG_CONFIG1   0x7C
#define RTL_GIG_REG_CONFIG3   0x7E
#define RTL_GIG_REG_CONFIG5   0x82
#define RTL_GIG_REG_PMCH      0x8C
#define RTL_GIG_REG_CPLUS_CMD 0x88
#define RTL_GIG_REG_INTR_MIT  0xE2
#define RTL_GIG_REG_RX_CONFIG 0xD0
#define RTL_GIG_REG_TX_CONFIG 0xD4
#define RTL_GIG_REG_EPHY_TX   0xA0
#define RTL_GIG_REG_EPHY_RX   0xA4

#define RTL_GIG_CMD_RESET   0x10
#define RTL_GIG_CMD_RE      0x08
#define RTL_GIG_CMD_TE      0x04

#define RTL_GIG_RCR_AAP     0x01
#define RTL_GIG_RCR_APM     0x02
#define RTL_GIG_RCR_AM      0x04
#define RTL_GIG_RCR_AB      0x08

#define RTL_GIG_NUM_TX  8
#define RTL_GIG_NUM_RX  8
#define RTL_GIG_BUF_LEN 2048

#define RTL_GIG_TX_OWN  (1 << 31)
#define RTL_GIG_TX_EOR  (1 << 30)
#define RTL_GIG_TX_FS   (1 << 29)
#define RTL_GIG_TX_LS   (1 << 28)
#define RTL_GIG_TX_LEN_MASK 0x1FFF

#define RTL_GIG_RX_OWN  (1 << 31)
#define RTL_GIG_RX_EOR  (1 << 30)
#define RTL_GIG_RX_LEN_MASK 0x3FFF

struct rtl8169_desc {
    uint32_t status;
    uint32_t vlan;
    uint32_t addr_low;
    uint32_t addr_high;
} __attribute__((packed));

extern nic_driver_t rtl8169_nic;

#endif

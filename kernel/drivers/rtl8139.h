#ifndef RTL8139_H
#define RTL8139_H

#include "types.h"
#include "nic.h"

#define RTL8139_VENDOR 0x10EC
#define RTL8139_DEVICE 0x8139

/* I/O port offsets from BAR0 */
#define RTL_REG_IDR0      0x00
#define RTL_REG_IDR4      0x04
#define RTL_REG_MAR0      0x08
#define RTL_REG_TXSTATUS0 0x10
#define RTL_REG_TXSTATUS1 0x14
#define RTL_REG_TXSTATUS2 0x18
#define RTL_REG_TXSTATUS3 0x1C
#define RTL_REG_TXADDR0   0x20
#define RTL_REG_TXADDR1   0x24
#define RTL_REG_TXADDR2   0x28
#define RTL_REG_TXADDR3   0x2C
#define RTL_REG_RBSTART   0x30
#define RTL_REG_ERBCR     0x34
#define RTL_REG_CR        0x37
#define RTL_REG_CAPR      0x38
#define RTL_REG_CBR       0x3A
#define RTL_REG_IMR       0x3C
#define RTL_REG_ISR       0x3E
#define RTL_REG_TCR       0x40
#define RTL_REG_RCR       0x44
#define RTL_REG_CONFIG1   0x52
#define RTL_REG_MSR       0x58

#define RTL_CR_RST   0x10
#define RTL_CR_RE    0x08
#define RTL_CR_TE    0x04
#define RTL_CR_BUFE  0x01

#define RTL_RCR_AAP  0x01
#define RTL_RCR_APM  0x02
#define RTL_RCR_AM   0x04
#define RTL_RCR_AB   0x08
#define RTL_RCR_WRAP 0x80

#define RTL_NUM_TX  4
#define RTL_BUF_LEN 2048
#define RTL_RX_BUF_SIZE (RTL_BUF_LEN * 2 + 16)

#define RTL_TX_CLEAR 0x00002000
#define RTL_TX_HOST_OWN (1 << 13)
#define RTL_TX_OWN (1 << 13)
#define RTL_TX_EOR (1 << 14)
#define RTL_TX_FS (1 << 15)
#define RTL_TX_LS (1 << 15)

#define RTL_RX_OK (1 << 0)
#define RTL_RX_BUF_FULL (1 << 1)
#define RTL_RX_CRC_ERR (1 << 2)
#define RTL_RX_BAD_ALIGN (1 << 6)

extern nic_driver_t rtl8139_nic;

#endif

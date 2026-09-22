#ifndef E1000_H
#define E1000_H

#include "types.h"

#define E1000_VENDOR_INTEL   0x8086
#define E1000_DEV_82540EM    0x100E
#define E1000_DEV_82545EM    0x100F
#define E1000_DEV_82546EB    0x1010
#define E1000_DEV_82541PI    0x1076
#define E1000_DEV_82541EI    0x1013
#define E1000_DEV_82547EI    0x1019
#define E1000_DEV_82571EB    0x105E
#define E1000_DEV_82572EI    0x107D
#define E1000_DEV_82573E     0x108B
#define E1000_DEV_82574L     0x10D3
#define E1000_DEV_82575EB    0x10A7
#define E1000_DEV_82576      0x10C9
#define E1000_DEV_82579LM    0x1502
#define E1000_DEV_82579V     0x1503
#define E1000_DEV_I217_LM    0x120A
#define E1000_DEV_I217_V     0x120B
#define E1000_DEV_I218_LM    0x155A
#define E1000_DEV_I218_V     0x1559
#define E1000_DEV_I219_LM    0x156F
#define E1000_DEV_I219_V     0x1570

struct e1000_tx_desc {
    uint64_t addr;
    uint16_t length;
    uint8_t  cso;
    uint8_t  cmd;
    uint8_t  status;
    uint8_t  css;
    uint16_t special;
} __attribute__((packed));

struct e1000_rx_desc {
    uint64_t addr;
    uint16_t length;
    uint16_t csum;
    uint8_t  status;
    uint8_t  errors;
    uint16_t special;
} __attribute__((packed));

#define E1000_NUM_TX  32
#define E1000_NUM_RX  64
#define E1000_BUF_LEN 2048

int e1000_init(void);
int e1000_send(const void *data, int len);
int e1000_recv(void *buf, int max_len);
void e1000_dbg_set_rx_trace(int on);
void e1000_get_mac(uint8_t *mac);

#include "nic.h"
extern nic_driver_t e1000_nic;

#endif

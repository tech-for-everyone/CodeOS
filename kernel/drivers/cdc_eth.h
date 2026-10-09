#ifndef CDC_ETH_H
#define CDC_ETH_H

#include "types.h"
#include "nic.h"

#define CDC_ETH_VENDOR_ASIX  0x0B95
#define CDC_ETH_VENDOR_MOSCHIP 0x9710
#define CDC_ETH_VENDOR_SMSC  0x0424
#define CDC_ETH_VENDOR_REALTEK 0x0BDA

int  cdc_eth_init(void);

extern nic_driver_t cdc_eth_nic;

#endif

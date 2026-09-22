#ifndef FW_CFG_H
#define FW_CFG_H

#include "types.h"

#define FW_CFG_CTL   0x09020008UL  /* selector register */
#define FW_CFG_DATA  0x09020000UL  /* data register */
#define FW_CFG_DMA   0x09020010UL  /* DMA register (guest addr of access struct) */

int  fw_cfg_init(void);
int  fw_cfg_available(void);
void fw_cfg_read_bytes(uint16_t sel, uint8_t *buf, uint32_t len);
int  fw_cfg_find_file(const char *name, uint16_t *sel_out, uint32_t *size_out);
int  fw_cfg_dma_write(uint16_t sel, const void *data, uint32_t len);

#endif

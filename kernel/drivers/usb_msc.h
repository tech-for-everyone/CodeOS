#ifndef USB_MSC_H
#define USB_MSC_H

#include "types.h"

int  usb_msc_init(void);
int  usb_msc_read_sectors(uint32_t lba, uint32_t count, void *buf);
int  usb_msc_write_sectors(uint32_t lba, uint32_t count, const void *buf);
int  usb_msc_get_sector_size(void);
uint32_t usb_msc_get_sector_count(void);
int  usb_msc_available(void);

#endif

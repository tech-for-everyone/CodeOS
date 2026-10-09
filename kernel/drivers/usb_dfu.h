#ifndef USB_DFU_H
#define USB_DFU_H

#include "types.h"

/* USB DFU (Device Firmware Upgrade) — USB firmware update support */

#define DFU_MAX_DEVICES  4

/* DFU class request codes */
#define DFU_DETACH      0x00
#define DFU_DNLOAD      0x01
#define DFU_UPLOAD      0x02
#define DFU_GETSTATUS   0x03
#define DFU_CLRSTATUS   0x04
#define DFU_GETSTATE    0x05
#define DFU_ABORT       0x06

/* DFU device states */
#define DFU_STATE_APP_IDLE            0x00
#define DFU_STATE_APP_DETACH          0x01
#define DFU_STATE_DFU_IDLE            0x02
#define DFU_STATE_DFU_DNLOAD_SYNC     0x03
#define DFU_STATE_DFU_DNBUSY          0x04
#define DFU_STATE_DFU_DNLOAD_IDLE     0x05
#define DFU_STATE_DFU_MANIFEST_SYNC   0x06
#define DFU_STATE_DFU_MANIFEST        0x07
#define DFU_STATE_DFU_MANIFEST_WAIT_RESET 0x08
#define DFU_STATE_DFU_UPLOAD_IDLE     0x09
#define DFU_STATE_DFU_ERROR           0x0A

struct dfu_device {
    struct usb_dev *dev;
    int  open;
    int  slot;
    int  state;
    int  interface;
    int  alt_setting;
    int  can_download;
    int  can_upload;
    int  detached;
    char name[128];
};

int  usb_dfu_init(void);
int  usb_dfu_available(void);
void usb_dfu_print_info(void);

int  usb_dfu_get_device_count(void);
int  usb_dfu_detach(int dev);
int  usb_dfu_download(int dev, int alt, const void *data, int len);
int  usb_dfu_upload(int dev, int alt, void *buf, int max_len);
int  usb_dfu_get_status(int dev, int *state, int *poll_timeout);
int  usb_dfu_get_state(int dev);
int  usb_dfu_clear_status(int dev);
int  usb_dfu_abort(int dev);

#endif

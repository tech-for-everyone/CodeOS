#ifndef USB_H
#define USB_H

#include "types.h"

void usb_init(void);
int  usb_available(void);
void usb_print_info(void);

/* Unified USB transfer API (dispatches to EHCI/xHCI) */
struct usb_dev;
int  usb_control_transfer(struct usb_dev *dev, int dir_in,
                          uint8_t bmReqType, uint8_t bRequest,
                          uint16_t wValue, uint16_t wIndex,
                          uint16_t wLength, void *data);
int  usb_bulk_transfer(struct usb_dev *dev, int endpoint,
                       int dir_in, void *data, int len);

/* Unified USB keyboard polling */
int  usb_kbd_available(void);
int  usb_kbd_poll(void);

/* Unified USB touch/tablet polling */
int  usb_touch_available(void);
int  usb_touch_poll(int *x, int *y, int *btn);

/* Unified USB relative mouse polling */
int  usb_mouse_available(void);
int  usb_mouse_poll(int *dx, int *dy, int *buttons, int *wheel);

/* USB hub */
int  usb_hub_init(void);
int  usb_hub_available(void);
int  usb_hub_get_port_count(int hub);
int  usb_hub_reset_port(int hub, int port);

/* USB mass storage */
int  usb_msc_init(void);
int  usb_msc_available(void);
int  usb_msc_read_sectors(uint32_t lba, uint32_t count, void *buf);
int  usb_msc_write_sectors(uint32_t lba, uint32_t count, const void *buf);
int  usb_msc_get_sector_size(void);
uint32_t usb_msc_get_sector_count(void);

/* USB Bluetooth HCI */
int  usb_bt_init(void);
int  usb_bt_available(void);
void usb_bt_print_info(void);

/* USB CDC ACM (Serial) */
int  usb_acm_init(void);
int  usb_acm_available(void);
void usb_acm_print_info(void);
int  usb_acm_get_port_count(void);
int  usb_acm_open(int port);
int  usb_acm_close(int port);
int  usb_acm_write(int port, const void *data, int len);
int  usb_acm_read(int port, void *buf, int max_len);

/* USB Audio */
int  usb_audio_init(void);
int  usb_audio_available(void);
void usb_audio_print_info(void);
int  usb_audio_get_stream_count(void);

/* USB UVC (Webcam) */
int  usb_uvc_init(void);
int  usb_uvc_available(void);
void usb_uvc_print_info(void);
int  usb_uvc_get_camera_count(void);

/* USB Gamepad */
int  usb_gamepad_init(void);
int  usb_gamepad_available(void);
void usb_gamepad_print_info(void);
int  usb_gamepad_get_count(void);

/* USB Printer */
int  usb_printer_init(void);
int  usb_printer_available(void);
void usb_printer_print_info(void);
int  usb_printer_get_count(void);

/* USB RNDIS */
int  usb_rndis_init(void);
int  usb_rndis_available(void);
void usb_rndis_print_info(void);

/* USB MTP */
int  usb_mtp_init(void);
int  usb_mtp_available(void);
void usb_mtp_print_info(void);
int  usb_mtp_get_device_count(void);

/* USB Wacom */
int  usb_wacom_init(void);
int  usb_wacom_available(void);
void usb_wacom_print_info(void);
int  usb_wacom_get_count(void);

/* USB DFU */
int  usb_dfu_init(void);
int  usb_dfu_available(void);
void usb_dfu_print_info(void);
int  usb_dfu_get_device_count(void);

#endif

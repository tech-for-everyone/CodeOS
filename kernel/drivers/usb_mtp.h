#ifndef USB_MTP_H
#define USB_MTP_H

#include "types.h"

/* USB MTP/PTP (Media Transfer Protocol / Picture Transfer Protocol) */

#define MTP_MAX_DEVICES  4
#define MTP_MAX_OBJECTS  64
#define MTP_TX_BUF_SIZE 8192

/* MTP operation codes */
#define MTP_OP_GET_DEVICE_INFO      0x1001
#define MTP_OP_OPEN_SESSION         0x1002
#define MTP_OP_CLOSE_SESSION        0x1003
#define MTP_OP_GET_STORAGE_IDS      0x1004
#define MTP_OP_GET_STORAGE_INFO     0x1005
#define MTP_OP_GET_NUM_OBJECTS      0x1006
#define MTP_OP_GET_OBJECT_HANDLES   0x1007
#define MTP_OP_GET_OBJECT_INFO      0x1008
#define MTP_OP_GET_OBJECT           0x1009
#define MTP_OP_SEND_OBJECT          0x100C
#define MTP_OP_GET_PARTIAL_OBJECT   0x101B

/* MTP response codes */
#define MTP_RSP_OK                  0x2001
#define MTP_RSP_GENERAL_ERROR       0x2002
#define MTP_RSP_SESSION_NOT_OPEN    0x2003
#define MTP_RSP_INVALID_STORAGE_ID  0x2004

/* MTP object format codes */
#define MTP_FORMAT_UNDEFINED        0x3000
#define MTP_FORMAT_ASSOCIATION      0x3001
#define MTP_FORMAT_JPEG             0x3801
#define MTP_FORMAT_PNG              0x380B
#define MTP_FORMAT_TEXT             0x3004

struct mtp_device {
    struct usb_dev *dev;
    int  open;
    int  session_id;
    char manufacturer[128];
    char model[128];
    char serial[128];
    uint32_t storage_ids[8];
    int  num_storage;
    int  slot;
};

struct mtp_object {
    uint32_t handle;
    uint32_t parent;
    uint32_t format;
    uint32_t size;
    char     name[128];
};

int  usb_mtp_init(void);
int  usb_mtp_available(void);
void usb_mtp_print_info(void);

int  usb_mtp_get_device_count(void);
int  usb_mtp_open_session(int dev);
int  usb_mtp_close_session(int dev);
int  usb_mtp_get_object_count(int dev);
int  usb_mtp_get_object(int dev, uint32_t handle, void *buf, int max_len);
int  usb_mtp_get_partial_object(int dev, uint32_t handle, uint32_t offset,
                                uint32_t len, void *buf, int max_len);
int  usb_mtp_send_object(int dev, uint32_t parent, const char *name,
                         uint32_t format, const void *data, int len);
int  usb_mtp_get_storage_count(int dev);
int  usb_mtp_get_free_space(int dev, int storage_idx);
int  usb_mtp_get_used_space(int dev, int storage_idx);

#endif

#ifndef USB_BT_H
#define USB_BT_H

#include "types.h"

/* USB Bluetooth HCI Transport (USB Transport Layer for BT Core Spec v5.x) */

#define USB_BT_EP_IN    0x81
#define USB_BT_EP_OUT   0x02

/* HCI packet types over USB */
#define HCI_CMD_PACKET      0x01
#define HCI_ACL_PACKET      0x02
#define HCI_SCO_PACKET      0x03
#define HCI_EVENT_PACKET    0x04

/* HCI opcodes (OGF << 10 | OCF) */
#define HCI_OP(ogf, ocf)    (((ogf) << 10) | (ocf))

#define HCI_OP_LINK_CTRL        0x01
#define HCI_OP_LINK_POLICY      0x02
#define HCI_OP_HOST_CTL         0x03
#define HCI_OP_INFO_PARAMS      0x04
#define HCI_OP_STATUS_PARAMS    0x05
#define HCI_OP_LE_CTL           0x08
#define HCI_OP_VENDOR          0x3F

#define HCI_RESET                   HCI_OP(0x03, 0x0003)
#define HCI_READ_BD_ADDR            HCI_OP(0x04, 0x0009)
#define HCI_READ_LOCAL_NAME         HCI_OP(0x04, 0x0014)
#define HCI_READ_LOCAL_VERSION      HCI_OP(0x04, 0x0001)
#define HCI_WRITE_SCAN_ENABLE       HCI_OP(0x03, 0x001A)
#define HCI_WRITE_LOCAL_NAME        HCI_OP(0x03, 0x0013)
#define HCI_WRITE_CLASS_OF_DEVICE   HCI_OP(0x03, 0x0024)
#define HCI_INQUIRY                 HCI_OP(0x01, 0x0001)
#define HCI_INQUIRY_CANCEL          HCI_OP(0x01, 0x0002)
#define HCI_CREATE_CONNECTION       HCI_OP(0x01, 0x0005)
#define HCI_ACCEPT_CONNECTION       HCI_OP(0x01, 0x0009)
#define HCI_DISCONNECT              HCI_OP(0x01, 0x0006)
#define HCI_LE_SET_SCAN_ENABLE      HCI_OP(0x08, 0x000C)
#define HCI_LE_SET_SCAN_PARAMS      HCI_OP(0x08, 0x000B)

/* HCI event codes */
#define HCI_EVT_CMD_COMPLETE   0x0E
#define HCI_EVT_CMD_STATUS     0x0F
#define HCI_EVT_CONN_REQUEST   0x04
#define HCI_EVT_CONN_COMPLETE  0x03
#define HCI_EVT_DISCONN_COMPLETE 0x05
#define HCI_EVT_INQUIRY_RESULT 0x02
#define HCI_EVT_INQUIRY_RESULT_WITH_RSSI 0x22
#define HCI_EVT_NUM_COMP_PKTS  0x13

#define BT_MAX_ACL_DATA    1024
#define BT_MAX_CMD_DATA    256
#define BT_MAX_EVT_DATA    256
#define BT_MAX_DEVICES     16

struct bt_device {
    uint8_t  bd_addr[6];
    char     name[64];
    uint16_t handle;
    uint8_t  class_of_device[3];
    int      connected;
    int      slot_id;
    struct usb_dev *usb_dev;
};

int  usb_bt_init(void);
int  usb_bt_available(void);
void usb_bt_print_info(void);

int  usb_bt_send_cmd(uint16_t opcode, const void *data, int len);
int  usb_bt_send_acl(uint16_t handle, const void *data, int len);
int  usb_bt_poll_event(void *buf, int max_len);

int  usb_bt_inquiry_start(void);
int  usb_bt_inquiry_cancel(void);
int  usb_bt_get_device_count(void);
struct bt_device *usb_bt_get_device(int idx);

int  usb_bt_create_connection(uint8_t *bd_addr);
int  usb_bt_disconnect(uint16_t handle);

#endif

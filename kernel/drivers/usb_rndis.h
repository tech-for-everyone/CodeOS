#ifndef USB_RNDIS_H
#define USB_RNDIS_H

#include "types.h"
#include "nic.h"

/* USB RNDIS (Remote Network Driver Interface Specification) — Microsoft USB networking */

#define RNDIS_MAX_MTU 1514
#define RNDIS_RX_BUF  4096

/* RNDIS message types */
#define RNDIS_MSG_INITIALIZE     0x00000002
#define RNDIS_MSG_INITIALIZE_CMPLT 0x00000002
#define RNDIS_MSG_QUERY          0x00000004
#define RNDIS_MSG_SET            0x00000005
#define RNDIS_MSG_RESET          0x00000006
#define RNDIS_MSG_RESET_CMPLT    0x00000006
#define RNDIS_MSG_KEEPALIVE      0x00000008
#define RNDIS_MSG_KEEPALIVE_CMPLT 0x00000008
#define RNDIS_MSG_PACKET         0x00000001

/* OID codes */
#define OID_GEN_SUPPORTED_LIST    0x00010101
#define OID_GEN_MEDIA_SUPPORTED   0x00010104
#define OID_GEN_MEDIA_IN_USE      0x00010105
#define OID_GEN_MAXIMUM_FRAME_SIZE 0x00010111
#define OID_GEN_LINK_SPEED        0x00010121
#define OID_GEN_PACKET_FILTER     0x0001010E
#define OID_802_3_CURRENT_ADDRESS 0x01010102
#define OID_GEN_MAXIMUM_TOTAL_SIZE 0x00010113

/* RNDIS status codes */
#define RNDIS_STATUS_SUCCESS     0x00000000
#define RNDIS_STATUS_PENDING     0x00000103
#define RNDIS_STATUS_NOT_ACCEPTED 0xC0010017

struct rndis_msg_hdr {
    uint32_t msg_type;
    uint32_t msg_len;
    uint32_t request_id;
    uint32_t data_offset;
    uint32_t data_len;
    uint32_t oid;
    uint32_t info_buffer_offset;
    uint32_t info_len;
};

struct rndis_init_msg {
    uint32_t msg_type;
    uint32_t msg_len;
    uint32_t request_id;
    uint32_t major_version;
    uint32_t minor_version;
    uint32_t max_transfer_size;
};

struct rndis_init_cmplt {
    uint32_t msg_type;
    uint32_t msg_len;
    uint32_t request_id;
    uint32_t status;
    uint32_t major_version;
    uint32_t minor_version;
    uint32_t device_flags;
    uint32_t medium;
    uint32_t max_packets_per_message;
    uint32_t max_transfer_size;
    uint32_t packet_alignment;
    uint32_t af_list_offset;
    uint32_t af_list_size;
};

int  usb_rndis_init(void);
int  usb_rndis_available(void);
void usb_rndis_print_info(void);

int  usb_rndis_send(const void *data, int len);
int  usb_rndis_recv(void *buf, int max_len);
void usb_rndis_get_mac(uint8_t *mac);
int  usb_rndis_get_link_speed(void);

int usb_rndis_probe(void);

extern nic_driver_t usb_rndis_nic;
int  usb_rndis_get_mtu(void);

#endif

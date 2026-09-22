#include "usb_rndis.h"
#include "nic.h"
#include "usb_ehci.h"
#include "../kernel/kprintf.h"
#include "../kernel/string.h"
#include "../kernel/pmm.h"
#include "../kernel/vmm.h"

static struct usb_dev *rndis_dev;
static int rndis_ok;
static uint8_t rndis_mac[6];
static int rndis_ep_in;
static int rndis_ep_out;

static uint32_t rndis_request_id;
static uint32_t rndis_link_speed;
static uint32_t rndis_max_frame;

static uint8_t rndis_tx_buf[RNDIS_RX_BUF] __attribute__((aligned(16)));
static uint8_t rndis_rx_buf[RNDIS_RX_BUF] __attribute__((aligned(16)));

static uint32_t rndis_next_request_id(void) {
    return ++rndis_request_id;
}

#pragma pack(push, 1)
struct rndis_ctrl_msg {
    uint32_t msg_type;
    uint32_t msg_len;
    uint32_t request_id;
    uint32_t data_offset;
    uint32_t data_len;
};
#pragma pack(pop)

static int rndis_send_control_msg(uint32_t msg_type, const void *data, int data_len) {
    struct rndis_ctrl_msg *m = (struct rndis_ctrl_msg *)rndis_tx_buf;
    memset(rndis_tx_buf, 0, sizeof(struct rndis_ctrl_msg));
    m->msg_type = msg_type;
    m->msg_len = sizeof(struct rndis_ctrl_msg) + data_len;
    m->request_id = rndis_next_request_id();
    m->data_offset = 0;
    m->data_len = data_len;

    if (data && data_len > 0)
        memcpy(rndis_tx_buf + sizeof(struct rndis_ctrl_msg), data, data_len);

    return ehci_control_transfer(rndis_dev, USB_DIR_OUT, 0x00, 0x00,
                                 0, 0, m->msg_len, rndis_tx_buf);
}

static int rndis_recv_response(uint32_t expected_type, void *buf, int max_len) {
    for (int attempt = 0; attempt < 50; attempt++) {
        for (volatile int d = 0; d < 10000; d++) asm volatile("pause");

        int r = ehci_bulk_transfer(rndis_dev, rndis_ep_in, USB_DIR_IN,
                                   rndis_rx_buf, RNDIS_RX_BUF);
        if (r < (int)sizeof(struct rndis_ctrl_msg)) continue;

        struct rndis_ctrl_msg *m = (struct rndis_ctrl_msg *)rndis_rx_buf;
        if (m->msg_type == expected_type) {
            int copy = m->data_len;
            if (copy > max_len) copy = max_len;
            if (copy > 0 && buf)
                memcpy(buf, rndis_rx_buf + sizeof(struct rndis_ctrl_msg) + m->data_offset, copy);
            return copy;
        }
    }
    return -1;
}

static int rndis_query_oid(uint32_t oid, void *buf, int max_len) {
    uint8_t qdata[8];
    qdata[0] = oid & 0xFF;
    qdata[1] = (oid >> 8) & 0xFF;
    qdata[2] = (oid >> 16) & 0xFF;
    qdata[3] = (oid >> 24) & 0xFF;
    qdata[4] = 0;
    qdata[5] = 0;
    qdata[6] = 0;
    qdata[7] = 0;

    rndis_send_control_msg(RNDIS_MSG_QUERY, qdata, 8);
    return rndis_recv_response(RNDIS_MSG_QUERY, buf, max_len);
}

int usb_rndis_init(void) {
    rndis_ok = 0;
    rndis_dev = NULL;
    rndis_request_id = 0;

    int nd = ehci_get_num_devs();
    for (int i = 0; i < nd; i++) {
        struct usb_dev *d = ehci_get_dev(i);
        if (!d) continue;
        if ((d->class_code == 0xEF && d->subclass == 0x01 && d->protocol == 0x01) ||
            (d->vendor_id == 0x0424 && d->product_id == 0xEC00) ||
            (d->vendor_id == 0x0525 && d->product_id == 0xA4A2)) {
            rndis_dev = d;
            rndis_ep_in = d->ep_in_addr & 0x0F;
            rndis_ep_out = d->ep_out_addr & 0x0F;
            break;
        }
    }
    if (!rndis_dev) return -1;

    struct rndis_init_msg {
        uint32_t msg_type;
        uint32_t msg_len;
        uint32_t request_id;
        uint32_t major_version;
        uint32_t minor_version;
        uint32_t max_transfer_size;
    } init;
    memset(&init, 0, sizeof(init));
    init.msg_type = RNDIS_MSG_INITIALIZE;
    init.msg_len = sizeof(init);
    init.request_id = rndis_next_request_id();
    init.major_version = 1;
    init.minor_version = 0;
    init.max_transfer_size = 4096;

    int r = ehci_control_transfer(rndis_dev, USB_DIR_OUT, 0x00, 0x00,
                                  0, 0, sizeof(init), &init);
    if (r < 0) {
        kprintf("usb_rndis: init control failed\n");
        return -1;
    }

    struct rndis_init_cmplt cmplt;
    r = rndis_recv_response(RNDIS_MSG_INITIALIZE_CMPLT, &cmplt, sizeof(cmplt));
    if (r < 0 || cmplt.status != RNDIS_STATUS_SUCCESS) {
        kprintf("usb_rndis: init failed (status=%d)\n",
                (r >= 0) ? (int)cmplt.status : -1);
        return -1;
    }

    rndis_link_speed = cmplt.max_transfer_size;
    rndis_max_frame = cmplt.max_transfer_size;

    uint8_t mac_data[6];
    int mlen = rndis_query_oid(OID_802_3_CURRENT_ADDRESS, mac_data, 6);
    if (mlen >= 6) {
        memcpy(rndis_mac, mac_data, 6);
    } else {
        rndis_mac[0] = (rndis_dev->vendor_id >> 8) & 0xFF;
        rndis_mac[1] = rndis_dev->vendor_id & 0xFF;
        rndis_mac[2] = (rndis_dev->product_id >> 8) & 0xFF;
        rndis_mac[3] = rndis_dev->product_id & 0xFF;
        rndis_mac[4] = 0x01;
        rndis_mac[5] = 0x00;
    }

    uint32_t filter = 0x0000000D;
    rndis_send_control_msg(RNDIS_MSG_SET, &filter, 4);
    uint8_t drain[64];
    rndis_recv_response(RNDIS_MSG_SET, drain, sizeof(drain));

    rndis_ok = 1;
    kprintf("usb_rndis: RNDIS Ethernet MAC=%02x:%02x:%02x:%02x:%02x:%02x\n",
            rndis_mac[0], rndis_mac[1], rndis_mac[2],
            rndis_mac[3], rndis_mac[4], rndis_mac[5]);
    return 0;
}

int usb_rndis_available(void) { return rndis_ok; }

int usb_rndis_probe(void) {
    if (usb_rndis_init() < 0) return -1;
    return 0;
}

int usb_rndis_send(const void *data, int len) {
    if (!rndis_ok || !rndis_dev) return -1;
    if (len > RNDIS_RX_BUF - 44) len = RNDIS_RX_BUF - 44;

    struct rndis_ctrl_msg *m = (struct rndis_ctrl_msg *)rndis_tx_buf;
    memset(rndis_tx_buf, 0, sizeof(struct rndis_ctrl_msg));
    m->msg_type = RNDIS_MSG_PACKET;
    m->msg_len = sizeof(struct rndis_ctrl_msg) + 8 + len;
    m->data_offset = 0;
    m->data_len = len;

    memset(rndis_tx_buf + sizeof(struct rndis_ctrl_msg), 0, 8);
    memcpy(rndis_tx_buf + sizeof(struct rndis_ctrl_msg) + 8, data, len);

    int r = ehci_bulk_transfer(rndis_dev, rndis_ep_out, USB_DIR_OUT,
                               rndis_tx_buf, m->msg_len);
    return (r >= 0) ? len : -1;
}

int usb_rndis_recv(void *buf, int max_len) {
    if (!rndis_ok || !rndis_dev) return -1;

    int r = ehci_bulk_transfer(rndis_dev, rndis_ep_in, USB_DIR_IN,
                               rndis_rx_buf, RNDIS_RX_BUF);
    if (r < (int)sizeof(struct rndis_ctrl_msg)) return -1;

    struct rndis_ctrl_msg *m = (struct rndis_ctrl_msg *)rndis_rx_buf;
    if (m->msg_type != RNDIS_MSG_PACKET) return -1;

    int data_len = m->data_len;
    int data_off = m->data_offset + sizeof(uint32_t);

    if (data_len <= 0 || data_len > max_len) return -1;
    if (data_off + data_len > r) return -1;

    memcpy(buf, rndis_rx_buf + data_off, data_len);
    return data_len;
}

void usb_rndis_get_mac(uint8_t *mac) {
    memcpy(mac, rndis_mac, 6);
}

int usb_rndis_get_link_speed(void) {
    return rndis_link_speed ? 100 : 0;
}

int usb_rndis_get_mtu(void) {
    return RNDIS_MAX_MTU;
}

void usb_rndis_print_info(void) {
    if (!rndis_ok) {
        kprintf("usb_rndis: not available\n");
        return;
    }
    kprintf("usb_rndis: RNDIS Ethernet MAC=%02x:%02x:%02x:%02x:%02x:%02x\n",
            rndis_mac[0], rndis_mac[1], rndis_mac[2],
            rndis_mac[3], rndis_mac[4], rndis_mac[5]);
}

nic_driver_t usb_rndis_nic = {
    .name = "usb-rndis",
    .probe = usb_rndis_probe,
    .send = usb_rndis_send,
    .recv = usb_rndis_recv,
    .get_mac = usb_rndis_get_mac,
};

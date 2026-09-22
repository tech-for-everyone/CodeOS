#include "usb_bt.h"
#include "usb_ehci.h"
#include "../kernel/kprintf.h"
#include "../kernel/string.h"
#include "../kernel/pmm.h"
#include "../kernel/vmm.h"

static struct usb_dev *bt_dev;
static int bt_ok;
static int bt_ep_in;
static int bt_ep_out;

static struct bt_device bt_devices[BT_MAX_DEVICES];
static int bt_num_devices;

static uint8_t bt_cmd_buf[BT_MAX_CMD_DATA] __attribute__((aligned(16)));
static uint8_t bt_evt_buf[BT_MAX_EVT_DATA] __attribute__((aligned(16)));
static uint8_t bt_acl_buf[BT_MAX_ACL_DATA] __attribute__((aligned(16)));

static int bt_cmd_idx;

static void bd_addr_to_str(const uint8_t *addr, char *str) {
    for (int i = 0; i < 6; i++) {
        str[i * 3]     = "0123456789ABCDEF"[(addr[5 - i] >> 4) & 0xF];
        str[i * 3 + 1] = "0123456789ABCDEF"[addr[5 - i] & 0xF];
        str[i * 3 + 2] = ':';
    }
    str[17] = 0;
}

static int bt_send_raw(const void *data, int len) {
    if (!bt_dev) return -1;
    return ehci_bulk_transfer(bt_dev, bt_ep_out, USB_DIR_OUT, (void *)data, len);
}

static int bt_recv_raw(void *data, int max_len) {
    if (!bt_dev) return -1;
    return ehci_bulk_transfer(bt_dev, bt_ep_in, USB_DIR_IN, data, max_len);
}

static int bt_handle_event(const uint8_t *evt, int len) {
    if (len < 2) return 0;
    uint8_t event_code = evt[0];
    uint8_t param_len  = evt[1];

    switch (event_code) {
    case HCI_EVT_CMD_COMPLETE: {
        if (param_len < 4) break;
        uint16_t opcode = evt[3] | ((uint16_t)evt[4] << 8);
        uint8_t status  = evt[5];
        (void)opcode;
        (void)status;
        return 0;
    }
    case HCI_EVT_CMD_STATUS: {
        if (param_len < 4) break;
        uint8_t status = evt[2];
        uint16_t opcode = evt[4] | ((uint16_t)evt[5] << 8);
        (void)status;
        (void)opcode;
        return 0;
    }
    case HCI_EVT_INQUIRY_RESULT:
    case HCI_EVT_INQUIRY_RESULT_WITH_RSSI: {
        if (param_len < 14) break;
        int idx = bt_num_devices;
        if (idx >= BT_MAX_DEVICES) break;
        struct bt_device *d = &bt_devices[idx];
        memset(d, 0, sizeof(*d));
        memcpy(d->bd_addr, evt + 3, 6);
        d->class_of_device[0] = evt[9];
        d->class_of_device[1] = evt[10];
        d->class_of_device[2] = evt[11];
        d->connected = 0;
        d->slot_id = -1;
        bd_addr_to_str(d->bd_addr, (char *)d->name);
        bt_num_devices++;
        kprintf("bt: inquiry found %s class=%02x%02x%02x\n",
                d->name, d->class_of_device[0],
                d->class_of_device[1], d->class_of_device[2]);
        return 0;
    }
    case HCI_EVT_CONN_REQUEST: {
        if (param_len < 10) break;
        uint8_t bd_addr[6];
        memcpy(bd_addr, evt + 3, 6);
        kprintf("bt: connection request from ");
        char addr[18];
        bd_addr_to_str(bd_addr, addr);
        kprintf("%s\n", addr);
        return 0;
    }
    case HCI_EVT_CONN_COMPLETE: {
        if (param_len < 13) break;
        uint8_t status = evt[2];
        uint16_t handle = evt[4] | ((uint16_t)evt[5] << 8);
        uint8_t bd_addr[6];
        memcpy(bd_addr, evt + 6, 6);
        if (status == 0) {
            for (int i = 0; i < bt_num_devices; i++) {
                if (memcmp(bt_devices[i].bd_addr, bd_addr, 6) == 0) {
                    bt_devices[i].handle = handle;
                    bt_devices[i].connected = 1;
                    break;
                }
            }
            kprintf("bt: connected handle=0x%04x\n", handle);
        }
        return 0;
    }
    case HCI_EVT_DISCONN_COMPLETE: {
        if (param_len < 4) break;
        uint16_t handle = evt[4] | ((uint16_t)evt[5] << 8);
        for (int i = 0; i < bt_num_devices; i++) {
            if (bt_devices[i].handle == handle) {
                bt_devices[i].connected = 0;
                bt_devices[i].handle = 0;
                break;
            }
        }
        kprintf("bt: disconnected handle=0x%04x\n", handle);
        return 0;
    }
    default:
        break;
    }
    return 0;
}

int usb_bt_send_cmd(uint16_t opcode, const void *data, int len) {
    if (!bt_ok) return -1;
    bt_cmd_buf[0] = HCI_CMD_PACKET;
    bt_cmd_buf[1] = opcode & 0xFF;
    bt_cmd_buf[2] = (opcode >> 8) & 0xFF;
    bt_cmd_buf[3] = len;
    if (data && len > 0)
        memcpy(bt_cmd_buf + 4, data, len);
    return bt_send_raw(bt_cmd_buf, 4 + len);
}

int usb_bt_send_acl(uint16_t handle, const void *data, int len) {
    if (!bt_ok) return -1;
    uint8_t hdr[4];
    hdr[0] = handle & 0xFF;
    hdr[1] = ((handle >> 8) & 0x0F) | 0x20;
    hdr[2] = len & 0xFF;
    hdr[3] = (len >> 8) & 0xFF;

    bt_acl_buf[0] = HCI_ACL_PACKET;
    memcpy(bt_acl_buf + 1, hdr, 4);
    memcpy(bt_acl_buf + 5, data, len);
    return bt_send_raw(bt_acl_buf, 5 + len);
}

int usb_bt_poll_event(void *buf, int max_len) {
    if (!bt_ok) return -1;
    int r = bt_recv_raw(bt_evt_buf, BT_MAX_EVT_DATA);
    if (r <= 0) return -1;
    if (bt_evt_buf[0] == HCI_EVENT_PACKET) {
        int evt_len = 2 + bt_evt_buf[1];
        if (evt_len > r) evt_len = r;
        bt_handle_event(bt_evt_buf, evt_len);
        if (buf && max_len > 0) {
            int cpy = evt_len < max_len ? evt_len : max_len;
            memcpy(buf, bt_evt_buf, cpy);
        }
        return evt_len;
    }
    return -1;
}

int usb_bt_inquiry_start(void) {
    uint8_t params[5];
    params[0] = 0x08;   /* LAP: GIAC */
    params[1] = 0x00;
    params[2] = 0x8B;
    params[3] = 0x30;   /* duration: 30 * 1.28s */
    params[4] = 0x00;   /* num_responses: unlimited */
    return usb_bt_send_cmd(HCI_INQUIRY, params, 5);
}

int usb_bt_inquiry_cancel(void) {
    return usb_bt_send_cmd(HCI_INQUIRY_CANCEL, NULL, 0);
}

int usb_bt_get_device_count(void) {
    return bt_num_devices;
}

struct bt_device *usb_bt_get_device(int idx) {
    if (idx < 0 || idx >= bt_num_devices) return NULL;
    return &bt_devices[idx];
}

int usb_bt_create_connection(uint8_t *bd_addr) {
    uint8_t params[13];
    memset(params, 0, sizeof(params));
    memcpy(params, bd_addr, 6);
    params[6] = 0x18;   /* packet type: DM1, DH1, DM3, DH3, DM5, DH5 */
    params[7] = 0x00;
    params[8] = 0x01;   /* page scan repetition mode */
    params[9] = 0x00;
    params[10] = 0x00;  /* clock offset */
    params[11] = 0x00;
    params[12] = 0x01;  /* allow role switch */
    return usb_bt_send_cmd(HCI_CREATE_CONNECTION, params, 13);
}

int usb_bt_disconnect(uint16_t handle) {
    uint8_t params[3];
    params[0] = handle & 0xFF;
    params[1] = (handle >> 8) & 0x0F;
    params[2] = 0x13;   /* remote user terminated */
    return usb_bt_send_cmd(HCI_DISCONNECT, params, 3);
}

int usb_bt_init(void) {
    bt_ok = 0;
    bt_dev = NULL;
    bt_num_devices = 0;

    int nd = ehci_get_num_devs();
    for (int i = 0; i < nd; i++) {
        struct usb_dev *d = ehci_get_dev(i);
        if (!d) continue;
        /* Bluetooth class: 0xE0/0x01/0x01 */
        if (d->class_code == 0xE0 && d->subclass == 0x01 && d->protocol == 0x01) {
            bt_dev = d;
            bt_ep_in = d->ep_in_addr & 0x0F;
            bt_ep_out = d->ep_out_addr & 0x0F;
            break;
        }
    }
    if (!bt_dev) return -1;

    bt_cmd_idx = 0;
    memset(bt_devices, 0, sizeof(bt_devices));

    /* Send HCI Reset */
    usb_bt_send_cmd(HCI_RESET, NULL, 0);
    for (volatile int i = 0; i < 100000; i++) asm volatile("pause");
    usb_bt_poll_event(NULL, 0);

    /* Read BD_ADDR */
    uint8_t resp[64];
    usb_bt_send_cmd(HCI_READ_BD_ADDR, NULL, 0);
    for (volatile int i = 0; i < 100000; i++) asm volatile("pause");
    int r = usb_bt_poll_event(resp, sizeof(resp));

    char addr_str[18] = "??";
    if (r > 0 && resp[0] == HCI_EVENT_PACKET && resp[3] == 0x0E) {
        /* Command Complete event: evt_len starts at offset 2, params at 5 */
        if (r >= 12) {
            uint8_t *bd = resp + 8;
            bd_addr_to_str(bd, addr_str);
        }
    }

    /* Set local name */
    char name[] = "CodeOS BT";
    bt_cmd_buf[0] = HCI_WRITE_LOCAL_NAME & 0xFF;
    bt_cmd_buf[1] = (HCI_WRITE_LOCAL_NAME >> 8) & 0xFF;
    bt_cmd_buf[2] = sizeof(name);
    memcpy(bt_cmd_buf + 3, name, sizeof(name));
    /* Send as raw command over bulk */
    uint8_t raw_cmd[4 + sizeof(name)];
    raw_cmd[0] = HCI_CMD_PACKET;
    raw_cmd[1] = HCI_WRITE_LOCAL_NAME & 0xFF;
    raw_cmd[2] = (HCI_WRITE_LOCAL_NAME >> 8) & 0xFF;
    raw_cmd[3] = sizeof(name);
    memcpy(raw_cmd + 4, name, sizeof(name));
    bt_send_raw(raw_cmd, sizeof(raw_cmd));
    for (volatile int i = 0; i < 100000; i++) asm volatile("pause");
    usb_bt_poll_event(NULL, 0);

    bt_ok = 1;
    kprintf("usb_bt: Bluetooth HCI at BD_ADDR=%s\n", addr_str);
    return 0;
}

int usb_bt_available(void) {
    return bt_ok;
}

void usb_bt_print_info(void) {
    if (!bt_ok) {
        kprintf("usb_bt: not available\n");
        return;
    }
    kprintf("usb_bt: Bluetooth controller ready, %d device(s) found\n",
            bt_num_devices);
    for (int i = 0; i < bt_num_devices; i++) {
        struct bt_device *d = &bt_devices[i];
        kprintf("  %d: %s %s class=%02x%02x%02x\n",
                i, d->name, d->connected ? "(connected)" : "",
                d->class_of_device[0], d->class_of_device[1],
                d->class_of_device[2]);
    }
}

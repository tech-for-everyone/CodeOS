#include "usb_mtp.h"
#include "usb_ehci.h"
#include "../kernel/kprintf.h"
#include "../kernel/string.h"
#include "../kernel/pmm.h"
#include "../kernel/vmm.h"

static struct mtp_device mtp_devs[MTP_MAX_DEVICES];
static int mtp_num_devices;
static int mtp_ok;

static uint8_t mtp_buf[MTP_TX_BUF_SIZE] __attribute__((aligned(16)));

static uint32_t mtp_req_id;

static uint32_t mtp_next_id(void) {
    return ++mtp_req_id;
}

#pragma pack(push, 1)
struct mtp_container {
    uint32_t length;
    uint16_t type;       /* 1=request, 2=response, 3=data */
    uint16_t code;
    uint32_t transaction_id;
};
#pragma pack(pop)

static int mtp_send_request(struct mtp_device *d, uint16_t code,
                            uint32_t *params, int num_params) {
    struct mtp_container *c = (struct mtp_container *)mtp_buf;
    c->length = sizeof(*c) + num_params * 4;
    c->type = 1; /* request */
    c->code = code;
    c->transaction_id = mtp_next_id();

    for (int i = 0; i < num_params; i++) {
        uint32_t *p = (uint32_t *)(mtp_buf + sizeof(*c) + i * 4);
        *p = params[i];
    }

    return ehci_control_transfer(d->dev, USB_DIR_OUT, 0x00, 0x6C,
                                 0, 0, c->length, mtp_buf);
}

static int mtp_recv_response(struct mtp_device *d, void *buf, int max_len) {
    for (int attempt = 0; attempt < 100; attempt++) {
        for (volatile int i = 0; i < 5000; i++) asm volatile("pause");

        int r = ehci_control_transfer(d->dev, USB_DIR_IN, 0x80, 0x6C,
                                      0, 0, max_len > MTP_TX_BUF_SIZE ? MTP_TX_BUF_SIZE : max_len,
                                      buf ? buf : mtp_buf);
        if (r < (int)sizeof(struct mtp_container)) continue;

        struct mtp_container *c = (struct mtp_container *)buf;
        if (c->type == 2) { /* response */
            return (c->code == MTP_RSP_OK) ? 0 : -1;
        }
        if (c->type == 3) { /* data */
            int data_start = sizeof(struct mtp_container);
            int data_len = c->length - data_start;
            if (data_len > max_len) data_len = max_len;
            if (data_len > 0 && buf) {
                memcpy(buf, (uint8_t *)c + data_start, data_len);
            }
            /* Drain the response that follows */
            uint8_t drain[64];
            for (volatile int j = 0; j < 10000; j++) asm volatile("pause");
            ehci_control_transfer(d->dev, USB_DIR_IN, 0x80, 0x6C,
                                  0, 0, sizeof(drain), drain);
            return data_len;
        }
    }
    return -1;
}

static int mtp_send_and_recv(struct mtp_device *d, uint16_t code,
                             uint32_t *params, int num_params,
                             void *data_buf, int max_len) {
    mtp_send_request(d, code, params, num_params);
    return mtp_recv_response(d, data_buf, max_len);
}

static void mtp_get_device_info(struct mtp_device *d) {
    int r = mtp_send_and_recv(d, MTP_OP_GET_DEVICE_INFO, NULL, 0,
                              mtp_buf, MTP_TX_BUF_SIZE);
    if (r < 52) return;

    /* Parse standard device info: skip standard version (4), MTP version (4),
       vendor extension desc len (2), vendor extension desc (variable),
       functional mode (2), supported ops count (2), supported ops (variable),
       events supported count (2), events (variable), device properties count (2),
       device properties (variable), capture formats count (2), capture formats (variable),
       image formats count (2), image formats (variable) */
    int off = 4 + 4; /* standard + mtp version */

    /* Skip vendor extension descriptor */
    if (off + 2 <= r) {
        uint16_t ext_len = *(uint16_t *)(mtp_buf + off);
        off += 2 + ext_len;
    }

    /* functional mode */
    off += 2;

    /* supported operations */
    if (off + 2 <= r) {
        uint16_t num_ops = *(uint16_t *)(mtp_buf + off);
        off += 2 + num_ops * 2;
    }

    /* events supported */
    if (off + 2 <= r) {
        uint16_t num_events = *(uint16_t *)(mtp_buf + off);
        off += 2 + num_events * 2;
    }

    /* device properties */
    if (off + 2 <= r) {
        uint16_t num_props = *(uint16_t *)(mtp_buf + off);
        off += 2 + num_props * 2;
    }

    /* capture formats */
    if (off + 2 <= r) {
        uint16_t num_fmt = *(uint16_t *)(mtp_buf + off);
        off += 2 + num_fmt * 2;
    }

    /* image formats */
    if (off + 2 <= r) {
        uint16_t num_fmt = *(uint16_t *)(mtp_buf + off);
        off += 2 + num_fmt * 2;
    }

    /* manufacturer, model, serial as Unicode strings */
    /* manufacturer (2 bytes len in chars, then UTF-16LE) */
    if (off + 2 <= r) {
        uint16_t slen = *(uint16_t *)(mtp_buf + off);
        off += 2;
        int ci = 0;
        for (int i = 0; i < slen && off + 2 <= r && ci < 127; i++) {
            uint16_t ch = *(uint16_t *)(mtp_buf + off);
            d->manufacturer[ci++] = (ch < 128) ? (char)ch : '?';
            off += 2;
        }
        d->manufacturer[ci] = 0;
    }

    /* model */
    if (off + 2 <= r) {
        uint16_t slen = *(uint16_t *)(mtp_buf + off);
        off += 2;
        int ci = 0;
        for (int i = 0; i < slen && off + 2 <= r && ci < 127; i++) {
            uint16_t ch = *(uint16_t *)(mtp_buf + off);
            d->model[ci++] = (ch < 128) ? (char)ch : '?';
            off += 2;
        }
        d->model[ci] = 0;
    }

    /* serial */
    if (off + 2 <= r) {
        uint16_t slen = *(uint16_t *)(mtp_buf + off);
        off += 2;
        int ci = 0;
        for (int i = 0; i < slen && off + 2 <= r && ci < 127; i++) {
            uint16_t ch = *(uint16_t *)(mtp_buf + off);
            d->serial[ci++] = (ch < 128) ? (char)ch : '?';
            off += 2;
        }
        d->serial[ci] = 0;
    }
}

int usb_mtp_init(void) {
    mtp_num_devices = 0;
    mtp_ok = 0;
    mtp_req_id = 0;

    int nd = ehci_get_num_devs();
    for (int i = 0; i < nd; i++) {
        struct usb_dev *d = ehci_get_dev(i);
        if (!d) continue;
        /* MTP: class=0x06, subclass=0x01, protocol=0x01 */
        /* PTP: class=0x06, subclass=0x01, protocol=0x01 */
        if (d->class_code == 0x06 && d->subclass == 0x01) {
            if (mtp_num_devices >= MTP_MAX_DEVICES) break;

            struct mtp_device *md = &mtp_devs[mtp_num_devices];
            memset(md, 0, sizeof(*md));
            md->dev = d;
            md->slot = mtp_num_devices;

            /* Open session */
            uint32_t session = 1;
            mtp_send_request(md, MTP_OP_OPEN_SESSION, &session, 1);
            md->session_id = 1;

            /* Get device info */
            mtp_get_device_info(md);

            kprintf("usb_mtp: MTP device at dev %d: \"%s %s\" (s/n: %s)\n",
                    i, md->manufacturer, md->model, md->serial);
            mtp_num_devices++;
        }
    }

    if (mtp_num_devices > 0) {
        mtp_ok = 1;
        kprintf("usb_mtp: %d device(s) detected\n", mtp_num_devices);
    }
    return (mtp_num_devices > 0) ? 0 : -1;
}

int usb_mtp_available(void) { return mtp_ok; }
int usb_mtp_get_device_count(void) { return mtp_num_devices; }

int usb_mtp_open_session(int dev) {
    if (dev < 0 || dev >= mtp_num_devices) return -1;
    uint32_t session = 2;
    mtp_send_request(&mtp_devs[dev], MTP_OP_OPEN_SESSION, &session, 1);
    mtp_devs[dev].session_id = 2;
    return 0;
}

int usb_mtp_close_session(int dev) {
    if (dev < 0 || dev >= mtp_num_devices) return -1;
    mtp_send_request(&mtp_devs[dev], MTP_OP_CLOSE_SESSION, NULL, 0);
    mtp_devs[dev].session_id = 0;
    return 0;
}

int usb_mtp_get_storage_count(int dev) {
    if (dev < 0 || dev >= mtp_num_devices) return 0;
    struct mtp_device *md = &mtp_devs[dev];

    uint32_t params[1] = { 0xFFFFFFFF };
    int r = mtp_send_and_recv(md, MTP_OP_GET_STORAGE_IDS, params, 1,
                              mtp_buf, MTP_TX_BUF_SIZE);
    if (r < 4) return 0;
    uint32_t num = *(uint32_t *)mtp_buf;
    if (num > 8) num = 8;
    md->num_storage = num;
    for (uint32_t i = 0; i < num && (int)(4 + i * 4) < r; i++) {
        md->storage_ids[i] = *(uint32_t *)(mtp_buf + 4 + i * 4);
    }
    return num;
}

int usb_mtp_get_free_space(int dev, int storage_idx) {
    (void)dev; (void)storage_idx;
    /* Would need to parse storage info response — simplified */
    return -1;
}

int usb_mtp_get_used_space(int dev, int storage_idx) {
    (void)dev; (void)storage_idx;
    return -1;
}

int usb_mtp_get_object_count(int dev) {
    if (dev < 0 || dev >= mtp_num_devices) return 0;

    uint32_t params[3] = { 0x00001001, 0, 0xFFFFFFFF };
    int r = mtp_send_and_recv(&mtp_devs[dev], MTP_OP_GET_NUM_OBJECTS, params, 3,
                              mtp_buf, MTP_TX_BUF_SIZE);
    if (r < 4) return 0;
    return *(uint32_t *)mtp_buf;
}

int usb_mtp_get_object(int dev, uint32_t handle, void *buf, int max_len) {
    if (dev < 0 || dev >= mtp_num_devices) return -1;

    uint32_t params[1] = { handle };
    return mtp_send_and_recv(&mtp_devs[dev], MTP_OP_GET_OBJECT, params, 1,
                             buf, max_len);
}

int usb_mtp_get_partial_object(int dev, uint32_t handle, uint32_t offset,
                               uint32_t len, void *buf, int max_len) {
    if (dev < 0 || dev >= mtp_num_devices) return -1;

    uint32_t params[3] = { handle, offset, len };
    return mtp_send_and_recv(&mtp_devs[dev], MTP_OP_GET_PARTIAL_OBJECT, params, 3,
                             buf, max_len);
}

int usb_mtp_send_object(int dev, uint32_t parent, const char *name,
                        uint32_t format, const void *data, int data_len) {
    if (dev < 0 || dev >= mtp_num_devices) return -1;
    (void)parent; (void)name; (void)format; (void)data; (void)data_len;
    /* Simplified: would need to build object info and send object */
    return -1;
}

void usb_mtp_print_info(void) {
    if (!mtp_ok) {
        kprintf("usb_mtp: no MTP devices\n");
        return;
    }
    kprintf("usb_mtp: %d device(s)\n", mtp_num_devices);
    for (int i = 0; i < mtp_num_devices; i++) {
        struct mtp_device *d = &mtp_devs[i];
        kprintf("  dev %d: \"%s %s\" s/n=%s storage=%d\n",
                i, d->manufacturer, d->model, d->serial, d->num_storage);
    }
}

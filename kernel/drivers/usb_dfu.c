#include "usb_dfu.h"
#include "usb_ehci.h"
#include "../kernel/kprintf.h"
#include "../kernel/string.h"
#include "../kernel/pmm.h"
#include "../kernel/vmm.h"

static struct dfu_device dfu_devs[DFU_MAX_DEVICES];
static int dfu_num_devices;
static int dfu_ok;

static uint8_t dfu_buf[4096] __attribute__((aligned(16)));

#pragma pack(push, 1)
struct dfu_status {
    uint8_t  status;
    uint32_t poll_timeout;
    uint8_t  state;
    uint8_t  iString;
};
#pragma pack(pop)

static int dfu_send_request(int dev, uint8_t request, uint16_t value,
                            uint16_t index, uint16_t len, void *data) {
    if (dev < 0 || dev >= dfu_num_devices) return -1;
    struct usb_dev *d = dfu_devs[dev].dev;
    if (!d) return -1;
    /* DFU requests: bmRequestType=0x21 (host-to-device, class, interface) for DNLOAD,
     * 0xA1 (device-to-host, class, interface) for GETSTATUS/UPLOAD */
    int dir = (request == DFU_DNLOAD || request == DFU_CLRSTATUS ||
               request == DFU_ABORT || request == DFU_DETACH) ? USB_DIR_OUT : USB_DIR_IN;
    uint8_t req_type = dir ? 0x21 : 0xA1;
    return ehci_control_transfer(d, dir, req_type, request, value, index, len, data);
}

int usb_dfu_init(void) {
    dfu_num_devices = 0;
    dfu_ok = 0;

    int nd = ehci_get_num_devs();
    for (int i = 0; i < nd; i++) {
        struct usb_dev *d = ehci_get_dev(i);
        if (!d) continue;
        /* DFU: class=0xFE, subclass=0x01, protocol=0x01 (runtime) or 0x02 (DFU mode) */
        if (d->class_code == 0xFE && d->subclass == 0x01) {
            if (dfu_num_devices >= DFU_MAX_DEVICES) break;

            struct dfu_device *df = &dfu_devs[dfu_num_devices];
            memset(df, 0, sizeof(*df));
            df->dev = d;
            df->slot = dfu_num_devices;
            df->interface = 0;
            df->state = DFU_STATE_APP_IDLE;
            df->can_download = (d->protocol == 0x02);
            df->can_upload = (d->protocol == 0x02);

            kprintf("usb_dfu: DFU device at dev %d (VID=%04x PID=%04x) %s\n",
                    i, d->vendor_id, d->product_id,
                    d->protocol == 0x02 ? "(DFU mode)" : "(runtime)");
            dfu_num_devices++;
        }
    }

    if (dfu_num_devices > 0) {
        dfu_ok = 1;
        kprintf("usb_dfu: %d device(s) detected\n", dfu_num_devices);
    }
    return (dfu_num_devices > 0) ? 0 : -1;
}

int usb_dfu_available(void) { return dfu_ok; }
int usb_dfu_get_device_count(void) { return dfu_num_devices; }

int usb_dfu_detach(int dev) {
    if (dev < 0 || dev >= dfu_num_devices) return -1;
    int r = dfu_send_request(dev, DFU_DETACH, 0, dfu_devs[dev].interface, 0, NULL);
    dfu_devs[dev].detached = 1;
    dfu_devs[dev].state = DFU_STATE_APP_DETACH;
    return r;
}

int usb_dfu_download(int dev, int alt, const void *data, int len) {
    if (dev < 0 || dev >= dfu_num_devices) return -1;
    if (!dfu_devs[dev].can_download) return -1;
    if (len > 4096) len = 4096;

    memcpy(dfu_buf, data, len);
    int r = dfu_send_request(dev, DFU_DNLOAD, alt,
                             dfu_devs[dev].interface, len, dfu_buf);
    /* Poll status */
    struct dfu_status st;
    for (int i = 0; i < 1000; i++) {
        for (volatile int j = 0; j < 10000; j++) asm volatile("pause");
        if (dfu_send_request(dev, DFU_GETSTATUS, 0, dfu_devs[dev].interface,
                             sizeof(st), &st) == 0) {
            dfu_devs[dev].state = st.state;
            if (st.state != DFU_STATE_DFU_DNBUSY) break;
        }
    }
    return r;
}

int usb_dfu_upload(int dev, int alt, void *buf, int max_len) {
    if (dev < 0 || dev >= dfu_num_devices) return -1;
    if (!dfu_devs[dev].can_upload) return -1;

    return dfu_send_request(dev, DFU_UPLOAD, alt,
                            dfu_devs[dev].interface, max_len, buf);
}

int usb_dfu_get_status(int dev, int *state, int *poll_timeout) {
    if (dev < 0 || dev >= dfu_num_devices) return -1;

    struct dfu_status st;
    int r = dfu_send_request(dev, DFU_GETSTATUS, 0, dfu_devs[dev].interface,
                             sizeof(st), &st);
    if (r < 0) return -1;

    dfu_devs[dev].state = st.state;
    if (state) *state = st.state;
    if (poll_timeout) *poll_timeout = st.poll_timeout;
    return 0;
}

int usb_dfu_get_state(int dev) {
    if (dev < 0 || dev >= dfu_num_devices) return -1;
    uint8_t state = 0;
    int r = dfu_send_request(dev, DFU_GETSTATE, 0, dfu_devs[dev].interface,
                             1, &state);
    return (r >= 0) ? (int)state : -1;
}

int usb_dfu_clear_status(int dev) {
    if (dev < 0 || dev >= dfu_num_devices) return -1;
    return dfu_send_request(dev, DFU_CLRSTATUS, 0, dfu_devs[dev].interface, 0, NULL);
}

int usb_dfu_abort(int dev) {
    if (dev < 0 || dev >= dfu_num_devices) return -1;
    return dfu_send_request(dev, DFU_ABORT, 0, dfu_devs[dev].interface, 0, NULL);
}

void usb_dfu_print_info(void) {
    if (!dfu_ok) {
        kprintf("usb_dfu: no DFU devices\n");
        return;
    }
    kprintf("usb_dfu: %d device(s)\n", dfu_num_devices);
    for (int i = 0; i < dfu_num_devices; i++) {
        struct dfu_device *d = &dfu_devs[i];
        kprintf("  dev %d: VID=%04x PID=%04x state=%d %s\n",
                i, d->dev->vendor_id, d->dev->product_id,
                d->state, d->detached ? "(detached)" : "");
    }
}

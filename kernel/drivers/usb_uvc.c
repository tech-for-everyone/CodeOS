#include "usb_uvc.h"
#include "usb_ehci.h"
#include "../kernel/kprintf.h"
#include "../kernel/string.h"
#include "../kernel/pmm.h"
#include "../kernel/vmm.h"

static struct uvc_camera uvc_cameras[UVC_MAX_CAMERAS];
static int uvc_num_cameras;
static int uvc_ok;

static void uvc_add_format(int cam, int w, int h, int fps, int fmt, int bpp) {
    struct uvc_camera *c = &uvc_cameras[cam];
    if (c->num_formats >= UVC_MAX_FORMATS) return;
    struct uvc_format *f = &c->formats[c->num_formats];
    f->width = w;
    f->height = h;
    f->fps = fps;
    f->format = fmt;
    f->bits_per_pixel = bpp;
    f->max_packet_mult = 0;
    c->num_formats++;
}

static int uvc_set_cur(struct usb_dev *dev, uint8_t iface, uint8_t ep,
                        uint8_t cs, uint8_t unit, uint16_t len, const void *data) {
    (void)ep;
    return ehci_control_transfer(dev, USB_DIR_OUT, 0x21, UVC_SET_CUR,
                                 (cs << 8), (unit << 8) | iface,
                                 len, (void *)data);
}

int usb_uvc_init(void) {
    uvc_num_cameras = 0;
    uvc_ok = 0;

    int nd = ehci_get_num_devs();
    for (int i = 0; i < nd; i++) {
        struct usb_dev *d = ehci_get_dev(i);
        if (!d) continue;
        /* Video class: 0x0E */
        if (d->class_code == 0x0E) {
            if (uvc_num_cameras >= UVC_MAX_CAMERAS) break;

            struct uvc_camera *cam = &uvc_cameras[uvc_num_cameras];
            memset(cam, 0, sizeof(*cam));
            cam->dev = d;
            cam->ep_in = d->ep_in_addr & 0x0F;

            /* Add default formats: common webcam resolutions */
            uvc_add_format(uvc_num_cameras, 640, 480, 30, 0, 16);
            uvc_add_format(uvc_num_cameras, 800, 600, 20, 0, 16);
            uvc_add_format(uvc_num_cameras, 1024, 768, 15, 0, 16);
            uvc_add_format(uvc_num_cameras, 1280, 720, 10, 0, 16);
            uvc_add_format(uvc_num_cameras, 1920, 1080, 5, 0, 16);
            uvc_add_format(uvc_num_cameras, 640, 480, 30, 1, 24);
            uvc_add_format(uvc_num_cameras, 1280, 720, 10, 1, 24);

            kprintf("usb_uvc: camera at dev %d (VID=%04x PID=%04x), %d formats\n",
                    i, d->vendor_id, d->product_id, cam->num_formats);
            uvc_num_cameras++;
        }
    }

    if (uvc_num_cameras > 0) {
        uvc_ok = 1;
        kprintf("usb_uvc: %d camera(s) detected\n", uvc_num_cameras);
    }
    return (uvc_num_cameras > 0) ? 0 : -1;
}

int usb_uvc_available(void) { return uvc_ok; }
int usb_uvc_get_camera_count(void) { return uvc_num_cameras; }

int usb_uvc_get_format_count(int cam) {
    if (cam < 0 || cam >= uvc_num_cameras) return 0;
    return uvc_cameras[cam].num_formats;
}

struct uvc_format *usb_uvc_get_format(int cam, int fmt) {
    if (cam < 0 || cam >= uvc_num_cameras) return NULL;
    if (fmt < 0 || fmt >= uvc_cameras[cam].num_formats) return NULL;
    return &uvc_cameras[cam].formats[fmt];
}

int usb_uvc_open(int cam) {
    if (cam < 0 || cam >= uvc_num_cameras) return -1;
    struct uvc_camera *c = &uvc_cameras[cam];
    c->open = 1;
    c->frame_ready = 0;
    c->frame_size = 0;
    return 0;
}

int usb_uvc_close(int cam) {
    if (cam < 0 || cam >= uvc_num_cameras) return -1;
    struct uvc_camera *c = &uvc_cameras[cam];
    c->open = 0;
    c->streaming = 0;
    return 0;
}

int usb_uvc_set_format(int cam, int width, int height, int fps) {
    if (cam < 0 || cam >= uvc_num_cameras) return -1;
    struct uvc_camera *c = &uvc_cameras[cam];

    for (int i = 0; i < c->num_formats; i++) {
        struct uvc_format *f = &c->formats[i];
        if (f->width == (uint32_t)width && f->height == (uint32_t)height &&
            f->fps == (uint32_t)fps) {
            c->cur_width = width;
            c->cur_height = height;
            c->cur_fps = fps;
            c->cur_format = f->format;
            return 0;
        }
    }
    return -1;
}

int usb_uvc_start_streaming(int cam) {
    if (cam < 0 || cam >= uvc_num_cameras) return -1;
    struct uvc_camera *c = &uvc_cameras[cam];
    if (!c->open) return -1;

    /* Set alt interface to start streaming */
    uint8_t data[1] = { 0x01 };
    uvc_set_cur(c->dev, 0, c->ep_in, 0x01, 0, 1, data);

    c->streaming = 1;
    c->frame_ready = 0;
    kprintf("usb_uvc: camera %d streaming started (%dx%d@%d)\n",
            cam, c->cur_width, c->cur_height, c->cur_fps);
    return 0;
}

int usb_uvc_stop_streaming(int cam) {
    if (cam < 0 || cam >= uvc_num_cameras) return -1;
    struct uvc_camera *c = &uvc_cameras[cam];

    uint8_t data[1] = { 0x00 };
    uvc_set_cur(c->dev, 0, c->ep_in, 0x01, 0, 1, data);

    c->streaming = 0;
    return 0;
}

static void uvc_process_mjpeg(int cam, const uint8_t *data, int len) {
    struct uvc_camera *c = &uvc_cameras[cam];
    /* MJPEG: strip FHD (Frame Information Header) if present */
    int offset = 0;
    if (len >= 2 && data[0] == 0xFF && data[1] == 0xD8) {
        /* JPEG SOI found — no FHD header */
    } else if (len >= 12) {
        /* Skip FHD: 2 byte hdr_len + 10 bytes FHD */
        offset = 2 + 10;
        if (offset > len) offset = 0;
    }

    int payload = len - offset;
    if (payload > UVC_FRAME_BUF_SIZE) payload = UVC_FRAME_BUF_SIZE;
    if (payload > 0) {
        memcpy(c->frame_buf, data + offset, payload);
        c->frame_size = payload;
        c->frame_ready = 1;
    }
}

static void uvc_process_uncompressed(int cam, const uint8_t *data, int len) {
    struct uvc_camera *c = &uvc_cameras[cam];
    /* Uncompressed: skip 2-byte header */
    int offset = (len >= 2) ? 2 : 0;
    int payload = len - offset;
    if (payload > UVC_FRAME_BUF_SIZE) payload = UVC_FRAME_BUF_SIZE;
    if (payload > 0) {
        memcpy(c->frame_buf, data + offset, payload);
        c->frame_size = payload;
        c->frame_ready = 1;
    }
}

int usb_uvc_read_frame(int cam, void *buf, int max_len) {
    if (cam < 0 || cam >= uvc_num_cameras) return -1;
    struct uvc_camera *c = &uvc_cameras[cam];
    if (!c->open || !c->streaming) return -1;

    /* Try to receive a frame */
    uint8_t pkt_buf[2048] __attribute__((aligned(4)));
    int r = ehci_bulk_transfer(c->dev, c->ep_in, USB_DIR_IN, pkt_buf, sizeof(pkt_buf));
    if (r <= 0) return -1;

    if (c->cur_format == 1)
        uvc_process_mjpeg(cam, pkt_buf, r);
    else
        uvc_process_uncompressed(cam, pkt_buf, r);

    if (!c->frame_ready) return 0;

    int cpy = c->frame_size < max_len ? c->frame_size : max_len;
    memcpy(buf, c->frame_buf, cpy);
    c->frame_ready = 0;
    return cpy;
}

int usb_uvc_poll_frame(int cam) {
    if (cam < 0 || cam >= uvc_num_cameras) return -1;
    struct uvc_camera *c = &uvc_cameras[cam];
    if (!c->open || !c->streaming) return -1;
    return c->frame_ready ? 1 : 0;
}

void usb_uvc_print_info(void) {
    if (!uvc_ok) {
        kprintf("usb_uvc: no cameras\n");
        return;
    }
    kprintf("usb_uvc: %d camera(s)\n", uvc_num_cameras);
    for (int i = 0; i < uvc_num_cameras; i++) {
        struct uvc_camera *c = &uvc_cameras[i];
        kprintf("  camera %d: %d format(s) %s\n",
                i, c->num_formats,
                c->streaming ? "(streaming)" : (c->open ? "(open)" : ""));
        for (int j = 0; j < c->num_formats; j++) {
            struct uvc_format *f = &c->formats[j];
            kprintf("    %d: %dx%d@%dfps %s %dbpp\n",
                    j, f->width, f->height, f->fps,
                    f->format ? "MJPEG" : "RAW", f->bits_per_pixel);
        }
    }
}

/* ARM64 input layer.
 *
 * Two sources, both polled (the kernel has no GIC/IRQ setup):
 *   - virtio-mmio input devices: `-device virtio-tablet-device` gives the
 *     phone UI an absolute pointer (the touch-like path), and
 *     `-device virtio-keyboard-device` gives it keys.  The device is found by
 *     scanning the QEMU `virt` virtio-mmio window at 0x0A000000, so no device
 *     tree parsing is needed.
 *   - the PL011 serial console, decoded as a keyboard.  This keeps the UI
 *     navigable in a plain `-nographic` boot with no input devices attached.
 */
#include "input.h"
#include "serial.h"
#include "fb.h"
#include "io.h"
#include "kprintf.h"
#include "types.h"

/* ────────────────────────────────────────────────────────────────────────
 * Event queue
 * ──────────────────────────────────────────────────────────────────────── */

#define IN_Q 64

static input_event_t in_queue[IN_Q];
static int in_head, in_tail;

static void q_push(const input_event_t *e) {
    int next = (in_head + 1) % IN_Q;
    if (next == in_tail)
        return; /* drop when full; the UI drains it every loop */
    in_queue[in_head] = *e;
    in_head = next;
}

static int q_pop(input_event_t *e) {
    if (in_tail == in_head)
        return 0;
    *e = in_queue[in_tail];
    in_tail = (in_tail + 1) % IN_Q;
    return 1;
}

/* ────────────────────────────────────────────────────────────────────────
 * Serial console → keyboard
 * ──────────────────────────────────────────────────────────────────────── */

static int ser_state;   /* 0 idle, 1 seen ESC, 2 saw ESC [ */

static void serial_pump(void) {
    while (serial_available()) {
        int c = (unsigned char)serial_readchar();

        if (ser_state == 1) {
            if (c == '[') { ser_state = 2; continue; }
            ser_state = 0;
            q_push(&(input_event_t){ .type = IN_KEY, .key = IN_KEY_BACK });
            /* fall through and decode c normally */
        } else if (ser_state == 2) {
            ser_state = 0;
            if      (c == 'A') q_push(&(input_event_t){ .type = IN_KEY, .key = IN_KEY_UP });
            else if (c == 'B') q_push(&(input_event_t){ .type = IN_KEY, .key = IN_KEY_DOWN });
            else if (c == 'C') q_push(&(input_event_t){ .type = IN_KEY, .key = IN_KEY_RIGHT });
            else if (c == 'D') q_push(&(input_event_t){ .type = IN_KEY, .key = IN_KEY_LEFT });
            else               ; /* unknown prefix */
        } else if (ser_state == 0) {
            if      (c == 0x1b)      { ser_state = 1; }
            else if (c == '\r' || c == '\n') q_push(&(input_event_t){ .type = IN_KEY, .key = IN_KEY_ENTER });
            else if (c == 0x7f || c == 0x08) q_push(&(input_event_t){ .type = IN_KEY, .key = IN_KEY_BACK });
            else                     q_push(&(input_event_t){ .type = IN_KEY, .key = c });
        }
    }
}

/* ────────────────────────────────────────────────────────────────────────
 * virtio-mmio input
 * ──────────────────────────────────────────────────────────────────────── */

#define VIRTIO_MMIO_BASE   0x0A000000UL
#define VIRTIO_MMIO_STRIDE 0x200UL
#define VIRTIO_MMIO_SLOTS  32
#define VIRTIO_ID_INPUT    18

#define VIO_MAGIC               0x000
#define VIO_VERSION             0x004
#define VIO_DEVICE_ID           0x008
#define VIO_DEVICE_FEATURES     0x010
#define VIO_DEVICE_FEATURES_SEL 0x014
#define VIO_DRIVER_FEATURES     0x020
#define VIO_DRIVER_FEATURES_SEL 0x024
#define VIO_QUEUE_SEL           0x030
#define VIO_QUEUE_NUM_MAX       0x034
#define VIO_QUEUE_NUM           0x038
#define VIO_QUEUE_READY         0x044
#define VIO_QUEUE_NOTIFY        0x050
#define VIO_STATUS              0x070
#define VIO_QUEUE_DESC_LOW      0x080
#define VIO_QUEUE_DESC_HIGH     0x084
#define VIO_QUEUE_DRIVER_LOW    0x090
#define VIO_QUEUE_DRIVER_HIGH   0x094
#define VIO_QUEUE_DEVICE_LOW    0x0A0
#define VIO_QUEUE_DEVICE_HIGH   0x0A4
#define VIO_CONFIG              0x100

#define VIRTIO_STATUS_ACK         1
#define VIO_STATUS_DRIVER         2
#define VIO_STATUS_DRIVER_OK      4
#define VIO_STATUS_FEATURES_OK    8

#define VRING_DESC_F_WRITE 2

#define VIO_CFG_ID_NAME  0x01
#define VIO_CFG_EV_BITS  0x11
#define VIO_CFG_ABS_INFO 0x12

#define EV_SYN 0
#define EV_KEY 1
#define EV_ABS 3
#define SYN_REPORT 0
#define ABS_X 0
#define ABS_Y 1
#define BTN_LEFT  0x110
#define BTN_TOUCH 0x14a

/* Exactly struct virtio_input_event: 8 bytes. */
typedef struct {
    uint16_t type;
    uint16_t code;
    uint32_t value;
} vio_event_t;

typedef struct {
    uint64_t base;
    int present;
    int is_pointer;
    uint16_t qnum;
    /* Pointer absolute range, from the device config. */
    uint32_t abs_min_x, abs_max_x, abs_min_y, abs_max_y;

    uint16_t last_used;
    int notify;
    /* Pointer state, grouped per SYN. */
    int have_pos;
    uint32_t pos_x, pos_y;
    int pending_move;
    int pending_btn;   /* -1 none, 0 up, 1 down */
} vio_input_t;

static vio_input_t vio_devs[2];
static int vio_count;

static inline uint32_t vio_rd(vio_input_t *d, uint32_t off) {
    return mmio_read32(d->base + off);
}
static inline void vio_wr(vio_input_t *d, uint32_t off, uint32_t v) {
    mmio_write32(d->base + off, v);
}

static int vio_read_cfg(vio_input_t *d, uint8_t sel, uint8_t sub,
                        void *buf, uint32_t len) {
    volatile uint8_t *c = (volatile uint8_t *)(d->base + VIO_CONFIG);
    c[0] = sel;
    c[1] = sub;
    dmb();
    uint32_t size = c[2];
    if (size == 0) return 0;
    if (size > len) size = len;
    for (uint32_t i = 0; i < size; i++)
        ((uint8_t *)buf)[i] = c[8 + i];
    return (int)size;
}

static int vio_vq_setup(vio_input_t *d, uint16_t qi) {
    vio_wr(d, VIO_QUEUE_SEL, qi);
    uint32_t max = vio_rd(d, VIO_QUEUE_NUM_MAX);
    if (max == 0) return 0;
    uint32_t num = max < VQ_N ? max : VQ_N;
    if (num < 2) return 0;
    d->qnum = (uint16_t)num;
    vio_wr(d, VIO_QUEUE_NUM, num);

    uint64_t da = (uint64_t)(uintptr_t)d->desc;
    uint64_t aa = (uint64_t)(uintptr_t)&d->avail;
    uint64_t ua = (uint64_t)(uintptr_t)&d->used;
    vio_wr(d, VIO_QUEUE_DESC_LOW,    (uint32_t)da);
    vio_wr(d, VIO_QUEUE_DESC_HIGH,   (uint32_t)(da >> 32));
    vio_wr(d, VIO_QUEUE_DRIVER_LOW,  (uint32_t)aa);
    vio_wr(d, VIO_QUEUE_DRIVER_HIGH, (uint32_t)(aa >> 32));
    vio_wr(d, VIO_QUEUE_DEVICE_LOW,  (uint32_t)ua);
    vio_wr(d, VIO_QUEUE_DEVICE_HIGH, (uint32_t)(ua >> 32));
    vio_wr(d, VIO_QUEUE_READY, 1);
    return 1;
}

static int vio_init(vio_input_t *d, uint64_t base) {
    d->base = base;
    if (mmio_read32(base + VIO_MAGIC) != 0x74726976) return 0;
    if (mmio_read32(base + VIO_VERSION) != 2) return 0;
    if (mmio_read32(base + VIO_DEVICE_ID) != VIRTIO_ID_INPUT) return 0;

    /* Reset and handshake. */
    vio_wr(d, VIO_QUEUE_SEL, 0);
    vio_wr(d, VIO_QUEUE_READY, 0);
    vio_wr(d, VIO_STATUS, 0);
    vio_wr(d, VIO_STATUS, 1);            /* ACKNOWLEDGE */
    vio_wr(d, VIO_STATUS, 1 | 2);        /* DRIVER */
    vio_wr(d, VIO_DEVICE_FEATURES_SEL, 0); (void)vio_rd(d, VIO_DEVICE_FEATURES);
    vio_wr(d, VIO_DEVICE_FEATURES_SEL, 1); (void)vio_rd(d, VIO_DEVICE_FEATURES);
    vio_wr(d, VIO_DRIVER_FEATURES_SEL, 0);
    vio_wr(d, VIO_DRIVER_FEATURES, 0);
    vio_wr(d, VIO_DRIVER_FEATURES_SEL, 1);
    vio_wr(d, VIO_DRIVER_FEATURES, 1u);          /* VIRTIO_F_VERSION_1 (bit 32) */
    vio_wr(d, VIO_STATUS, 1 | 2 | 8);            /* FEATURES_OK */
    if (!(vio_rd(d, VIO_STATUS) & 8)) {
        kprintf("input: %lx rejected FEATURES_OK, skipping\n", base);
        return 0;
    }

    /* Does the device report absolute axes?  If so it is a pointer. */
    uint8_t bmp[16];
    d->is_pointer = 0;
    if (vio_read_cfg(d, VIO_CFG_EV_BITS, EV_ABS, bmp, sizeof(bmp)) > 0 &&
        (bmp[ABS_X / 8] & (1u << (ABS_X % 8))))
        d->is_pointer = 1;

    if (d->is_pointer) {
        struct { uint32_t min, max, fuzz, flat, res; } ai;
        d->abs_min_x = 0; d->abs_max_x = 32767;
        d->abs_min_y = 0; d->abs_max_y = 32767;
        if (vio_read_cfg(d, VIO_CFG_ABS_INFO, ABS_X, &ai, sizeof(ai)) > 0) {
            d->abs_min_x = ai.min; d->abs_max_x = ai.max;
        }
        if (vio_read_cfg(d, VIO_CFG_ABS_INFO, ABS_Y, &ai, sizeof(ai)) > 0) {
            d->abs_min_y = ai.min; d->abs_max_y = ai.max;
        }
    }

    if (!vio_vq_setup(d, 0)) {
        kprintf("input: %lx queue setup failed\n", base);
        return 0;
    }

    /* Post all event buffers. */
    for (uint16_t i = 0; i < d->qnum; i++) {
        d->desc[i].addr  = (uint64_t)(uintptr_t)&d->evbuf[i];
        d->desc[i].len   = sizeof(vio_event_t);
        d->desc[i].flags = 2;   /* VRING_DESC_F_WRITE: device writes */
        d->desc[i].next  = 0;
        d->avail.ring[i] = i;
    }
    d->pending_btn = -1;
    dmb();
    d->avail.idx = d->qnum;
    dmb();
    vio_wr(d, VIO_QUEUE_NOTIFY, 0);

    vio_wr(d, VIO_STATUS, 1 | 2 | 8 | 4);        /* DRIVER_OK */
    d->present = 1;

    char name[16];
    for (int i = 0; i < 15; i++) name[i] = 0;
    name[15] = 0;
    vio_read_cfg(d, VIO_CFG_ID_NAME, 0, name, 15);
    kprintf("input: %s at 0x%lx (%s)\n", name, base,
            d->is_pointer ? "pointer" : "keyboard");
    if (d->is_pointer)
        kprintf("input: abs x=[%u,%u] y=[%u,%u]\n",
                d->abs_min_x, d->abs_max_x, d->abs_min_y, d->abs_max_y);
    return 1;
}

static void vio_push_key(uint16_t code, uint32_t value, int is_pointer) {
    if (!value) return;                 /* key-up ignored */
    if (is_pointer) return;
    input_event_t e = { .type = IN_KEY, .key = 0 };
    switch (code) {
    case 1:  e.key = IN_KEY_BACK;  break;   /* KEY_ESC */
    case 14: e.key = IN_KEY_BACK;  break;   /* KEY_BACKSPACE */
    case 28: e.key = IN_KEY_ENTER; break;   /* KEY_ENTER */
    case 96: e.key = IN_KEY_ENTER; break;   /* KEY_KPENTER */
    case 103: e.key = IN_KEY_UP;    break;
    case 108: e.key = IN_KEY_DOWN;  break;
    case 105: e.key = IN_KEY_LEFT;  break;
    case 106: e.key = IN_KEY_RIGHT; break;
    case 11:  e.key = '0'; break;
    case 12:  e.key = '-'; break;
    case 13:  e.key = '='; break;
    case 52:  e.key = '.'; break;
    case 53:  e.key = '/'; break;
    case 55:  e.key = '*'; break;
    case 78:  e.key = '+'; break;
    default:
        if (code >= 2 && code <= 10) e.key = '1' + (code - 2);
        else return;
        break;
    }
    if (e.key) q_push(&e);
}

static void vio_process(vio_input_t *d, const vio_event_t *ev) {
    if (ev->type == EV_SYN && ev->code == SYN_REPORT) {
        if (d->pending_btn >= 0) {
            input_event_t e = { .type = d->pending_btn ? IN_TOUCH_DOWN : IN_TOUCH_UP };
            e.x = (int)((uint64_t)d->pos_x * fb_getwidth() / 0x7fff);
            e.y = (int)((uint64_t)d->pos_y * fb_getheight() / 0x7fff);
            if (d->abs_max_x > d->abs_min_x)
                e.x = (int)((uint64_t)(d->pos_x - d->abs_min_x) * fb_getwidth() /
                            (d->abs_max_x - d->abs_min_x));
            if (d->abs_max_y > d->abs_min_y)
                e.y = (int)((uint64_t)(d->pos_y - d->abs_min_y) * fb_getheight() /
                            (d->abs_max_y - d->abs_min_y));
            if (e.x < 0) e.x = 0;
            if (e.y < 0) e.y = 0;
            if (e.x >= (int)fb_getwidth())  e.x = (int)fb_getwidth()  - 1;
            if (e.y >= (int)fb_getheight()) e.y = (int)fb_getheight() - 1;
            q_push(&e);
            d->pending_btn = -1;
        } else if (d->pending_move && d->have_pos) {
            input_event_t e = { .type = IN_TOUCH_MOVE };
            e.x = (int)((uint64_t)d->pos_x * fb_getwidth() / 0x7fff);
            e.y = (int)((uint64_t)d->pos_y * fb_getheight() / 0x7fff);
            if (d->abs_max_x > d->abs_min_x)
                e.x = (int)((uint64_t)(d->pos_x - d->abs_min_x) * fb_getwidth() /
                            (d->abs_max_x - d->abs_min_x));
            if (d->abs_max_y > d->abs_min_y)
                e.y = (int)((uint64_t)(d->pos_y - d->abs_min_y) * fb_getheight() /
                            (d->abs_max_y - d->abs_min_y));
            if (e.x < 0) e.x = 0;
            if (e.y < 0) e.y = 0;
            if (e.x >= (int)fb_getwidth())  e.x = (int)fb_getwidth()  - 1;
            if (e.y >= (int)fb_getheight()) e.y = (int)fb_getheight() - 1;
            q_push(&e);
        }
        d->pending_move = 0;
        return;
    }

    if (d->is_pointer) {
        if (ev->type == EV_ABS) {
            if (ev->code == ABS_X) { d->pos_x = ev->value; d->have_pos = 1; d->pending_move = 1; }
            else if (ev->code == ABS_Y) { d->pos_y = ev->value; d->have_pos = 1; d->pending_move = 1; }
        } else if (ev->type == EV_KEY &&
                   (ev->code == BTN_LEFT || ev->code == BTN_TOUCH)) {
            d->pending_btn = ev->value ? 1 : 0;
        }
    } else if (ev->type == EV_KEY) {
        vio_push_key(ev->code, ev->value, 0);
    }
}

static void vio_poll(vio_input_t *d) {
    if (!d->present) return;
    uint16_t idx = d->used.idx;
    dmb();
    int consumed = 0;
    while (d->last_used != idx) {
        uint16_t slot = d->last_used % d->qnum;
        uint32_t id = d->used.ring[slot].id;
        if (id < VQ_N) {
            vio_process(d, &d->evbuf[id]);
            d->avail.ring[d->avail.idx % d->qnum] = (uint16_t)id;
            dmb();
            d->avail.idx++;
            consumed = 1;
        }
        d->last_used++;
    }
    if (consumed) {
        dmb();
        vio_wr(d, VIO_QUEUE_NOTIFY, 0);
    }
}

/* ────────────────────────────────────────────────────────────────────────
 * Public API
 * ──────────────────────────────────────────────────────────────────────── */

void input_init(void) {
    vio_count = 0;
    for (int i = 0; i < VIRTIO_MMIO_SLOTS && vio_count < 2; i++) {
        uint64_t base = VIRTIO_MMIO_BASE + (uint64_t)i * VIRTIO_MMIO_STRIDE;
        if (mmio_read32(base + VIO_MAGIC) != 0x74726976) continue;
        if (mmio_read32(base + VIO_VERSION) != 2) continue;
        if (mmio_read32(base + VIO_DEVICE_ID) != VIRTIO_ID_INPUT) continue;
        if (vio_init(&vio_devs[vio_count], base)) vio_count++;
    }
    kprintf("input: ready (%d virtio device%s, serial keys)\n",
            vio_count, vio_count == 1 ? "" : "s");
}

int input_poll(input_event_t *out) {
    if (q_pop(out)) return 1;
    for (int i = 0; i < vio_count; i++) vio_poll(&vio_devs[i]);
    if (q_pop(out)) return 1;
    serial_pump();
    return q_pop(out);
}
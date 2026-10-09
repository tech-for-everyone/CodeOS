#include "display_bridge.h"
#include "string.h"
#include "arch/x86_64/fb.h"
#include "kernel/pmm.h"
#include "kernel/vmm.h"
#include "kernel/kprintf.h"

static display_bridge_t bridges[DISPLAY_BRIDGE_MAX];
static int bridge_count = 0;
static int bridge_next_id = 1;

/* ─── Message queue operations (lock-free SPSC) ─── */

static int msg_queue_push(db_msg_queue_t *q, const db_message_t *msg) {
    uint32_t next = (q->head + 1) % DISPLAY_MSG_QUEUE_SIZE;
    if (next == q->tail) return -1; /* full */
    q->messages[q->head] = *msg;
    __sync_synchronize();
    q->head = next;
    return 0;
}

static int msg_queue_pop(db_msg_queue_t *q, db_message_t *msg) {
    if (q->head == q->tail) return -1; /* empty */
    *msg = q->messages[q->tail];
    __sync_synchronize();
    q->tail = (q->tail + 1) % DISPLAY_MSG_QUEUE_SIZE;
    return 0;
}

/* ─── Init ─── */

int display_bridge_init(void) {
    memset(bridges, 0, sizeof(bridges));
    bridge_count = 0;
    bridge_next_id = 1;
    kprintf("[display-bridge] initialized\n");
    return 0;
}

/* ─── Lifecycle ─── */

int display_bridge_create(const char *name, int vm_id) {
    if (bridge_count >= DISPLAY_BRIDGE_MAX) return -1;

    display_bridge_t *b = &bridges[bridge_count];
    memset(b, 0, sizeof(*b));

    b->id = bridge_next_id++;
    b->active = 0;
    b->vm_id = vm_id;
    b->container_id = -1;
    b->needs_full_redraw = 1;
    b->last_blitted_seq = 0;

    if (name) strlcpy(b->name, name, DISPLAY_BRIDGE_NAME_MAX);

    b->guest_to_host.head = 0;
    b->guest_to_host.tail = 0;
    b->host_to_guest.head = 0;
    b->host_to_guest.tail = 0;

    bridge_count++;
    kprintf("[display-bridge] created: %s (id=%d, vm=%d)\n",
                  b->name, b->id, vm_id);
    return b->id;
}

int display_bridge_destroy(int id) {
    for (int i = 0; i < DISPLAY_BRIDGE_MAX; i++) {
        if (bridges[i].id == id) {
            display_bridge_unmap_fb(i);
            memmove(&bridges[i], &bridges[i + 1],
                    (bridge_count - i - 1) * sizeof(display_bridge_t));
            bridge_count--;
            return 0;
        }
    }
    return -1;
}

int display_bridge_activate(int id) {
    for (int i = 0; i < DISPLAY_BRIDGE_MAX; i++) {
        if (bridges[i].id == id) {
            bridges[i].active = 1;
            bridges[i].needs_full_redraw = 1;
            kprintf("[display-bridge] activated: %s\n", bridges[i].name);
            return 0;
        }
    }
    return -1;
}

int display_bridge_deactivate(int id) {
    for (int i = 0; i < DISPLAY_BRIDGE_MAX; i++) {
        if (bridges[i].id == id) {
            bridges[i].active = 0;
            kprintf("[display-bridge] deactivated: %s\n", bridges[i].name);
            return 0;
        }
    }
    return -1;
}

/* ─── Shared framebuffer ─── */

int display_bridge_alloc_fb(int id, uint32_t width, uint32_t height,
                            uint64_t *out_phys, uint32_t *out_size) {
    display_bridge_t *b = NULL;
    for (int i = 0; i < DISPLAY_BRIDGE_MAX; i++) {
        if (bridges[i].id == id) { b = &bridges[i]; break; }
    }
    if (!b) return -1;

    uint32_t stride = width * 4; /* BGRA */
    uint32_t header_size = sizeof(db_shared_fb_t);
    uint32_t total = header_size + stride * height;
    uint32_t pages = (total + 4095) / 4096;

    /* Allocate physical pages for shared framebuffer */
    uint64_t phys = pmm_alloc_pages(pages);
    if (!phys) return -1;

    uint64_t virt = phys_to_virt(phys);
    if (!virt) { pmm_free_pages(phys, pages); return -1; }

    db_shared_fb_t *fb = (db_shared_fb_t *)virt;
    fb->width = width;
    fb->height = height;
    fb->bpp = 32;
    fb->stride = stride;
    fb->dirty_seq = 0;
    fb->ack_seq = 0;
    memset((void *)fb->data, 0, stride * height);

    b->shared_fb = fb;
    b->fb_phys = phys;
    b->fb_size = total;
    b->fb_owned = 1;
    b->fb_pages = pages;
    b->width = width;
    b->height = height;
    b->bpp = 32;
    b->stride = stride;

    *out_phys = phys;
    *out_size = total;

    kprintf("[display-bridge] allocated shared FB: %ux%u (%u bytes) at phys=0x%llx\n",
                  width, height, total, (unsigned long long)phys);
    return 0;
}

int display_bridge_map_fb(int id, uint64_t phys, uint32_t size) {
    display_bridge_t *b = NULL;
    for (int i = 0; i < DISPLAY_BRIDGE_MAX; i++) {
        if (bridges[i].id == id) { b = &bridges[i]; break; }
    }
    if (!b) return -1;

    void *virt = (void *)phys_to_virt(phys);
    if (!virt) return -1;

    b->shared_fb = (db_shared_fb_t *)virt;
    b->fb_phys = phys;
    b->fb_size = size;
    b->fb_owned = 0;
    b->fb_pages = 0;
    b->width = b->shared_fb->width;
    b->height = b->shared_fb->height;
    b->bpp = b->shared_fb->bpp;
    b->stride = b->shared_fb->stride;
    b->needs_full_redraw = 1;

    kprintf("[display-bridge] mapped shared FB at phys=0x%llx\n",
                  (unsigned long long)phys);
    return 0;
}

int display_bridge_unmap_fb(int id) {
    display_bridge_t *b = NULL;
    for (int i = 0; i < DISPLAY_BRIDGE_MAX; i++) {
        if (bridges[i].id == id) { b = &bridges[i]; break; }
    }
    if (!b) return -1;

    if (b->shared_fb) {
        if (b->fb_owned && b->fb_phys && b->fb_pages > 0) {
            pmm_free_pages(b->fb_phys, b->fb_pages);
        }
        b->shared_fb = NULL;
        b->fb_phys = 0;
        b->fb_size = 0;
        b->fb_owned = 0;
        b->fb_pages = 0;
    }
    return 0;
}

/* ─── Message passing ─── */

int display_bridge_push_guest_msg(int id, const db_message_t *msg) {
    for (int i = 0; i < DISPLAY_BRIDGE_MAX; i++) {
        if (bridges[i].id == id)
            return msg_queue_push(&bridges[i].guest_to_host, msg);
    }
    return -1;
}

int display_bridge_pop_host_msg(int id, db_message_t *msg) {
    for (int i = 0; i < DISPLAY_BRIDGE_MAX; i++) {
        if (bridges[i].id == id)
            return msg_queue_pop(&bridges[i].host_to_guest, msg);
    }
    return -1;
}

int display_bridge_push_host_msg(int id, const db_message_t *msg) {
    for (int i = 0; i < DISPLAY_BRIDGE_MAX; i++) {
        if (bridges[i].id == id)
            return msg_queue_push(&bridges[i].host_to_guest, msg);
    }
    return -1;
}

int display_bridge_pop_guest_msg(int id, db_message_t *msg) {
    for (int i = 0; i < DISPLAY_BRIDGE_MAX; i++) {
        if (bridges[i].id == id)
            return msg_queue_pop(&bridges[i].guest_to_host, msg);
    }
    return -1;
}

/* ─── Frame blitting ─── */

int display_bridge_blit_dirty(int id) {
    display_bridge_t *b = NULL;
    for (int i = 0; i < DISPLAY_BRIDGE_MAX; i++) {
        if (bridges[i].id == id) { b = &bridges[i]; break; }
    }
    if (!b || !b->shared_fb || !b->active) return -1;

    uint64_t dirty = b->shared_fb->dirty_seq;
    if (dirty == b->last_blitted_seq && !b->needs_full_redraw)
        return 0; /* no new frames */

    if (b->needs_full_redraw) {
        display_bridge_blit_full(id);
        b->needs_full_redraw = 0;
    } else {
        /* For now, blit full frame on any dirty.
         * A production implementation would track dirty rectangles
         * and only blit changed regions. */
        display_bridge_blit_full(id);
    }

    b->last_blitted_seq = dirty;
    b->shared_fb->ack_seq = dirty;
    return 0;
}

int display_bridge_blit_full(int id) {
    display_bridge_t *b = NULL;
    for (int i = 0; i < DISPLAY_BRIDGE_MAX; i++) {
        if (bridges[i].id == id) { b = &bridges[i]; break; }
    }
    if (!b || !b->shared_fb) return -1;

    /* Copy guest framebuffer to host framebuffer */
    uint32_t src_stride = b->shared_fb->stride;
    uint8_t *src = b->shared_fb->data;

    /* Scale to host display if sizes differ */
    uint32_t host_w = fb_getwidth();
    uint32_t host_h = fb_getheight();

    if (b->width == host_w && b->height == host_h) {
        /* Direct copy — same resolution */
        uint32_t *dst = fb_get_active_buffer();
        uint32_t copy_size = src_stride * b->height;
        if (copy_size > host_w * host_h * 4)
            copy_size = host_w * host_h * 4;
        memcpy(dst, src, copy_size);
    } else {
        /* Scale blit: guest → host with nearest-neighbor sampling */
        uint32_t *dst = fb_get_active_buffer();
        uint32_t dst_stride = host_w * 4;

        for (uint32_t dy = 0; dy < host_h; dy++) {
            uint32_t sy = (dy * b->height) / host_h;
            uint8_t *src_row = src + sy * src_stride;
            uint32_t *dst_row = (uint32_t *)((uint8_t *)dst + dy * dst_stride);

            for (uint32_t dx = 0; dx < host_w; dx++) {
                uint32_t sx = (dx * b->width) / host_w;
                uint32_t sp = sx * 4;

                if (sp + 3 < src_stride) {
                    /* Convert BGRA → uint32_t pixel */
                    uint32_t pixel = src_row[sp] | (src_row[sp+1] << 8) |
                                     (src_row[sp+2] << 16) | (src_row[sp+3] << 24);
                    dst_row[dx] = pixel;
                }
            }
        }
    }

    return 0;
}

int display_bridge_blit_region(int id, uint32_t x, uint32_t y,
                               uint32_t w, uint32_t h) {
    display_bridge_t *b = NULL;
    for (int i = 0; i < DISPLAY_BRIDGE_MAX; i++) {
        if (bridges[i].id == id) { b = &bridges[i]; break; }
    }
    if (!b || !b->shared_fb) return -1;

    uint8_t *src = b->shared_fb->data;
    uint32_t *dst_buf = fb_get_active_buffer();
    uint8_t *dst = (uint8_t *)dst_buf;
    uint32_t src_stride = b->shared_fb->stride;
    uint32_t dst_stride = fb_getwidth() * 4;
    uint32_t host_h = fb_getheight();

    /* Clip to both guest and host bounds */
    if (x >= b->width || y >= b->height) return 0;
    if (x + w > b->width) w = b->width - x;
    if (y + h > b->height) h = b->height - y;

    for (uint32_t dy = 0; dy < h && (y + dy) < host_h; dy++) {
        uint32_t gy = y + dy;
        uint8_t *src_row = src + gy * src_stride + x * 4;
        uint8_t *dst_row = dst + (y + dy) * dst_stride + x * 4;
        uint32_t copy_w = w * 4;
        if (x * 4 + copy_w > dst_stride) copy_w = dst_stride - x * 4;
        memcpy(dst_row, src_row, copy_w);
    }

    return 0;
}

/* ─── Input forwarding ─── */

int display_bridge_send_key(int id, uint32_t key, int down) {
    db_message_t msg;
    memset(&msg, 0, sizeof(msg));
    msg.type = DB_MSG_INPUT_EVENT;
    msg.input.type = down ? DB_INPUT_KEY_DOWN : DB_INPUT_KEY_UP;
    msg.input.key = key;
    return display_bridge_push_host_msg(id, &msg);
}

int display_bridge_send_mouse(int id, int32_t x, int32_t y,
                              uint32_t buttons) {
    db_message_t msg;
    memset(&msg, 0, sizeof(msg));
    msg.type = DB_MSG_INPUT_EVENT;
    msg.input.type = DB_INPUT_MOUSE_MOVE;
    msg.input.x = x;
    msg.input.y = y;
    msg.input.key = buttons;
    return display_bridge_push_host_msg(id, &msg);
}

int display_bridge_send_wheel(int id, int32_t dx, int32_t dy) {
    db_message_t msg;
    memset(&msg, 0, sizeof(msg));
    msg.type = DB_MSG_INPUT_EVENT;
    msg.input.type = DB_INPUT_MOUSE_WHEEL;
    msg.input.dx = dx;
    msg.input.dy = dy;
    return display_bridge_push_host_msg(id, &msg);
}

/* ─── Queries ─── */

display_bridge_t *display_bridge_get(int id) {
    for (int i = 0; i < DISPLAY_BRIDGE_MAX; i++) {
        if (bridges[i].id == id) return &bridges[i];
    }
    return NULL;
}

int display_bridge_count(void) { return bridge_count; }

int display_bridge_get_pending_frames(int id) {
    for (int i = 0; i < DISPLAY_BRIDGE_MAX; i++) {
        if (bridges[i].id == id) {
            db_msg_queue_t *q = &bridges[i].guest_to_host;
            int count = (int)(q->head - q->tail + DISPLAY_MSG_QUEUE_SIZE)
                        % DISPLAY_MSG_QUEUE_SIZE;
            return count;
        }
    }
    return 0;
}

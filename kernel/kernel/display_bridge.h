#ifndef DISPLAY_BRIDGE_H
#define DISPLAY_BRIDGE_H

#include "types.h"

#define DISPLAY_BRIDGE_MAX      8
#define DISPLAY_BRIDGE_NAME_MAX 32
#define DISPLAY_MSG_QUEUE_SIZE  64

/* ─── Display bridge message types ─── */
typedef enum {
    DB_MSG_NONE = 0,
    DB_MSG_FRAME_UPDATE,      /* Guest sent a frame update */
    DB_MSG_CURSOR_UPDATE,     /* Guest moved/changed cursor */
    DB_MSG_RESOLUTION_CHANGE, /* Guest changed resolution */
    DB_MSG_INPUT_EVENT,       /* Host→Guest input */
    DB_MSG_DISPLAY_INFO,      /* Guest→Host display metadata */
    DB_MSG_CLIPBOARD_DATA,    /* Clipboard sync */
    DB_MSG_FOCUS_CHANGE,      /* Window focus changed */
} db_msg_type_t;

/* ─── Input event types (host→guest) ─── */
typedef enum {
    DB_INPUT_NONE = 0,
    DB_INPUT_KEY_DOWN,
    DB_INPUT_KEY_UP,
    DB_INPUT_MOUSE_MOVE,
    DB_INPUT_MOUSE_DOWN,
    DB_INPUT_MOUSE_UP,
    DB_INPUT_MOUSE_WHEEL,
} db_input_type_t;

/* ─── Input event ─── */
typedef struct {
    db_input_type_t type;
    uint32_t key;       /* keycode or mouse button */
    int32_t  x;         /* mouse X position */
    int32_t  y;         /* mouse Y position */
    int32_t  dx;        /* mouse delta X */
    int32_t  dy;        /* mouse delta Y */
    uint32_t modifiers; /* ctrl, alt, shift, etc. */
} db_input_event_t;

/* ─── Frame update ─── */
typedef struct {
    uint32_t x;         /* dirty region X */
    uint32_t y;         /* dirty region Y */
    uint32_t width;     /* dirty region width */
    uint32_t height;    /* dirty region height */
    uint32_t stride;    /* bytes per row */
    uint32_t bpp;       /* bits per pixel (32) */
    uint64_t offset;    /* offset into shared framebuffer */
    uint64_t sequence;  /* frame sequence number */
} db_frame_update_t;

/* ─── Display info (guest→host) ─── */
typedef struct {
    uint32_t width;
    uint32_t height;
    uint32_t bpp;
    uint32_t stride;
    uint32_t framebuffer_offset; /* offset in shared memory */
    uint32_t framebuffer_size;
    char     format[16];  /* "BGRA", "RGBA", etc. */
} db_display_info_t;

/* ─── Message ─── */
typedef struct {
    db_msg_type_t type;
    union {
        db_frame_update_t  frame;
        db_input_event_t   input;
        db_display_info_t  info;
        uint32_t           key;
        struct {
            int32_t x, y;
            uint32_t width, height;
        } cursor;
    };
} db_message_t;

/* ─── Message queue (lock-free SPSC) ─── */
typedef struct {
    db_message_t messages[DISPLAY_MSG_QUEUE_SIZE];
    volatile uint32_t head;
    volatile uint32_t tail;
} db_msg_queue_t;

/* ─── Shared framebuffer (guest writes, host reads) ─── */
typedef struct {
    volatile uint32_t width;
    volatile uint32_t height;
    volatile uint32_t bpp;
    volatile uint32_t stride;
    volatile uint64_t dirty_seq;   /* incremented on frame update */
    volatile uint64_t ack_seq;     /* host acks completed blit */
    uint8_t  data[0];             /* framebuffer pixel data */
} __attribute__((aligned(4096))) db_shared_fb_t;

/* ─── Display bridge instance ─── */
typedef struct {
    int          id;
    int          active;
    char         name[DISPLAY_BRIDGE_NAME_MAX];

    /* Guest → Host queue */
    db_msg_queue_t guest_to_host;

    /* Host → Guest queue */
    db_msg_queue_t host_to_guest;

    /* Shared framebuffer (mapped from guest) */
    db_shared_fb_t *shared_fb;
    uint64_t       fb_phys;       /* physical address of shared FB */
    uint32_t       fb_size;

    /* Current display state */
    uint32_t width;
    uint32_t height;
    uint32_t bpp;
    uint32_t stride;

    /* Blit tracking */
    uint64_t last_blitted_seq;
    int      needs_full_redraw;

    /* Associated VM/container */
    int      vm_id;
    int      container_id;

    /* Ownership tracking: 1 if we allocated fb, 0 if just mapped */
    int      fb_owned;
    uint32_t fb_pages; /* number of pages allocated for fb */
} display_bridge_t;

/* ─── Init ─── */
int display_bridge_init(void);

/* ─── Bridge lifecycle ─── */
int display_bridge_create(const char *name, int vm_id);
int display_bridge_destroy(int id);
int display_bridge_activate(int id);
int display_bridge_deactivate(int id);

/* ─── Shared framebuffer ─── */
int display_bridge_alloc_fb(int id, uint32_t width, uint32_t height,
                            uint64_t *out_phys, uint32_t *out_size);
int display_bridge_map_fb(int id, uint64_t phys, uint32_t size);
int display_bridge_unmap_fb(int id);

/* ─── Message passing ─── */
int display_bridge_push_guest_msg(int id, const db_message_t *msg);
int display_bridge_pop_host_msg(int id, db_message_t *msg);
int display_bridge_push_host_msg(int id, const db_message_t *msg);
int display_bridge_pop_guest_msg(int id, db_message_t *msg);

/* ─── Frame blitting (host side) ─── */
int display_bridge_blit_dirty(int id);
int display_bridge_blit_full(int id);
int display_bridge_blit_region(int id, uint32_t x, uint32_t y,
                               uint32_t w, uint32_t h);

/* ─── Input forwarding (host → guest) ─── */
int display_bridge_send_key(int id, uint32_t key, int down);
int display_bridge_send_mouse(int id, int32_t x, int32_t y,
                              uint32_t buttons);
int display_bridge_send_wheel(int id, int32_t dx, int32_t dy);

/* ─── Queries ─── */
display_bridge_t *display_bridge_get(int id);
int display_bridge_count(void);
int display_bridge_get_pending_frames(int id);

#endif

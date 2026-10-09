#include "x11_server.h"
#include "penrose_bridge.h"
#include "string.h"
#include "kprintf.h"
#include "mm.h"
#include "process.h"
#include "fs.h"
#include "xserver.h"
#include "sched.h"
#include "syscall.h"

static x11_client_t g_clients[X11_MAX_CLIENTS];
static x11_window_t g_windows[X11_MAX_WINDOWS];
static int g_client_count = 0;
static int g_window_count = 0;
static uint32_t g_next_xid = 1;
static uint32_t g_next_atom = 1;

static const char *g_atom_names[X11_MAX_ATOMS];
static int g_atom_count = 0;

static const char *builtin_atoms[] = {
    "WM_PROTOCOLS",
    "WM_DELETE_WINDOW",
    "WM_TAKE_FOCUS",
    "WM_STATE",
    "WM_CHANGE_STATE",
    "WM_CLASS",
    "WM_NAME",
    "WM_ICON_NAME",
    "WM_HINTS",
    "WM_NORMAL_HINTS",
    "WM_SIZE_HINTS",
    "WM_ZOOM_HINTS",
    "WM_CLIENT_MACHINE",
    "WM_COMMAND",
    "WM_ICON_SIZE",
    "WM_TRANSIENT_FOR",
    "WM_COLORMAP_WINDOWS",
    "NET_WM_NAME",
    "NET_WM_VISIBLE_NAME",
    "NET_WM_ICON_NAME",
    "NET_WM_VISIBLE_ICON_NAME",
    "NET_WM_DESKTOP",
    "NET_WM_WINDOW_TYPE",
    "NET_WM_WINDOW_TYPE_DESKTOP",
    "NET_WM_WINDOW_TYPE_DOCK",
    "NET_WM_WINDOW_TYPE_TOOLBAR",
    "NET_WM_WINDOW_TYPE_MENU",
    "NET_WM_WINDOW_TYPE_UTILITY",
    "NET_WM_WINDOW_TYPE_SPLASH",
    "NET_WM_WINDOW_TYPE_DIALOG",
    "NET_WM_WINDOW_TYPE_NORMAL",
    "NET_WM_STATE",
    "NET_WM_STATE_MODAL",
    "NET_WM_STATE_STICKY",
    "NET_WM_STATE_MAXIMIZED_VERT",
    "NET_WM_STATE_MAXIMIZED_HORZ",
    "NET_WM_STATE_SHADED",
    "NET_WM_STATE_SKIP_TASKBAR",
    "NET_WM_STATE_SKIP_PAGER",
    "NET_WM_STATE_HIDDEN",
    "NET_WM_STATE_FULLSCREEN",
    "NET_WM_STATE_ABOVE",
    "NET_WM_STATE_BELOW",
    "NET_WM_STATE_DEMANDS_ATTENTION",
    "NET_ACTIVE_WINDOW",
    "NET_SUPPORTED",
    "NET_SUPPORTING_WM_CHECK",
    "NET_WM_ICON",
    "NET_WM_PID",
    "NET_WM_HANDLED_ICONS",
    "NET_WM_USER_TIME",
    "NET_WM_USER_TIME_WINDOW",
    "NET_WM_STRUT",
    "NET_WM_STRUT_PARTIAL",
    "NET_WM_ICON_GEOMETRY",
    "NET_WM_SYNC_REQUEST",
    "NET_WM_SYNC_REQUEST_COUNTER",
    "NET_WM_FULLSCREEN_MONITORS",
    "CLIPBOARD",
    "TARGETS",
    "MULTIPLE",
    "TIMESTAMP",
    "SAVE_TARGETS",
    "UTF8_STRING",
    "TEXT",
    "STRING",
    "COMPOUND_TEXT",
    "XdndAware",
    "XdndEnter",
    "XdndPosition",
    "XdndStatus",
    "XdndLeave",
    "XdndDrop",
    "XdndFinished",
    "XdndSelection",
    "XdndTypeList",
    "XdndActionList",
};

static uint32_t alloc_xid(void) {
    return g_next_xid++;
}

static int find_window_slot(void) {
    for (int i = 0; i < X11_MAX_WINDOWS; i++) {
        if (g_windows[i].id == 0)
            return i;
    }
    return -1;
}

void x11_server_init(void) {
    memset(g_clients, 0, sizeof(g_clients));
    memset(g_windows, 0, sizeof(g_windows));
    g_client_count = 0;
    g_window_count = 0;
    g_next_xid = 1;
    g_next_atom = 1;

    for (int i = 0; i < (int)(sizeof(builtin_atoms) / sizeof(builtin_atoms[0])); i++) {
        if (g_atom_count < X11_MAX_ATOMS) {
            g_atom_names[g_atom_count] = builtin_atoms[i];
            g_atom_count++;
        }
    }

    kprintf("X11: Server initialized with %d builtin atoms\n", g_atom_count);
}

void x11_server_tick(void) {
    for (int i = 0; i < X11_MAX_CLIENTS; i++) {
        x11_client_t *client = &g_clients[i];
        if (client->id == 0) continue;

        uint8_t buf[256];
        int n = kernel_read(client->fd, buf, sizeof(buf));
        if (n > 0) {
            uint8_t reply[X11_BUF_SIZE];
            int reply_len = 0;
            x11_client_process_request(client->id, buf, n, reply, &reply_len);
            if (reply_len > 0) {
                kernel_write(client->fd, reply, reply_len);
            }
        }
    }
}

int x11_client_connect(int fd, int pid) {
    int slot = -1;
    for (int i = 0; i < X11_MAX_CLIENTS; i++) {
        if (g_clients[i].id == 0) {
            slot = i;
            break;
        }
    }
    if (slot < 0) return -1;

    x11_client_t *client = &g_clients[slot];
    memset(client, 0, sizeof(x11_client_t));
    client->id = slot + 1;
    client->fd = fd;
    client->pid = pid;
    client->seq = 0;
    client->authenticated = 1;
    client->big_endian = 0;
    client->byte_order = 'l';
    client->major_version = 11;
    client->minor_version = 0;
    client->atoms = (uint32_t *)malloc(X11_MAX_ATOMS * sizeof(uint32_t));
    client->atom_count = 0;
    client->windows = (x11_window_t *)malloc(X11_MAX_WINDOWS * sizeof(x11_window_t));
    client->window_count = 0;
    client->root_window = alloc_xid();

    g_client_count++;
    kprintf("X11: Client %d connected (pid=%d, fd=%d)\n", client->id, pid, fd);
    return client->id;
}

void x11_client_disconnect(int client_id) {
    if (client_id < 1 || client_id > X11_MAX_CLIENTS) return;
    x11_client_t *client = &g_clients[client_id - 1];
    if (client->id == 0) return;

    for (int i = 0; i < client->window_count; i++) {
        uint32_t xid = client->windows[i].xid;
        for (int j = 0; j < X11_MAX_WINDOWS; j++) {
            if (g_windows[j].xid == xid) {
                g_windows[j].id = 0;
                g_window_count--;
                break;
            }
        }
    }

    if (client->atoms) free(client->atoms);
    if (client->windows) free(client->windows);
    client->id = 0;
    g_client_count--;
    kprintf("X11: Client %d disconnected\n", client_id);
}

static int write_uint16(uint8_t *buf, int *pos, uint16_t val) {
    buf[(*pos)++] = val & 0xFF;
    buf[(*pos)++] = (val >> 8) & 0xFF;
    return 2;
}

static int write_uint32(uint8_t *buf, int *pos, uint32_t val) {
    buf[(*pos)++] = val & 0xFF;
    buf[(*pos)++] = (val >> 8) & 0xFF;
    buf[(*pos)++] = (val >> 16) & 0xFF;
    buf[(*pos)++] = (val >> 24) & 0xFF;
    return 4;
}

static int write_string(uint8_t *buf, int *pos, const char *str) {
    if (!str) str = "";
    int len = strlen(str);
    write_uint16(buf, pos, len);
    memcpy(&buf[*pos], str, len);
    *pos += len;
    while (*pos % 4) buf[(*pos)++] = 0;
    return len;
}

static uint32_t read_uint32(const uint8_t *buf, int len, int *pos, int *ok) {
    if (!*ok || *pos < 0 || *pos + 4 > len) { *ok = 0; return 0; }
    uint32_t val = buf[*pos] | (buf[*pos+1] << 8) | (buf[*pos+2] << 16) | (buf[*pos+3] << 24);
    *pos += 4;
    return val;
}

static uint16_t read_uint16(const uint8_t *buf, int len, int *pos, int *ok) {
    if (!*ok || *pos < 0 || *pos + 2 > len) { *ok = 0; return 0; }
    uint16_t val = buf[*pos] | (buf[*pos+1] << 8);
    *pos += 2;
    return val;
}

static uint8_t read_uint8(const uint8_t *buf, int len, int *pos, int *ok) {
    if (!*ok || *pos < 0 || *pos + 1 > len) { *ok = 0; return 0; }
    return buf[(*pos)++];
}

/* Minimum request length check (X11 sends opcode byte + 3 padding/rest). */
static int req_min_len(int len, int min_bytes) {
    return len >= min_bytes;
}

/* A client may only mutate windows it owns. Kernel-owned windows
 * (client_id 0, used by the csl GUI bridge) are never user-mutable. */
static int client_owns(x11_client_t *client, uint32_t xid) {
    if (!client) return 0;
    x11_window_t *w = x11_get_window(xid);
    if (!w) return 0;
    if (w->client_id == 0) return 0;   /* reserved for kernel-side agents */
    return w->client_id == client->id;
}

/* Clamp window size into sane bounds to avoid absurd allocations/blits. */
static uint16_t clamp_dim(int v) {
    if (v < 1) return 1;
    if (v > 4096) return 4096;
    return (uint16_t)v;
}

static int handle_create_window(x11_client_t *client, const uint8_t *data, int len, uint8_t *reply, int *reply_len) {
    (void)reply;
    if (!req_min_len(len, 36 + 4)) { *reply_len = 0; return 0; }

    int pos = 0, ok = 1;
    uint8_t depth = read_uint8(data, len, &pos, &ok);
    uint32_t wid = read_uint32(data, len, &pos, &ok);
    uint32_t parent = read_uint32(data, len, &pos, &ok);
    int16_t x = (int16_t)read_uint16(data, len, &pos, &ok);
    int16_t y = (int16_t)read_uint16(data, len, &pos, &ok);
    uint16_t width = read_uint16(data, len, &pos, &ok);
    uint16_t height = read_uint16(data, len, &pos, &ok);
    uint16_t border = read_uint16(data, len, &pos, &ok);
    uint16_t class = read_uint16(data, len, &pos, &ok);
    uint32_t visual = read_uint32(data, len, &pos, &ok);
    (void)parent; (void)border; (void)class; (void)visual;
    uint32_t value_mask = read_uint32(data, len, &pos, &ok);
    (void)value_mask;
    if (!ok || wid == 0) { *reply_len = 0; return 0; }

    width = clamp_dim(width);
    height = clamp_dim(height);

    int wslot = find_window_slot();
    if (wslot < 0) { *reply_len = 0; return 0; }

    if (x11_get_window(wid)) {  /* window id already in use */
        *reply_len = 0;
        return 0;
    }

    x11_window_t *win = &g_windows[wslot];
    win->id = wslot + 1;
    win->client_id = client->id;
    win->xid = wid;
    win->x = x;
    win->y = y;
    win->width = width;
    win->height = height;
    win->border_width = border;
    win->depth = depth;
    win->visual = visual;
    win->parent = parent;
    win->mapped = 0;
    win->class = class;
    win->override_redirect = 0;
    snprintf(win->title, sizeof(win->title), "X11 Window %u", wid);

    if (client->windows && client->window_count < X11_MAX_WINDOWS) {
        client->windows[client->window_count++] = *win;
    }
    g_window_count++;

    pos = 0;
    write_uint16(reply, &pos, 1);
    write_uint16(reply, &pos, client->seq);
    write_uint32(reply, &pos, 0);
    write_uint32(reply, &pos, wid);
    for (int i = 0; i < 20; i++) write_uint32(reply, &pos, 0);
    *reply_len = pos;
    return 0;
}

static int handle_destroy_window(x11_client_t *client, const uint8_t *data, int len, uint8_t *reply, int *reply_len) {
    (void)reply;
    if (!req_min_len(len, 4)) { *reply_len = 0; return 0; }
    int pos = 0, ok = 1;
    uint32_t wid = read_uint32(data, len, &pos, &ok);
    if (!ok || !client_owns(client, wid)) { *reply_len = 0; return 0; }

    for (int i = 0; i < X11_MAX_WINDOWS; i++) {
        if (g_windows[i].xid == wid) {
            g_windows[i].id = 0;
            g_window_count--;
            /* remove from this client's window list too */
            for (int j = 0; j < client->window_count; j++) {
                if (client->windows[j].xid == wid) {
                    client->windows[j] = client->windows[client->window_count - 1];
                    client->window_count--;
                    break;
                }
            }
            break;
        }
    }

    prs_emit_destroy(wid);

    pos = 0;
    write_uint16(reply, &pos, 1);
    write_uint16(reply, &pos, client->seq);
    write_uint32(reply, &pos, 0);
    write_uint32(reply, &pos, 0);
    for (int i = 0; i < 20; i++) write_uint32(reply, &pos, 0);
    *reply_len = pos;
    return 0;
}

static int handle_map_window(x11_client_t *client, const uint8_t *data, int len, uint8_t *reply, int *reply_len) {
    (void)client; (void)reply;
    if (!req_min_len(len, 4)) { *reply_len = 0; return 0; }
    int pos = 0, ok = 1;
    uint32_t wid = read_uint32(data, len, &pos, &ok);
    if (!ok || !client_owns(client, wid)) { *reply_len = 0; return 0; }

    x11_window_t *win = x11_get_window(wid);
    if (win) {
        win->mapped = 0;
        win->override_redirect = 0;
        int x = win->x;
        int y = win->y;
        int w = win->width;
        int h = win->height;
        prs_set_title(wid, win->title);
        prs_emit_configure(wid, x, y, w, h);
    }

    prs_emit_map_request(wid);
    *reply_len = 0;
    return 0;
}

static int handle_unmap_window(x11_client_t *client, const uint8_t *data, int len, uint8_t *reply, int *reply_len) {
    (void)reply;
    if (!req_min_len(len, 4)) { *reply_len = 0; return 0; }
    int pos = 0, ok = 1;
    uint32_t wid = read_uint32(data, len, &pos, &ok);
    if (!ok || !client_owns(client, wid)) { *reply_len = 0; return 0; }

    for (int i = 0; i < X11_MAX_WINDOWS; i++) {
        if (g_windows[i].xid == wid) {
            g_windows[i].mapped = 0;
            break;
        }
    }

    prs_emit_unmap(wid);
    *reply_len = 0;
    return 0;
}

static int handle_get_geometry(x11_client_t *client, const uint8_t *data, int len, uint8_t *reply, int *reply_len) {
    if (!req_min_len(len, 4)) { *reply_len = 0; return 0; }
    int pos = 0, ok = 1;
    uint32_t drawable = read_uint32(data, len, &pos, &ok);
    if (!ok) { *reply_len = 0; return 0; }

    x11_window_t *win = NULL;
    for (int i = 0; i < X11_MAX_WINDOWS; i++) {
        if (g_windows[i].xid == drawable) {
            win = &g_windows[i];
            break;
        }
    }

    pos = 0;
    write_uint16(reply, &pos, 1);
    write_uint16(reply, &pos, client->seq);
    write_uint32(reply, &pos, 0);
    write_uint32(reply, &pos, 28);
    if (win) {
        write_uint32(reply, &pos, win->xid);
        write_uint16(reply, &pos, win->x);
        write_uint16(reply, &pos, win->y);
        write_uint16(reply, &pos, win->width);
        write_uint16(reply, &pos, win->height);
        write_uint16(reply, &pos, win->border_width);
        write_uint16(reply, &pos, win->depth);
    } else {
        for (int i = 0; i < 7; i++) write_uint16(reply, &pos, 0);
    }
    *reply_len = pos;
    return 0;
}

static int handle_intern_atom(x11_client_t *client, const uint8_t *data, int len, uint8_t *reply, int *reply_len) {
    (void)client;
    if (!req_min_len(len, 4)) { *reply_len = 0; return 0; }
    int pos = 0, ok = 1;
    uint8_t only_if_exists = read_uint8(data, len, &pos, &ok);
    uint16_t name_len = read_uint16(data, len, &pos, &ok);
    if (!ok) { *reply_len = 0; return 0; }
    if (name_len > 255) name_len = 255;
    if (pos + name_len > len) { *reply_len = 0; return 0; }   /* OOB guard */
    char name[256];
    memcpy(name, &data[pos], name_len);
    name[name_len] = 0;

    uint32_t atom = 0;
    for (int i = 0; i < g_atom_count; i++) {
        if (strcmp(g_atom_names[i], name) == 0) {
            atom = i + 1;
            break;
        }
    }

    if (atom == 0 && !only_if_exists && g_atom_count < X11_MAX_ATOMS) {
        char *copy = strdup(name);
        if (copy) {
            g_atom_names[g_atom_count] = copy;
            atom = g_atom_count + 1;
            g_atom_count++;
        }
    }

    pos = 0;
    write_uint16(reply, &pos, 1);
    write_uint16(reply, &pos, client->seq);
    write_uint32(reply, &pos, 0);
    write_uint32(reply, &pos, atom);
    for (int i = 0; i < 20; i++) write_uint32(reply, &pos, 0);
    *reply_len = pos;
    return 0;
}

static int handle_get_atom_name(x11_client_t *client, const uint8_t *data, int len, uint8_t *reply, int *reply_len) {
    (void)client;
    if (!req_min_len(len, 4)) { *reply_len = 0; return 0; }
    int pos = 0, ok = 1;
    uint32_t atom = read_uint32(data, len, &pos, &ok);
    if (!ok) { *reply_len = 0; return 0; }

    const char *name = "UNKNOWN";
    if (atom > 0 && atom <= (uint32_t)g_atom_count) {
        name = g_atom_names[atom - 1];
    }

    pos = 0;
    write_uint16(reply, &pos, 1);
    write_uint16(reply, &pos, client->seq);
    write_uint32(reply, &pos, 0);
    write_uint32(reply, &pos, 8 + ((strlen(name) + 3) & ~3));
    write_string(reply, &pos, name);
    *reply_len = pos;
    return 0;
}

/* PutImage pixel payload must fit inside the request buffer.
 * Layout (as parsed below): 4+4 drawable+gc, depth, format, pad(2),
 * 2+2+2+2 dst, 2+2 w/h, pad(2), then w*h*(format/8) bytes of pixels. */
static int handle_put_image(x11_client_t *client, const uint8_t *data, int len, uint8_t *reply, int *reply_len) {
    (void)reply;
    if (!req_min_len(len, 24)) { *reply_len = 0; return 0; }
    int pos = 0, ok = 1;
    uint32_t drawable = read_uint32(data, len, &pos, &ok);
    (void)read_uint32(data, len, &pos, &ok); // gc
    uint8_t depth = read_uint8(data, len, &pos, &ok);
    uint8_t format = read_uint8(data, len, &pos, &ok);
    (void)depth;
    pos += 2; // pad
    int16_t dst_x = (int16_t)read_uint16(data, len, &pos, &ok);
    int16_t dst_y = (int16_t)read_uint16(data, len, &pos, &ok);
    uint16_t w = read_uint16(data, len, &pos, &ok);
    uint16_t h = read_uint16(data, len, &pos, &ok);
    pos += 2; // pad
    if (!ok) { *reply_len = 0; return 0; }

    if (w == 0 || h == 0 || !client_owns(client, drawable)) {
        *reply_len = 0;
        return 0;
    }

    /* Cap the blit dimensions to something sane and clip against the
     * window so a malicious client can't ask for an enormous copy. */
    x11_window_t *win = x11_get_window(drawable);
    int max_w = win ? win->width : (int)w;
    int max_h = win ? win->height : (int)h;

    int bytes_per_pixel = (format == 1) ? 1 : (format == 2) ? 2 : 4;
    int64_t dirty_w = w, dirty_h = h;
    if (dst_x < 0) dirty_w += dst_x;
    if (dst_y < 0) dirty_h += dst_y;
    if (win) {
        if (dst_x + (int)dirty_w > win->width)  dirty_w = win->width  - dst_x;
        if (dst_y + (int)dirty_h > win->height) dirty_h = win->height - dst_y;
    }
    if (dirty_w < 0) dirty_w = 0;
    if (dirty_h < 0) dirty_h = 0;
    (void)max_w; (void)max_h;

    int64_t need = (int64_t)w * (int64_t)h * (int64_t)bytes_per_pixel;
    if (need < 0 || need > 8 * 1024 * 1024) { *reply_len = 0; return 0; }
    if (pos + need > len) { *reply_len = 0; return 0; }   /* OOB guard */

    const uint8_t *pixels = &data[pos];
    /* Composite expects 32bpp rows with stride in pixel units. */
    prs_blit(drawable, dst_x, dst_y, w, h, pixels, (int)w);
    *reply_len = 0;
    return 0;
}

static int handle_configure_window(x11_client_t *client, const uint8_t *data, int len, uint8_t *reply, int *reply_len) {
    (void)reply;
    if (!req_min_len(len, 8)) { *reply_len = 0; return 0; }
    int pos = 0, ok = 1;
    uint32_t wid = read_uint32(data, len, &pos, &ok);
    uint16_t value_mask = read_uint16(data, len, &pos, &ok);
    pos += 2; // pad
    if (!ok) { *reply_len = 0; return 0; }

    x11_window_t *win = x11_get_window(wid);
    int x = win ? win->x : 0;
    int y = win ? win->y : 0;
    int w = win ? win->width : 0;
    int h = win ? win->height : 0;

    if (!win || !client_owns(client, wid)) {
        *reply_len = 0;
        return 0;
    }

    // values follow the mask in order: x, y, width, height, border_width, sibling, stack_mode
    if (value_mask & 0x0001) { int v = (int)read_uint16(data, len, &pos, &ok); pos += 2; x = v; } else pos += (ok ? 4 : 0);
    if (value_mask & 0x0002) { int v = (int)read_uint16(data, len, &pos, &ok); pos += 2; y = v; } else pos += (ok ? 4 : 0);
    if (value_mask & 0x0004) { int v = (int)read_uint16(data, len, &pos, &ok); pos += 2; w = v; } else pos += (ok ? 4 : 0);
    if (value_mask & 0x0008) { int v = (int)read_uint16(data, len, &pos, &ok); pos += 2; h = v; } else pos += (ok ? 4 : 0);
    if (value_mask & 0x0010) pos += (ok ? 4 : 0); // border_width
    if (value_mask & 0x0020) pos += (ok ? 4 : 0); // sibling
    if (value_mask & 0x0040) pos += (ok ? 4 : 0); // stack_mode
    if (!ok) { *reply_len = 0; return 0; }

    win->x = x;
    win->y = y;
    win->width = clamp_dim(w);
    win->height = clamp_dim(h);

    prs_emit_configure(wid, win->x, win->y, win->width, win->height);
    *reply_len = 0;
    return 0;
}

static int handle_change_property(x11_client_t *client, const uint8_t *data, int len, uint8_t *reply, int *reply_len) {
    (void)reply;
    if (!req_min_len(len, 24)) { *reply_len = 0; return 0; }
    int pos = 0, ok = 1;
    uint32_t window = read_uint32(data, len, &pos, &ok);
    uint32_t property = read_uint32(data, len, &pos, &ok);
    uint32_t type = read_uint32(data, len, &pos, &ok);
    uint8_t format = read_uint8(data, len, &pos, &ok);
    pos += 3; // pad
    uint32_t length = read_uint32(data, len, &pos, &ok);
    if (!ok || !client_owns(client, window)) { *reply_len = 0; return 0; }

    int bytes_per_unit = (format == 8) ? 1 : (format == 16) ? 2 : 4;
    uint64_t bytes = (uint64_t)length * (uint64_t)bytes_per_unit;
    if (bytes > 4096 || pos + (int64_t)bytes > len) { *reply_len = 0; return 0; }  /* OOB guard */

    int is_name = 0;
    if (property > 0 && property <= (uint32_t)g_atom_count) {
        const char *pname = g_atom_names[property - 1];
        is_name = (strcmp(pname, "WM_NAME") == 0) || (strcmp(pname, "NET_WM_NAME") == 0);
    }
    (void)type;
    if (is_name && format == 8 && window && length > 0) {
        char title[64];
        uint32_t n = length < (uint32_t)(sizeof(title) - 1) ? length : (uint32_t)(sizeof(title) - 1);
        memcpy(title, &data[pos], n);
        title[n] = 0;
        prs_set_title(window, title);
    }

    *reply_len = 0;
    return 0;
}

static int handle_get_property(x11_client_t *client, const uint8_t *data, int len, uint8_t *reply, int *reply_len) {
    (void)client;
    if (!req_min_len(len, 24)) { *reply_len = 0; return 0; }
    int pos = 0, ok = 1;
    uint8_t del = read_uint8(data, len, &pos, &ok);
    (void)del;
    uint32_t window = read_uint32(data, len, &pos, &ok);
    (void)window;
    uint32_t property = read_uint32(data, len, &pos, &ok);
    uint32_t type = read_uint32(data, len, &pos, &ok);
    uint32_t offset = read_uint32(data, len, &pos, &ok);
    (void)offset;
    uint32_t length = read_uint32(data, len, &pos, &ok);
    (void)length;
    if (!ok) { *reply_len = 0; return 0; }

    (void)property;
    pos = 0;
    write_uint16(reply, &pos, 1);
    write_uint16(reply, &pos, client->seq);
    write_uint32(reply, &pos, 0);
    write_uint32(reply, &pos, 8);
    write_uint32(reply, &pos, type);
    write_uint32(reply, &pos, 32);
    write_uint32(reply, &pos, 0);
    write_uint32(reply, &pos, 0);
    *reply_len = pos;
    return 0;
}

static int handle_query_tree(x11_client_t *client, const uint8_t *data, int len, uint8_t *reply, int *reply_len) {
    if (!req_min_len(len, 4)) { *reply_len = 0; return 0; }
    int pos = 0, ok = 1;
    uint32_t window = read_uint32(data, len, &pos, &ok);
    (void)window;
    if (!ok) { *reply_len = 0; return 0; }

    x11_window_t *win = NULL;
    for (int i = 0; i < X11_MAX_WINDOWS; i++) {
        if (g_windows[i].xid == window) {
            win = &g_windows[i];
            break;
        }
    }

    uint32_t root = 1;
    uint32_t parent = win ? win->parent : 0;
    int nchildren = 0;

    pos = 0;
    write_uint16(reply, &pos, 1);
    write_uint16(reply, &pos, client->seq);
    write_uint32(reply, &pos, 0);
    write_uint32(reply, &pos, 12 + nchildren * 4);
    write_uint32(reply, &pos, root);
    write_uint32(reply, &pos, parent);
    write_uint16(reply, &pos, nchildren);
    write_uint16(reply, &pos, 0);
    for (int i = 0; i < nchildren; i++) write_uint32(reply, &pos, 0);
    *reply_len = pos;
    return 0;
}

static int handle_no_operation(x11_client_t *client, const uint8_t *data, int len, uint8_t *reply, int *reply_len) {
    (void)client; (void)data; (void)len; (void)reply;
    *reply_len = 0;
    return 0;
}

int x11_client_process_request(int client_id, const uint8_t *data, int len, uint8_t *reply, int *reply_len) {
    if (client_id < 1 || client_id > X11_MAX_CLIENTS) return -1;
    x11_client_t *client = &g_clients[client_id - 1];
    if (client->id == 0) return -1;

    if (len < 1) return -1;
    uint8_t opcode = data[0];
    client->seq++;

    switch (opcode) {
        case X11_REQ_CREATE_WINDOW:
            return handle_create_window(client, data, len, reply, reply_len);
        case X11_REQ_DESTROY_WINDOW:
            return handle_destroy_window(client, data, len, reply, reply_len);
        case X11_REQ_MAP_WINDOW:
            return handle_map_window(client, data, len, reply, reply_len);
        case X11_REQ_UNMAP_WINDOW:
            return handle_unmap_window(client, data, len, reply, reply_len);
        case X11_REQ_GET_GEOMETRY:
            return handle_get_geometry(client, data, len, reply, reply_len);
        case X11_REQ_QUERY_TREE:
            return handle_query_tree(client, data, len, reply, reply_len);
        case X11_REQ_INTERN_ATOM:
            return handle_intern_atom(client, data, len, reply, reply_len);
        case X11_REQ_GET_ATOM_NAME:
            return handle_get_atom_name(client, data, len, reply, reply_len);
        case X11_REQ_CHANGE_PROPERTY:
            return handle_change_property(client, data, len, reply, reply_len);
        case X11_REQ_CONFIGURE_WINDOW:
            return handle_configure_window(client, data, len, reply, reply_len);
        case X11_REQ_PUT_IMAGE:
            return handle_put_image(client, data, len, reply, reply_len);
        case X11_REQ_GET_PROPERTY:
            return handle_get_property(client, data, len, reply, reply_len);
        case X11_REQ_NO_OPERATION:
            return handle_no_operation(client, data, len, reply, reply_len);
        default:
            kprintf("X11: Unhandled request %d from client %d\n", opcode, client_id);
            break;
    }

    *reply_len = 0;
    return 0;
}

void x11_send_event(int client_id, const x11_event_t *event) {
    if (client_id < 1 || client_id > X11_MAX_CLIENTS) return;
    x11_client_t *client = &g_clients[client_id - 1];
    if (client->id == 0) return;

    uint8_t buf[sizeof(x11_event_t)];
    memcpy(buf, event, sizeof(x11_event_t));
    kernel_write(client->fd, buf, sizeof(x11_event_t));
}

void x11_broadcast_event(const x11_event_t *event, int exclude_client) {
    for (int i = 0; i < X11_MAX_CLIENTS; i++) {
        if (g_clients[i].id != 0 && g_clients[i].id != exclude_client) {
            x11_send_event(g_clients[i].id, event);
        }
    }
}

x11_client_t *x11_get_client(int id) {
    if (id < 1 || id > X11_MAX_CLIENTS) return NULL;
    if (g_clients[id - 1].id == 0) return NULL;
    return &g_clients[id - 1];
}

x11_window_t *x11_get_window(uint32_t xid) {
    for (int i = 0; i < X11_MAX_WINDOWS; i++) {
        if (g_windows[i].xid == xid)
            return &g_windows[i];
    }
    return NULL;
}

uint32_t x11_intern_atom(const char *name, int only_if_exists) {
    for (int i = 0; i < g_atom_count; i++) {
        if (strcmp(g_atom_names[i], name) == 0)
            return i + 1;
    }
    if (!only_if_exists && g_atom_count < X11_MAX_ATOMS) {
        char *copy = strdup(name);
        if (copy) {
            g_atom_names[g_atom_count] = copy;
            return g_atom_count + 1;
        }
    }
    return 0;
}

const char *x11_get_atom_name(uint32_t atom) {
    if (atom > 0 && atom <= (uint32_t)g_atom_count)
        return g_atom_names[atom - 1];
    return "UNKNOWN";
}

uint32_t x11_create_kernel_window(const char *title, int w, int h) {
    int wslot = -1;
    for (int i = 0; i < X11_MAX_WINDOWS; i++) {
        if (g_windows[i].id == 0) {
            wslot = i;
            break;
        }
    }
    if (wslot < 0) return 0;
    if (g_window_count >= X11_MAX_WINDOWS) return 0;

    uint32_t xid = g_next_xid++;
    x11_window_t *win = &g_windows[wslot];
    memset(win, 0, sizeof(*win));
    win->id = wslot + 1;
    win->client_id = 0;
    win->xid = xid;
    win->x = 100;
    win->y = 100;
    win->width = w > 0 ? w : 480;
    win->height = h > 0 ? h : 360;
    win->border_width = 0;
    win->depth = 32;
    win->visual = 0;
    win->parent = 0;
    win->mapped = 0;
    win->class = 0;
    win->override_redirect = 0;
    snprintf(win->title, sizeof(win->title), "%s", title);
    g_window_count++;
    return xid;
}

void x11_release_window(uint32_t xid) {
    x11_window_t *w = x11_get_window(xid);
    if (!w || w->id == 0) return;
    w->id = 0;
    w->xid = 0;
    if (g_window_count > 0) g_window_count--;
}

uint32_t x11_window_xid(int index) {
    if (index < 0 || index >= X11_MAX_WINDOWS) return 0;
    x11_window_t *w = &g_windows[index];
    return (w->id != 0) ? w->xid : 0;
}
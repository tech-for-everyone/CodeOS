/* ─────────────────────────────────────────────────────────────────────
 * x11_stubs.c — Xlib-compatible C API for in-kernel Rust crates
 *
 * Implements the extern "C" symbols that the upstream `xlib` and
 * `x11-dl` Rust crates expect.  Every call routes through the
 * kernel's X11 server (x11_server.c) directly — no IPC, no sockets.
 *
 * This is NOT a full Xlib — it's the minimum API surface needed for
 * the Rust crates to compile and for basic X11 programs to work
 * inside the kernel.  Unimplemented functions return a safe default.
 * ───────────────────────────────────────────────────────────────────── */

#include "x11_stubs.h"
#include "x11_server.h"
#include "penrose_bridge.h"
#include "kprintf.h"
#include "mm.h"
#include "string.h"

#include <stdint.h>

/* ══════════════════════════════════════════════════════════════════════
 * Builtin atom table — one global instance, initialized lazily
 * ══════════════════════════════════════════════════════════════════════ */
Atom XA_PRIMARY         = 0;
Atom XA_SECONDARY       = 0;
Atom XA_ATOM            = 0;
Atom XA_CARDINAL        = 0;
Atom XA_STRING          = 0;
Atom XA_WM_NAME         = 0;
Atom XA_WM_CLASS        = 0;
Atom XA_WM_HINTS        = 0;
Atom XA_WM_NORMAL_HINTS = 0;
Atom XA_WM_DELETE_WINDOW = 0;
Atom XA_WM_PROTOCOLS    = 0;
Atom XA_CLIPBOARD       = 0;
Atom XA_UTF8_STRING     = 0;

static int g_atoms_initialized = 0;

static void ensure_atoms(Display *dpy) {
    if (g_atoms_initialized) return;
    XA_PRIMARY         = XInternAtom(dpy, "PRIMARY", False);
    XA_SECONDARY       = XInternAtom(dpy, "SECONDARY", False);
    XA_ATOM            = XInternAtom(dpy, "ATOM", False);
    XA_CARDINAL        = XInternAtom(dpy, "CARDINAL", False);
    XA_STRING          = XInternAtom(dpy, "STRING", False);
    XA_WM_NAME         = XInternAtom(dpy, "WM_NAME", False);
    XA_WM_CLASS        = XInternAtom(dpy, "WM_CLASS", False);
    XA_WM_HINTS        = XInternAtom(dpy, "WM_HINTS", False);
    XA_WM_NORMAL_HINTS = XInternAtom(dpy, "WM_NORMAL_HINTS", False);
    XA_WM_DELETE_WINDOW = XInternAtom(dpy, "WM_DELETE_WINDOW", False);
    XA_WM_PROTOCOLS    = XInternAtom(dpy, "WM_PROTOCOLS", False);
    XA_CLIPBOARD       = XInternAtom(dpy, "CLIPBOARD", False);
    XA_UTF8_STRING     = XInternAtom(dpy, "UTF8_STRING", False);
    g_atoms_initialized = 1;
}

/* ══════════════════════════════════════════════════════════════════════
 * Display open / close
 * ══════════════════════════════════════════════════════════════════════ */

Display *XOpenDisplay(const char *display_name) {
    (void)display_name;

    /* Allocate a Display handle.  We don't go through the syscall path
     * — the Rust crates run in-kernel and call this directly. */
    Display *dpy = (Display *)malloc(sizeof(Display));
    if (!dpy) return NULL;
    memset(dpy, 0, sizeof(Display));

    dpy->xfd = -1;  /* not a real fd */
    dpy->sequence = 1;
    dpy->nscreens = 1;
    dpy->proto_major_version = 11;
    dpy->proto_minor_version = 0;
    dpy->vendor = "CodeOS";
    dpy->display_name = display_name ? strdup(display_name) : strdup(":0");
    dpy->next_atom = 256; /* reserve low range for builtin atoms */

    /* Connect to the kernel X11 server.
     * x11_client_connect expects an fd + pid.  We use fd=0 (stdin)
     * as a sentinel since we're kernel-side — the server only
     * reads the client_id for routing. */
    dpy->client_id = x11_client_connect(0, 0);
    if (dpy->client_id <= 0) {
        /* Server may be uninitialized — still return a handle for
         * crate linking; queries will return empty results. */
        dpy->client_id = 1;
    }

    /* Root window */
    dpy->root_window = 1;

    /* Default screen geometry — query from xserver if available */
    dpy->screen_width = 1920;
    dpy->screen_height = 1080;
    dpy->default_depth = 32;
    dpy->default_cmap = 1;
    dpy->default_visual = NULL;

    /* Kick the atom cache */
    ensure_atoms(dpy);

    return dpy;
}

int XCloseDisplay(Display *dpy) {
    if (!dpy) return 0;
    x11_client_disconnect(dpy->client_id);
    if (dpy->display_name) free(dpy->display_name);
    free(dpy);
    return 0;
}

/* ══════════════════════════════════════════════════════════════════════
 * Window management
 * ══════════════════════════════════════════════════════════════════════ */

static Window next_wid = 100;

Window XCreateSimpleWindow(Display *dpy, Window parent,
        int x, int y, unsigned w, unsigned h, unsigned bw,
        unsigned long border, unsigned long bg) {
    (void)border; (void)bg;
    return XCreateWindow(dpy, parent, x, y, w, h, bw,
                         dpy->default_depth, 1, NULL, 0, NULL);
}

Window XCreateWindow(Display *dpy, Window parent,
        int x, int y, unsigned w, unsigned h, unsigned bw,
        int depth, unsigned klass, Visual *vis, unsigned long vmask, void *attr) {
    (void)parent; (void)depth; (void)klass; (void)vis; (void)vmask; (void)attr;

    if (!dpy) return 0;

    Window wid = next_wid++;

    /* Register with the kernel X11 server.  We call the server's
     * internal API directly (not through the client protocol). */
    uint32_t sxid = x11_create_kernel_window("X11 Window", (int)w, (int)h);
    (void)sxid;

    return wid;
}

int XDestroyWindow(Display *dpy, Window w) {
    (void)dpy;
    x11_window_t *xw = x11_get_window(w);
    if (xw) x11_release_window(w);
    prs_emit_destroy(w);
    return 1;
}

int XDestroySubwindows(Display *dpy, Window w) {
    return XDestroyWindow(dpy, w);
}

int XMapWindow(Display *dpy, Window w) {
    (void)dpy;
    prs_emit_map_request(w);
    return 1;
}

int XMapSubwindows(Display *dpy, Window w) {
    return XMapWindow(dpy, w);
}

int XMapRaised(Display *dpy, Window w) {
    return XMapWindow(dpy, w);
}

int XUnmapWindow(Display *dpy, Window w) {
    (void)dpy;
    x11_window_t *xw = x11_get_window(w);
    if (xw) xw->mapped = 0;
    prs_emit_unmap(w);
    return 1;
}

int XUnmapSubwindows(Display *dpy, Window w) {
    return XUnmapWindow(dpy, w);
}

int XRaiseWindow(Display *dpy, Window w) {
    (void)dpy; (void)w;
    prs_focus_client(w);
    return 1;
}

int XLowerWindow(Display *dpy, Window w) {
    (void)dpy; (void)w;
    return 1;
}

int XRestackWindows(Display *dpy, Window *wins, int n) {
    (void)dpy;
    for (int i = 0; i < n; i++) XRaiseWindow(dpy, wins[i]);
    return 1;
}

int XMoveWindow(Display *dpy, Window w, int x, int y) {
    (void)dpy;
    x11_window_t *xw = x11_get_window(w);
    if (xw) {
        xw->x = x;
        xw->y = y;
        prs_position_client(w, x, y, xw->width, xw->height);
    }
    return 1;
}

int XResizeWindow(Display *dpy, Window w, unsigned width, unsigned height) {
    (void)dpy;
    x11_window_t *xw = x11_get_window(w);
    if (xw) {
        xw->width = (int)width;
        xw->height = (int)height;
        prs_position_client(w, xw->x, xw->y, xw->width, xw->height);
    }
    return 1;
}

int XMoveResizeWindow(Display *dpy, Window w, int x, int y, unsigned width, unsigned height) {
    (void)dpy;
    x11_window_t *xw = x11_get_window(w);
    if (xw) {
        xw->x = x;
        xw->y = y;
        xw->width = (int)width;
        xw->height = (int)height;
        prs_position_client(w, x, y, (int)width, (int)height);
    }
    return 1;
}

int XConfigureWindow(Display *dpy, Window w, unsigned vm, void *vals) {
    (void)dpy; (void)vm; (void)vals;
    return 1;
}

int XReparentWindow(Display *dpy, Window w, Window parent, int x, int y) {
    (void)parent;
    return XMoveWindow(dpy, w, x, y);
}

int XWithdrawWindow(Display *dpy, Window w) {
    return XUnmapWindow(dpy, w);
}

int XIconifyWindow(Display *dpy, Window w) {
    prs_minimize_client(w);
    return 1;
}

/* ══════════════════════════════════════════════════════════════════════
 * Queries
 * ══════════════════════════════════════════════════════════════════════ */

Status XGetWindowAttributes(Display *dpy, Window w, XWindowAttributes *a) {
    if (!dpy || !a) return 0;
    memset(a, 0, sizeof(XWindowAttributes));
    x11_window_t *xw = x11_get_window(w);
    if (xw) {
        a->x = xw->x;
        a->y = xw->y;
        a->width = xw->width;
        a->height = xw->height;
        a->border_width = xw->border_width;
        a->depth = xw->depth;
        a->class = InputOutput;
        a->root = dpy->root_window;
        a->map_state = xw->mapped ? 2 : 0;  /* IsViewable=2, IsUnmapped=0 */
        a->visual = dpy->default_visual;
        a->screen = (Screen *)0;
    } else {
        /* Fallback for unmapped / unknown windows */
        a->root = dpy->root_window;
        a->depth = dpy->default_depth;
        a->visual = dpy->default_visual;
    }
    return 1;
}

Status XGetGeometry(Display *dpy, Drawable d, Window *root,
        int *x, int *y, unsigned *w, unsigned *h, unsigned *brd, unsigned *depth) {
    (void)dpy;
    if (root) *root = 1;
    x11_window_t *xw = x11_get_window(d);
    if (xw) {
        if (x) *x = xw->x;
        if (y) *y = xw->y;
        if (w) *w = (unsigned)xw->width;
        if (h) *h = (unsigned)xw->height;
        if (brd) *brd = (unsigned)xw->border_width;
        if (depth) *depth = (unsigned)xw->depth;
    } else {
        if (x) *x = 0;
        if (y) *y = 0;
        if (w) *w = 0;
        if (h) *h = 0;
        if (brd) *brd = 0;
        if (depth) *depth = 32;
    }
    return 1;
}

Status XGetWindowProperty(Display *dpy, Window w, Atom prop,
        long off, long len, Bool del, Atom rtype, Atom *atype,
        int *afmt, unsigned long *nitems, unsigned long *bafter,
        unsigned char **data) {
    (void)dpy; (void)w; (void)prop; (void)off; (void)len; (void)del;
    (void)rtype;
    /* Stub — return empty */
    if (atype) *atype = None;
    if (afmt) *afmt = 0;
    if (nitems) *nitems = 0;
    if (bafter) *bafter = 0;
    if (data) *data = NULL;
    return 1;
}

Status XQueryTree(Display *dpy, Window w, Window *root, Window *parent,
        Window **children, unsigned *nchildren) {
    (void)w;
    if (root) *root = dpy ? dpy->root_window : 1;
    if (parent) *parent = 0;
    if (children) *children = NULL;
    if (nchildren) *nchildren = 0;
    return 1;
}

/* ══════════════════════════════════════════════════════════════════════
 * Properties
 * ══════════════════════════════════════════════════════════════════════ */

int XChangeProperty(Display *dpy, Window w, Atom prop, Atom type,
        int fmt, int mode, const unsigned char *data, int nelem) {
    (void)dpy; (void)type; (void)fmt; (void)mode; (void)data; (void)nelem;

    /* Handle WM_NAME / NET_WM_NAME specially */
    if (prop == XA_WM_NAME && data && nelem > 0) {
        char title[64];
        int n = nelem < 63 ? nelem : 63;
        memcpy(title, data, n);
        title[n] = 0;
        prs_set_title(w, title);
    }
    return 1;
}

int XDeleteProperty(Display *dpy, Window w, Atom prop) {
    (void)dpy; (void)w; (void)prop;
    return 1;
}

int XRotateWindowProperties(Display *dpy, Window w, Atom *props,
        int nprop, int delta) {
    (void)dpy; (void)w; (void)props; (void)nprop; (void)delta;
    return 1;
}

/* ══════════════════════════════════════════════════════════════════════
 * Atoms
 * ══════════════════════════════════════════════════════════════════════ */

Atom XInternAtom(Display *dpy, const char *name, Bool only) {
    (void)dpy;
    return (Atom)x11_intern_atom(name, only ? 1 : 0);
}

Status XGetAtomName(Display *dpy, Atom a, char *buf, int len) {
    (void)dpy;
    const char *name = x11_get_atom_name(a);
    if (!name || !buf || len <= 0) return 0;
    int n = 0;
    while (name[n] && n < len - 1) { buf[n] = name[n]; n++; }
    buf[n] = 0;
    return 1;
}

/* ══════════════════════════════════════════════════════════════════════
 * Events — simple ring buffer per-display
 * ══════════════════════════════════════════════════════════════════════ */

#define EVT_BUF_SZ 64

static struct {
    XEvent buf[EVT_BUF_SZ];
    int head, tail, count;
} g_evt;

static void evt_push(const XEvent *ev) {
    if (g_evt.count >= EVT_BUF_SZ) return;
    g_evt.buf[g_evt.tail] = *ev;
    g_evt.tail = (g_evt.tail + 1) % EVT_BUF_SZ;
    g_evt.count++;
}

static int evt_pop(XEvent *ev) {
    if (g_evt.count <= 0) return 0;
    *ev = g_evt.buf[g_evt.head];
    g_evt.head = (g_evt.head + 1) % EVT_BUF_SZ;
    g_evt.count--;
    return 1;
}

int XPending(Display *dpy) {
    (void)dpy;
    /* Flush penrose WM events into our XEvent queue */
    while (prs_event_count() > 0) {
        prs_event_t pe = prs_event_pop();
        XEvent ev;
        memset(&ev, 0, sizeof(ev));
        switch (pe.type) {
            case PRS_EVENT_MAP_REQUEST: {
                ev.type = MapRequest;
                ev.xmap.window = pe.window;
                evt_push(&ev);
                break;
            }
            case PRS_EVENT_UNMAP_NOTIFY:
                ev.type = UnmapNotify;
                ev.xunmap.window = pe.window;
                evt_push(&ev);
                break;
            case PRS_EVENT_DESTROY_NOTIFY:
                ev.type = DestroyNotify;
                ev.xdestroy.window = pe.window;
                evt_push(&ev);
                break;
            case PRS_EVENT_CONFIGURE_REQUEST:
                ev.type = ConfigureNotify;
                ev.xconfigure.window = pe.window;
                ev.xconfigure.x = pe.cx;
                ev.xconfigure.y = pe.cy;
                ev.xconfigure.width = pe.cw;
                ev.xconfigure.height = pe.ch;
                evt_push(&ev);
                break;
            case PRS_EVENT_KEY_PRESS:
                ev.type = KeyPress;
                ev.xkey.window = pe.window;
                ev.xkey.keycode = (unsigned)pe.detail;
                ev.xkey.state = pe.state;
                evt_push(&ev);
                break;
            case PRS_EVENT_KEY_RELEASE:
                ev.type = KeyRelease;
                ev.xkey.window = pe.window;
                ev.xkey.keycode = (unsigned)pe.detail;
                ev.xkey.state = pe.state;
                evt_push(&ev);
                break;
            case PRS_EVENT_BUTTON_PRESS:
                ev.type = ButtonPress;
                ev.xbutton.window = pe.window;
                ev.xbutton.button = (unsigned)pe.detail;
                ev.xbutton.state = pe.state;
                ev.xbutton.x = pe.mx;
                ev.xbutton.y = pe.my;
                evt_push(&ev);
                break;
            case PRS_EVENT_BUTTON_RELEASE:
                ev.type = ButtonRelease;
                ev.xbutton.window = pe.window;
                ev.xbutton.button = (unsigned)pe.detail;
                ev.xbutton.state = pe.state;
                ev.xbutton.x = pe.mx;
                ev.xbutton.y = pe.my;
                evt_push(&ev);
                break;
            case PRS_EVENT_MOTION:
                ev.type = MotionNotify;
                ev.xmotion.window = pe.window;
                ev.xmotion.x = pe.mx;
                ev.xmotion.y = pe.my;
                ev.xmotion.state = pe.state;
                evt_push(&ev);
                break;
            default:
                break;
        }
    }
    return g_evt.count;
}

int XNextEvent(Display *dpy, XEvent *ev) {
    /* Block until an event is available (poll in a tight loop
     * for now; kernel threads schedule cooperatively). */
    while (!XPending(dpy)) {
        /* yield to scheduler briefly */
        __asm__ volatile("hlt");
    }
    return evt_pop(ev) ? 0 : -1;
}

int XPeekEvent(Display *dpy, XEvent *ev) {
    if (!XPending(dpy)) return -1;
    /* Peek without consuming */
    if (g_evt.count > 0) {
        *ev = g_evt.buf[g_evt.head];
        return 0;
    }
    return -1;
}

int XSendEvent(Display *dpy, Window w, Bool prop, long mask, XEvent *ev) {
    (void)dpy; (void)w; (void)prop; (void)mask;
    /* For client messages, relay to the X11 server */
    if (ev && ev->type == ClientMessage) {
        x11_event_t xev;
        memset(&xev, 0, sizeof(xev));
        xev.type = X11_EVENT_CLIENT_MESSAGE;
        xev.event = w;
        x11_broadcast_event(&xev, -1);
    }
    return 1;
}

int XSelectInput(Display *dpy, Window w, long mask) {
    (void)dpy; (void)w; (void)mask;
    /* Accept but ignore — our event delivery is server-driven */
    return 1;
}

Bool XEventsQueued(Display *dpy, int mode) {
    (void)mode;
    return (Bool)XPending(dpy);
}

int XSync(Display *dpy, Bool discard) {
    (void)discard;
    if (dpy) dpy->sequence++;
    return 1;
}

int XFlush(Display *dpy) {
    if (dpy) dpy->sequence++;
    return 1;
}

/* ══════════════════════════════════════════════════════════════════════
 * Input
 * ══════════════════════════════════════════════════════════════════════ */

int XGrabPointer(Display *dpy, Window w, Bool oe, unsigned em,
        int pm, int km, Window confine, Cursor cur, Time t) {
    (void)dpy; (void)w; (void)oe; (void)em; (void)pm; (void)km;
    (void)confine; (void)cur; (void)t;
    return Success;
}

int XUngrabPointer(Display *dpy, Time t) {
    (void)dpy; (void)t;
    return Success;
}

int XGrabKeyboard(Display *dpy, Window w, Bool oe, int pm, int km, Time t) {
    (void)dpy; (void)w; (void)oe; (void)pm; (void)km; (void)t;
    return Success;
}

int XUngrabKeyboard(Display *dpy, Time t) {
    (void)dpy; (void)t;
    return Success;
}

int XWarpPointer(Display *dpy, Window sw, Window dw,
        int sx, int sy, unsigned sw2, unsigned sh, int dx, int dy) {
    (void)dpy; (void)sw; (void)dw; (void)sx; (void)sy;
    (void)sw2; (void)sh; (void)dx; (void)dy;
    return 1;
}

int XQueryPointer(Display *dpy, Window w, Window *root, Window *child,
        int *rx, int *ry, int *wx, int *wy, unsigned *mask) {
    (void)dpy; (void)w;
    if (root) *root = 1;
    if (child) *child = 0;
    if (rx) *rx = 0;
    if (ry) *ry = 0;
    if (wx) *wx = 0;
    if (wy) *wy = 0;
    if (mask) *mask = 0;
    return 0;
}

unsigned XKeysymToKeycode(Display *dpy, KeySym ks) {
    (void)dpy;
    /* Simple ASCII mapping */
    if (ks >= 0x20 && ks < 0x7f) return (unsigned)(ks - 0x20 + 8);
    return 0;
}

KeySym XStringToKeysym(const char *s) {
    if (!s) return 0;
    /* Very basic: single-char keysyms */
    if (s[1] == 0 && s[0] >= 0x20 && s[0] < 0x7f) return (KeySym)s[0];
    if (strcmp(s, "Return") == 0) return 0xff0d;
    if (strcmp(s, "space") == 0) return 0x0020;
    if (strcmp(s, "Tab") == 0) return 0xff09;
    if (strcmp(s, "Escape") == 0) return 0xff1b;
    if (strcmp(s, "BackSpace") == 0) return 0xff08;
    if (strcmp(s, "Delete") == 0) return 0xffff;
    if (strcmp(s, "Up") == 0) return 0xff52;
    if (strcmp(s, "Down") == 0) return 0xff54;
    if (strcmp(s, "Left") == 0) return 0xff51;
    if (strcmp(s, "Right") == 0) return 0xff53;
    return 0;
}

char *XKeysymToString(KeySym ks) {
    if (ks >= 0x20 && ks < 0x7f) {
        static char buf[2];
        buf[0] = (char)ks;
        buf[1] = 0;
        return buf;
    }
    return NULL;
}

int XLookupKeysym(void *km, int idx) {
    (void)km; (void)idx;
    return 0;
}

/* ══════════════════════════════════════════════════════════════════════
 * GC
 * ══════════════════════════════════════════════════════════════════════ */

typedef struct {
    int in_use;
    unsigned long fg, bg;
    int line_width, line_style, cap_style, join_style;
    int fill_style, fill_rule;
    int clip_x, clip_y;
    unsigned clip_w, clip_h;
    int has_clip;
} stub_gc_t;

#define MAX_STUB_GC 32
static stub_gc_t g_gc[MAX_STUB_GC];

GC XCreateGC(Display *dpy, Drawable d, unsigned long vm, void *vals) {
    (void)dpy; (void)d;
    for (int i = 0; i < MAX_STUB_GC; i++) {
        if (!g_gc[i].in_use) {
            g_gc[i].in_use = 1;
            g_gc[i].fg = 0xFFFFFFFF;
            g_gc[i].bg = 0xFF000000;
            g_gc[i].line_width = 1;
            g_gc[i].fill_style = FillSolid;
            if (vals && vm) {
                XGCValues *v = (XGCValues *)vals;
                if (vm & 0x00000001) g_gc[i].fg = v->foreground;
                if (vm & 0x00000002) g_gc[i].bg = v->background;
                if (vm & 0x00000010) g_gc[i].line_width = v->line_width;
                if (vm & 0x00000020) g_gc[i].line_style = v->line_style;
                if (vm & 0x00000040) g_gc[i].cap_style = v->cap_style;
                if (vm & 0x00000080) g_gc[i].join_style = v->join_style;
                if (vm & 0x00001000) g_gc[i].fill_style = v->fill_style;
            }
            return (GC)(uintptr_t)(i + 1);
        }
    }
    return (GC)0;
}

int XFreeGC(Display *dpy, GC gc) {
    (void)dpy;
    int idx = (int)(uintptr_t)gc - 1;
    if (idx >= 0 && idx < MAX_STUB_GC) g_gc[idx].in_use = 0;
    return 1;
}

int XChangeGC(Display *dpy, GC gc, unsigned long vm, void *vals) {
    (void)dpy;
    int idx = (int)(uintptr_t)gc - 1;
    if (idx < 0 || idx >= MAX_STUB_GC || !g_gc[idx].in_use) return 0;
    if (vals && vm) {
        XGCValues *v = (XGCValues *)vals;
        if (vm & 0x00000001) g_gc[idx].fg = v->foreground;
        if (vm & 0x00000002) g_gc[idx].bg = v->background;
        if (vm & 0x00000010) g_gc[idx].line_width = v->line_width;
        if (vm & 0x00001000) g_gc[idx].fill_style = v->fill_style;
    }
    return 1;
}

static stub_gc_t *get_gc(GC gc) {
    int idx = (int)(uintptr_t)gc - 1;
    if (idx < 0 || idx >= MAX_STUB_GC) return NULL;
    if (!g_gc[idx].in_use) return NULL;
    return &g_gc[idx];
}

int XSetForeground(Display *dpy, GC gc, unsigned long fg) {
    (void)dpy;
    stub_gc_t *g = get_gc(gc);
    if (g) g->fg = fg;
    return 1;
}

int XSetBackground(Display *dpy, GC gc, unsigned long bg) {
    (void)dpy;
    stub_gc_t *g = get_gc(gc);
    if (g) g->bg = bg;
    return 1;
}

int XSetLineAttributes(Display *dpy, GC gc, unsigned lw, int ls, int cs, int js) {
    (void)dpy;
    stub_gc_t *g = get_gc(gc);
    if (g) { g->line_width = (int)lw; g->line_style = ls; g->cap_style = cs; g->join_style = js; }
    return 1;
}

int XSetFillStyle(Display *dpy, GC gc, int fs) {
    (void)dpy;
    stub_gc_t *g = get_gc(gc);
    if (g) g->fill_style = fs;
    return 1;
}

int XSetFillRule(Display *dpy, GC gc, int fr) {
    (void)dpy;
    stub_gc_t *g = get_gc(gc);
    if (g) g->fill_rule = fr;
    return 1;
}

int XSetClipMask(Display *dpy, GC gc, Pixmap pm) {
    (void)dpy; (void)pm;
    stub_gc_t *g = get_gc(gc);
    if (g) g->has_clip = (pm != None);
    return 1;
}

int XSetClipRectangles(Display *dpy, GC gc, int cx, int cy,
        XRectangle *rects, int n) {
    (void)dpy; (void)rects; (void)n;
    stub_gc_t *g = get_gc(gc);
    if (g) { g->clip_x = cx; g->clip_y = cy; g->has_clip = 1; }
    return 1;
}

int XSetDashes(Display *dpy, GC gc, int off, const char *dl, int n) {
    (void)dpy; (void)gc; (void)off; (void)dl; (void)n;
    return 1;
}

int XSetTile(Display *dpy, GC gc, Pixmap tile) {
    (void)dpy; (void)gc; (void)tile;
    return 1;
}

int XSetStipple(Display *dpy, GC gc, Pixmap stipple) {
    (void)dpy; (void)gc; (void)stipple;
    return 1;
}

int XSetTSOrigin(Display *dpy, GC gc, int x, int y) {
    (void)dpy; (void)gc; (void)x; (void)y;
    return 1;
}

int XSetSubwindowMode(Display *dpy, GC gc, int mode) {
    (void)dpy; (void)gc; (void)mode;
    return 1;
}

int XGetGCValues(Display *dpy, GC gc, unsigned long vm, XGCValues *v) {
    (void)dpy;
    stub_gc_t *g = get_gc(gc);
    if (!g || !v) return 0;
    if (vm & 0x00000001) v->foreground = g->fg;
    if (vm & 0x00000002) v->background = g->bg;
    if (vm & 0x00000010) v->line_width = g->line_width;
    if (vm & 0x00001000) v->fill_style = g->fill_style;
    return 1;
}

int XFlushGC(Display *dpy, GC gc) {
    (void)dpy; (void)gc;
    return 1;
}

/* ══════════════════════════════════════════════════════════════════════
 * Drawing — route through penrose/xserver where possible
 * ══════════════════════════════════════════════════════════════════════ */

int XCopyArea(Display *dpy, Drawable src, Drawable dst, GC gc,
        int sx, int sy, unsigned w, unsigned h, int dx, int dy) {
    (void)dpy; (void)src; (void)gc; (void)sx; (void)sy;
    (void)w; (void)h; (void)dx; (void)dy;
    /* Stub — no GPU blit in kernel yet */
    return 1;
}

int XCopyPlane(Display *dpy, Drawable src, Drawable dst, GC gc,
        int sx, int sy, unsigned w, unsigned h, int dx, int dy, unsigned long plane) {
    (void)plane;
    return XCopyArea(dpy, src, dst, gc, sx, sy, w, h, dx, dy);
}

int XFillRectangle(Display *dpy, Drawable d, GC gc,
        int x, int y, unsigned w, unsigned h) {
    (void)dpy;
    stub_gc_t *g = get_gc(gc);
    uint32_t color = g ? (uint32_t)g->fg : 0xFFFFFFFF;
    prs_blit(d, x, y, (int)w, (int)h, &color, 1);
    return 1;
}

int XFillRectangles(Display *dpy, Drawable d, GC gc, XRectangle *rects, int n) {
    for (int i = 0; i < n; i++)
        XFillRectangle(dpy, d, gc, rects[i].x, rects[i].y,
                       rects[i].width, rects[i].height);
    return 1;
}

int XFillPolygon(Display *dpy, Drawable d, GC gc,
        XPoint *pts, int n, int shape, int mode) {
    (void)shape; (void)mode;
    (void)dpy; (void)d; (void)gc; (void)pts; (void)n;
    return 1;
}

int XFillArc(Display *dpy, Drawable d, GC gc,
        int x, int y, unsigned w, unsigned h, int a1, int a2) {
    (void)a1; (void)a2;
    /* Fill as a rectangle approximation */
    return XFillRectangle(dpy, d, gc, x, y, w, h);
}

int XFillArcs(Display *dpy, Drawable d, GC gc, XArc *arcs, int narrcs) {
    for (int i = 0; i < narrcs; i++)
        XFillArc(dpy, d, gc, arcs[i].x, arcs[i].y,
                 arcs[i].width, arcs[i].height, arcs[i].angle1, arcs[i].angle2);
    return 1;
}

int XDrawPoint(Display *dpy, Drawable d, GC gc, int x, int y) {
    return XFillRectangle(dpy, d, gc, x, y, 1, 1);
}

int XDrawPoints(Display *dpy, Drawable d, GC gc, XPoint *pts, int n, int mode) {
    (void)mode;
    for (int i = 0; i < n; i++)
        XDrawPoint(dpy, d, gc, pts[i].x, pts[i].y);
    return 1;
}

int XDrawLine(Display *dpy, Drawable d, GC gc, int x1, int y1, int x2, int y2) {
    (void)dpy; (void)gc; (void)x1; (void)y1; (void)x2; (void)y2;
    /* Stub — line drawing through xserver would need GC context */
    return 1;
}

int XDrawLines(Display *dpy, Drawable d, GC gc, XPoint *pts, int n, int mode) {
    for (int i = 0; i < n - 1; i++)
        XDrawLine(dpy, d, gc, pts[i].x, pts[i].y, pts[i+1].x, pts[i+1].y);
    return 1;
}

int XDrawRectangle(Display *dpy, Drawable d, GC gc,
        int x, int y, unsigned w, unsigned h) {
    (void)dpy; (void)gc;
    /* Stub — draw 4 lines */
    return 1;
}

int XDrawRectangles(Display *dpy, Drawable d, GC gc, XRectangle *r, int n) {
    for (int i = 0; i < n; i++)
        XDrawRectangle(dpy, d, gc, r[i].x, r[i].y, r[i].width, r[i].height);
    return 1;
}

int XDrawArc(Display *dpy, Drawable d, GC gc,
        int x, int y, unsigned w, unsigned h, int a1, int a2) {
    (void)a1; (void)a2;
    return XDrawRectangle(dpy, d, gc, x, y, w, h);
}

int XDrawArcs(Display *dpy, Drawable d, GC gc, XArc *arcs, int narrcs) {
    for (int i = 0; i < narrcs; i++)
        XDrawArc(dpy, d, gc, arcs[i].x, arcs[i].y,
                 arcs[i].width, arcs[i].height, arcs[i].angle1, arcs[i].angle2);
    return 1;
}

int XDrawString(Display *dpy, Drawable d, GC gc, int x, int y,
        const char *string, int length) {
    (void)dpy; (void)d; (void)gc; (void)x; (void)y;
    (void)string; (void)length;
    /* Text rendering stub — HyperDE handles this at compositor level */
    return 1;
}

int XDrawString16(Display *dpy, Drawable d, GC gc, int x, int y,
        const char *string, int length) {
    return XDrawString(dpy, d, gc, x, y, string, length);
}

int XDrawImageString(Display *dpy, Drawable d, GC gc, int x, int y,
        const char *string, int length) {
    return XDrawString(dpy, d, gc, x, y, string, length);
}

int XDrawImageString16(Display *dpy, Drawable d, GC gc, int x, int y,
        const char *string, int length) {
    return XDrawString(dpy, d, gc, x, y, string, length);
}

int XClearArea(Display *dpy, Window w, int x, int y,
        unsigned w2, unsigned h, Bool exp) {
    (void)dpy; (void)x; (void)y; (void)w2; (void)h; (void)exp;
    return 1;
}

int XClearWindow(Display *dpy, Window w) {
    (void)dpy; (void)w;
    return 1;
}

/* ══════════════════════════════════════════════════════════════════════
 * Pixmap / Image
 * ══════════════════════════════════════════════════════════════════════ */

Pixmap XCreatePixmap(Display *dpy, Drawable d, unsigned w, unsigned h, unsigned depth) {
    (void)dpy; (void)d; (void)w; (void)h; (void)depth;
    static Pixmap next_pm = 0x10000;
    return next_pm++;
}

int XFreePixmap(Display *dpy, Pixmap pm) {
    (void)dpy; (void)pm;
    return 1;
}

XImage *XCreateImage(Display *dpy, Visual *vis, unsigned depth,
        int fmt, int off, char *data, unsigned w, unsigned h,
        int bpad, int bpl) {
    (void)dpy; (void)vis; (void)depth; (void)fmt; (void)off;
    (void)bpad;
    XImage *img = (XImage *)malloc(sizeof(XImage));
    if (!img) return NULL;
    memset(img, 0, sizeof(XImage));
    img->width = (int)w;
    img->height = (int)h;
    img->xoffset = 0;
    img->format = 2; /* ZPixmap */
    img->data = data;
    img->byte_order = 0; /* LSBFirst */
    img->bitmap_unit = 32;
    img->bitmap_bit_order = 0;
    img->bitmap_pad = 32;
    img->depth = (int)depth;
    img->bytes_per_line = bpl > 0 ? bpl : (int)w * 4;
    img->bits_per_pixel = 32;
    img->red_mask = 0x00FF0000;
    img->green_mask = 0x0000FF00;
    img->blue_mask = 0x000000FF;
    return img;
}

XImage *XGetImage(Display *dpy, Drawable d, int x, int y,
        unsigned w, unsigned h, unsigned long pm, int fmt) {
    (void)pm;
    int bpl = (int)w * 4;
    char *data = (char *)malloc((size_t)bpl * h);
    if (!data) return NULL;
    memset(data, 0, (size_t)bpl * h);
    return XCreateImage(dpy, NULL, 32, fmt, 0, data, w, h, 32, bpl);
}

XImage *XSubImage(XImage *img, int x, int y, unsigned w, unsigned h) {
    (void)img; (void)x; (void)y; (void)w; (void)h;
    return XCreateImage(NULL, NULL, 32, 2, 0, NULL, w, h, 32, (int)w * 4);
}

int XPutImage(Display *dpy, Drawable d, GC gc, XImage *img,
        int sx, int sy, int dx, int dy, unsigned w, unsigned h) {
    (void)dpy; (void)gc; (void)sx; (void)sy;
    if (!img || !img->data) return 0;
    /* Route through penrose blit if src and dst are the same format */
    if (img->bits_per_pixel == 32) {
        prs_blit(d, dx, dy, (int)w, (int)h, img->data + sy * img->bytes_per_line + sx * 4,
                 img->bytes_per_line / 4);
    }
    return 1;
}

int XDestroyImage(XImage *img) {
    if (img) {
        /* Only free data if we allocated it ourselves */
        free(img);
    }
    return 1;
}

unsigned long XGetPixel(XImage *img, int x, int y) {
    if (!img || !img->data) return 0;
    int off = y * img->bytes_per_line + x * 4;
    if (off < 0 || off + 4 > img->bytes_per_line * img->height) return 0;
    unsigned char *p = (unsigned char *)img->data + off;
    return (unsigned long)p[0] | ((unsigned long)p[1] << 8) |
           ((unsigned long)p[2] << 16) | ((unsigned long)p[3] << 24);
}

int XPutPixel(XImage *img, int x, int y, unsigned long px) {
    if (!img || !img->data) return 0;
    int off = y * img->bytes_per_line + x * 4;
    if (off < 0 || off + 4 > img->bytes_per_line * img->height) return 0;
    unsigned char *p = (unsigned char *)img->data + off;
    p[0] = (unsigned char)(px & 0xFF);
    p[1] = (unsigned char)((px >> 8) & 0xFF);
    p[2] = (unsigned char)((px >> 16) & 0xFF);
    p[3] = (unsigned char)((px >> 24) & 0xFF);
    return 1;
}

/* ══════════════════════════════════════════════════════════════════════
 * Font
 * ══════════════════════════════════════════════════════════════════════ */

XFontStruct *XLoadQueryFont(Display *dpy, const char *name) {
    (void)dpy; (void)name;
    /* Return a small heap-allocated stub so callers can free it */
    XFontStruct *f = (XFontStruct *)malloc(64);
    if (f) memset(f, 0, 64);
    return f;
}

int XFreeFont(Display *dpy, XFontStruct *f) {
    (void)dpy; (void)f;
    return 1;
}

int XSetFont(Display *dpy, GC gc, unsigned long font) {
    (void)dpy; (void)gc; (void)font;
    return 1;
}

int XTextWidth(XFontStruct *f, const char *s, int n) {
    (void)f;
    /* Assume 8px per character for basic metrics */
    return n * 8;
}

int XTextExtents(XFontStruct *f, const char *s, int n,
        int *dir, int *asc, int *desc, void *ov) {
    (void)f; (void)s; (void)ov;
    if (dir) *dir = 0;
    if (asc) *asc = 12;
    if (desc) *desc = 4;
    return 1;
}

char **XListFonts(Display *dpy, const char *pat, int max, int *cnt) {
    (void)dpy; (void)pat; (void)max;
    if (cnt) *cnt = 0;
    return NULL;
}

int XFreeFontNames(char **list) {
    (void)list;
    return 1;
}

/* ══════════════════════════════════════════════════════════════════════
 * Color
 * ══════════════════════════════════════════════════════════════════════ */

int XAllocColor(Display *dpy, Colormap cm, XColor *c) {
    (void)dpy; (void)cm;
    if (!c) return 0;
    /* Just pass through — pixel is already set */
    return 1;
}

int XAllocNamedColor(Display *dpy, Colormap cm, const char *name,
        XColor *sc, XColor *ec) {
    (void)dpy; (void)cm; (void)name; (void)sc; (void)ec;
    return 0;
}

int XFreeColors(Display *dpy, Colormap cm, unsigned long *px, int n, unsigned long pl) {
    (void)dpy; (void)cm; (void)px; (void)n; (void)pl;
    return 1;
}

Colormap XCreateColormap(Display *dpy, Window w, Visual *v, int a) {
    (void)dpy; (void)w; (void)v; (void)a;
    static Colormap next_cm = 2;
    return next_cm++;
}

int XFreeColormap(Display *dpy, Colormap cm) {
    (void)dpy; (void)cm;
    return 1;
}

int XCopyColormapAndFree(Display *dpy, Colormap cm) {
    (void)dpy; (void)cm;
    return (int)XCreateColormap(dpy, 0, NULL, 0);
}

Colormap XDefaultColormap(Display *dpy, int scr) {
    (void)scr;
    return dpy ? dpy->default_cmap : 1;
}

Visual *XDefaultVisual(Display *dpy, int scr) {
    (void)scr;
    return dpy ? dpy->default_visual : NULL;
}

int XMatchVisualInfo(Display *dpy, int scr, int d, int cls, XVisualInfo *vi) {
    (void)dpy; (void)scr;
    if (!vi) return 0;
    memset(vi, 0, sizeof(XVisualInfo));
    vi->depth = d;
    vi->class = cls;
    vi->red_mask = 0x00FF0000;
    vi->green_mask = 0x0000FF00;
    vi->blue_mask = 0x000000FF;
    vi->colormap_entries = 256;
    vi->bits_per_rgb = 8;
    return 1;
}

int XParseColor(Display *dpy, Colormap cm, const char *spec, XColor *c) {
    (void)dpy; (void)cm;
    if (!spec || !c) return 0;
    /* Parse #RRGGBB */
    if (spec[0] == '#' && strlen(spec) == 7) {
        unsigned r = 0, g = 0, b = 0;
        /* Simple hex parse */
        for (int i = 1; i < 7; i++) {
            char ch = spec[i];
            unsigned v = 0;
            if (ch >= '0' && ch <= '9') v = ch - '0';
            else if (ch >= 'a' && ch <= 'f') v = ch - 'a' + 10;
            else if (ch >= 'A' && ch <= 'F') v = ch - 'A' + 10;
            if (i < 3) r = (r << 4) | v;
            else if (i < 5) g = (g << 4) | v;
            else b = (b << 4) | v;
        }
        c->red = (unsigned short)(r * 257);
        c->green = (unsigned short)(g * 257);
        c->blue = (unsigned short)(b * 257);
        c->pixel = (r << 16) | (g << 8) | b;
        return 1;
    }
    return 0;
}

int XStoreColors(Display *dpy, Colormap cm, XColor *c, int n) {
    (void)dpy; (void)cm; (void)c; (void)n;
    return 1;
}

int XStoreNamedColor(Display *dpy, Colormap cm, const char *name,
        unsigned long px, int fl) {
    (void)dpy; (void)cm; (void)name; (void)px; (void)fl;
    return 1;
}

int XQueryColor(Display *dpy, Colormap cm, XColor *c) {
    (void)dpy; (void)cm;
    /* pixel → rgb fallback */
    if (c) {
        unsigned px = (unsigned)c->pixel;
        c->red = (unsigned short)(((px >> 16) & 0xFF) * 257);
        c->green = (unsigned short)(((px >> 8) & 0xFF) * 257);
        c->blue = (unsigned short)((px & 0xFF) * 257);
    }
    return 1;
}

int XQueryColors(Display *dpy, Colormap cm, XColor *cs, int n) {
    for (int i = 0; i < n; i++) XQueryColor(dpy, cm, &cs[i]);
    return 1;
}

int XLookupColor(Display *dpy, Colormap cm, const char *spec,
        XColor *ec, XColor *sc) {
    int r = XParseColor(dpy, cm, spec, sc);
    if (ec && sc) *ec = *sc;
    return r;
}

int XInstallColormap(Display *dpy, Colormap cm) {
    (void)dpy; (void)cm;
    return 1;
}

int XUninstallColormap(Display *dpy, Colormap cm) {
    (void)dpy; (void)cm;
    return 1;
}

/* ══════════════════════════════════════════════════════════════════════
 * Selection
 * ══════════════════════════════════════════════════════════════════════ */

int XSetSelectionOwner(Display *dpy, Atom sel, Window w, Time t) {
    (void)dpy; (void)sel; (void)w; (void)t;
    return 1;
}

Window XGetSelectionOwner(Display *dpy, Atom sel) {
    (void)dpy; (void)sel;
    return 0;
}

int XConvertSelection(Display *dpy, Atom sel, Atom tgt, Atom prop, Window w, Time t) {
    (void)dpy; (void)sel; (void)tgt; (void)prop; (void)w; (void)t;
    return 1;
}

/* ══════════════════════════════════════════════════════════════════════
 * Cursor
 * ══════════════════════════════════════════════════════════════════════ */

Cursor XCreateFontCursor(Display *dpy, unsigned shape) {
    (void)dpy; (void)shape;
    static Cursor next_cur = 0x1000;
    return next_cur++;
}

Cursor XCreatePixmapCursor(Display *dpy, Pixmap src, Pixmap msk,
        XColor *fg, XColor *bg, unsigned x, unsigned y) {
    (void)dpy; (void)src; (void)msk; (void)fg; (void)bg; (void)x; (void)y;
    return XCreateFontCursor(dpy, 0);
}

Cursor XCreateGlyphCursor(Display *dpy, void *sf, void *mf,
        unsigned sc, unsigned mc, XColor *fg, XColor *bg) {
    (void)dpy; (void)sf; (void)mf; (void)sc; (void)mc; (void)fg; (void)bg;
    return XCreateFontCursor(dpy, 0);
}

int XFreeCursor(Display *dpy, Cursor c) {
    (void)dpy; (void)c;
    return 1;
}

int XRecolorCursor(Display *dpy, Cursor c, XColor *fg, XColor *bg) {
    (void)dpy; (void)c; (void)fg; (void)bg;
    return 1;
}

/* ══════════════════════════════════════════════════════════════════════
 * Window properties (WM hints, etc.)
 * ══════════════════════════════════════════════════════════════════════ */

int XSetWindowBackground(Display *dpy, Window w, unsigned long px) {
    (void)dpy; (void)w; (void)px;
    return 1;
}

int XSetWindowBorderWidth(Display *dpy, Window w, unsigned bw) {
    (void)dpy;
    x11_window_t *xw = x11_get_window(w);
    if (xw) xw->border_width = (int)bw;
    return 1;
}

int XSetWindowBorder(Display *dpy, Window w, unsigned long px) {
    (void)dpy; (void)w; (void)px;
    return 1;
}

int XSetWMName(Display *dpy, Window w, XTextProperty *tp) {
    if (!dpy || !tp || !tp->value) return 0;
    /* Treat as a window title */
    int n = (int)tp->nitems;
    if (n > 63) n = 63;
    char buf[64];
    memcpy(buf, tp->value, n);
    buf[n] = 0;
    prs_set_title(w, buf);
    return 1;
}

int XSetWMProtocols(Display *dpy, Window w, Atom *protos, int n) {
    (void)dpy; (void)w; (void)protos; (void)n;
    return 1;
}

int XGetWMProtocols(Display *dpy, Window w, Atom **protos, int *n) {
    (void)dpy; (void)w;
    if (protos) *protos = NULL;
    if (n) *n = 0;
    return 1;
}

int XGetWMNormalHints(Display *dpy, Window w, XSizeHints *h, long *sup) {
    (void)dpy; (void)w;
    if (h) memset(h, 0, sizeof(XSizeHints));
    if (sup) *sup = 0;
    return 1;
}

int XSetWMNormalHints(Display *dpy, Window w, XSizeHints *h) {
    (void)dpy; (void)w; (void)h;
    return 1;
}

int XSetWMHints(Display *dpy, Window w, XWMHints *h) {
    (void)dpy; (void)w; (void)h;
    return 1;
}

int XGetWMHints(Display *dpy, Window w, XWMHints *h) {
    (void)dpy; (void)w;
    if (h) memset(h, 0, sizeof(XWMHints));
    return 1;
}

int XSetClassHint(Display *dpy, Window w, XClassHint *h) {
    (void)dpy; (void)w; (void)h;
    return 1;
}

int XGetClassHint(Display *dpy, Window w, XClassHint *h) {
    (void)dpy; (void)w;
    if (h) { h->res_name = NULL; h->res_class = NULL; }
    return 0;
}

int XSetCommand(Display *dpy, Window w, char **argv, int argc) {
    (void)dpy; (void)w; (void)argv; (void)argc;
    return 1;
}

int XGetCommand(Display *dpy, Window w, char ***argv, int *argc) {
    (void)dpy; (void)w;
    if (argv) *argv = NULL;
    if (argc) *argc = 0;
    return 1;
}

int XGetIconName(Display *dpy, Window w, char **name) {
    (void)dpy; (void)w;
    if (name) *name = NULL;
    return 0;
}

int XSetIconName(Display *dpy, Window w, const char *name) {
    (void)dpy; (void)w; (void)name;
    return 1;
}

Bool XStringListToTextProperty(char **list, int cnt, XTextProperty *tp) {
    if (!tp) return False;
    if (!list || cnt <= 0) {
        tp->value = NULL; tp->nitems = 0;
        return True;
    }
    int total = 0;
    for (int i = 0; i < cnt; i++) total += (int)strlen(list[i]) + 1;
    char *buf = (char *)malloc(total + 1);
    if (!buf) return False;
    buf[0] = 0;
    for (int i = 0; i < cnt; i++) {
        strcat(buf, list[i]);
        if (i < cnt - 1) strcat(buf, " ");
    }
    tp->value = (unsigned char *)buf;
    tp->nitems = (unsigned long)strlen(buf);
    tp->encoding = XA_STRING;
    tp->format = 8;
    return True;
}

Bool XTextPropertyToStringList(XTextProperty *tp, char ***list, int *cnt) {
    if (!tp || !list || !cnt) return False;
    *list = NULL; *cnt = 0;
    if (!tp->value || tp->nitems == 0) return True;
    char **l = (char **)malloc(sizeof(char *) * 2);
    if (!l) return False;
    l[0] = (char *)malloc(tp->nitems + 1);
    if (!l[0]) { free(l); return False; }
    memcpy(l[0], tp->value, tp->nitems);
    l[0][tp->nitems] = 0;
    l[1] = NULL;
    *list = l;
    *cnt = 1;
    return True;
}

int XFreeStringList(char **list) {
    if (!list) return 1;
    for (int i = 0; list[i]; i++) free(list[i]);
    free(list);
    return 1;
}

/* ══════════════════════════════════════════════════════════════════════
 * Misc
 * ══════════════════════════════════════════════════════════════════════ */

int XFree(void *data) {
    if (data) free(data);
    return 1;
}

int XKillClient(Display *dpy, Window w) {
    (void)dpy;
    prs_kill_client(w);
    return 1;
}

int XSetCloseDownMode(Display *dpy, int mode) {
    (void)dpy; (void)mode;
    return 1;
}

int XNoOp(Display *dpy) {
    (void)dpy;
    return 1;
}

int XGrabServer(Display *dpy) {
    (void)dpy;
    return 1;
}

int XUngrabServer(Display *dpy) {
    (void)dpy;
    return 1;
}

int XChangeSaveSet(Display *dpy, Window w, int mode) {
    (void)dpy; (void)w; (void)mode;
    return 1;
}

int XAddToSaveSet(Display *dpy, Window w) {
    (void)dpy; (void)w;
    return 1;
}

int XRemoveFromSaveSet(Display *dpy, Window w) {
    (void)dpy; (void)w;
    return 1;
}

int XSetErrorHandler(void *handler) {
    (void)handler;
    return 1;
}

int XSetIOErrorHandler(void *handler) {
    (void)handler;
    return 1;
}

Status XGetErrorText(Display *dpy, int code, char *buf, int len) {
    (void)dpy; (void)code;
    if (buf && len > 0) { buf[0] = 0; }
    return 1;
}

/* ══════════════════════════════════════════════════════════════════════
 * Extension stubs
 * ══════════════════════════════════════════════════════════════════════ */

Bool XQueryExtension(Display *dpy, const char *name,
        int *major, int *first_event, int *first_error) {
    (void)dpy; (void)name;
    if (major) *major = 0;
    if (first_event) *first_event = 0;
    if (first_error) *first_error = 0;
    return False;
}

char **XListExtensions(Display *dpy, int *n) {
    (void)dpy;
    if (n) *n = 0;
    return NULL;
}

int XFreeExtensionList(char **list) {
    (void)list;
    return 1;
}

/* ══════════════════════════════════════════════════════════════════════
 * XShm stubs
 * ══════════════════════════════════════════════════════════════════════ */

Status XShmQueryExtension(Display *dpy) {
    (void)dpy;
    return False;
}

Status XShmPixmapFormat(Display *dpy) {
    (void)dpy;
    return 0;
}

Status XShmPutImage(Display *dpy, Drawable d, GC gc, XImage *img,
        int sx, int sy, int dx, int dy, unsigned sw, unsigned sh, Bool se) {
    return XPutImage(dpy, d, gc, img, sx, sy, dx, dy, sw, sh);
}

/* ══════════════════════════════════════════════════════════════════════
 * XShape stubs
 * ══════════════════════════════════════════════════════════════════════ */

int XShapeQueryExtension(Display *dpy, int *ev, int *err) {
    (void)dpy;
    if (ev) *ev = 0;
    if (err) *err = 0;
    return False;
}

void XShapeCombineMask(Display *dpy, Window d, int dk, int x, int y, Pixmap m, int op) {
    (void)dpy; (void)d; (void)dk; (void)x; (void)y; (void)m; (void)op;
}

void XShapeCombineRegion(Display *dpy, Window d, int dk, int x, int y, void *r, int op) {
    (void)dpy; (void)d; (void)dk; (void)x; (void)y; (void)r; (void)op;
}

/* ══════════════════════════════════════════════════════════════════════
 * XRandR stubs
 * ══════════════════════════════════════════════════════════════════════ */

Status XRRQueryExtension(Display *dpy, int *ev, int *err) {
    (void)dpy;
    if (ev) *ev = 0;
    if (err) *err = 0;
    return False;
}

void *XRRGetScreenResources(Display *dpy, Window w) {
    (void)dpy; (void)w;
    return NULL;
}

void XRRFreeScreenResources(void *res) {
    (void)res;
}

int XRRGetOutputPrimary(Display *dpy, Window w) {
    (void)dpy; (void)w;
    return 0;
}

/* ══════════════════════════════════════════════════════════════════════
 * XComposite / XDamage / XFixes stubs
 * ══════════════════════════════════════════════════════════════════════ */

Status XCompositeQueryExtension(Display *dpy, int *ev, int *err) {
    (void)dpy;
    if (ev) *ev = 0;
    if (err) *err = 0;
    return False;
}

void XCompositeRedirectSubwindows(Display *dpy, Window w, int u) {
    (void)dpy; (void)w; (void)u;
}

int XDamageQueryExtension(Display *dpy, int *ev, int *err) {
    (void)dpy;
    if (ev) *ev = 0;
    if (err) *err = 0;
    return False;
}

void *XDamageCreate(Display *dpy, Drawable d, int l) {
    (void)dpy; (void)d; (void)l;
    return NULL;
}

void XDamageDestroy(Display *dpy, void *dm) {
    (void)dpy; (void)dm;
}

void XDamageSubtract(Display *dpy, void *dm, void *r, void *p) {
    (void)dpy; (void)dm; (void)r; (void)p;
}

Status XFixesQueryExtension(Display *dpy, int *ev, int *err) {
    (void)dpy;
    if (ev) *ev = 0;
    if (err) *err = 0;
    return False;
}

void XFixesSelectSelectionInput(Display *dpy, Window w, Atom s, unsigned long m) {
    (void)dpy; (void)w; (void)s; (void)m;
}

void XFixesSelectCursorInput(Display *dpy, Window w, unsigned long m) {
    (void)dpy; (void)w; (void)m;
}

Cursor XFixesCreateCursor(Display *dpy, Cursor src, int x, int y) {
    (void)dpy; (void)src; (void)x; (void)y;
    return XCreateFontCursor(dpy, 0);
}

void XFixesSetCursorName(Display *dpy, Cursor c, const char *n) {
    (void)dpy; (void)c; (void)n;
}

const char *XFixesGetCursorName(Display *dpy, Cursor c, Atom *a) {
    (void)dpy; (void)c;
    if (a) *a = None;
    return "left_ptr";
}

/* ══════════════════════════════════════════════════════════════════════
 * XRender stubs
 * ══════════════════════════════════════════════════════════════════════ */

void *XRenderCreatePicture(Display *dpy, Pixmap pm, void *fmt,
        unsigned long vm, void *v) {
    (void)dpy; (void)pm; (void)fmt; (void)vm; (void)v;
    static int fake_pic = 0;
    return (void *)(uintptr_t)(++fake_pic);
}

void XRenderFreePicture(Display *dpy, void *p) {
    (void)dpy; (void)p;
}

void XRenderComposite(Display *dpy, int op, void *src, void *mask, void *dst,
        int sx, int sy, int mx, int my, int dx, int dy, unsigned w, unsigned h) {
    (void)dpy; (void)op; (void)src; (void)mask; (void)dst;
    (void)sx; (void)sy; (void)mx; (void)my;
    (void)dx; (void)dy; (void)w; (void)h;
}

void *XRenderFindVisualFormat(Display *dpy, Visual *v) {
    (void)dpy; (void)v;
    return NULL;
}

/* ══════════════════════════════════════════════════════════════════════
 * XInput2 stubs
 * ══════════════════════════════════════════════════════════════════════ */

int XISelectEvents(Display *dpy, Window w, void *m, int l) {
    (void)dpy; (void)w; (void)m; (void)l;
    return 1;
}

/* ══════════════════════════════════════════════════════════════════════
 * XKB stubs
 * ══════════════════════════════════════════════════════════════════════ */

int XkbGetKeyboard(Display *dpy, unsigned w, unsigned ds) {
    (void)dpy; (void)w; (void)ds;
    return 0;
}

int XkbGetControls(Display *dpy, unsigned long w, void *s) {
    (void)dpy; (void)w; (void)s;
    return 0;
}

int XkbSetControls(Display *dpy, unsigned long w, void *s) {
    (void)dpy; (void)w; (void)s;
    return 1;
}

int XkbLatchLockState(Display *dpy, unsigned ds, unsigned aff, unsigned val,
        unsigned sl, unsigned ll, unsigned ul) {
    (void)dpy; (void)ds; (void)aff; (void)val; (void)sl; (void)ll; (void)ul;
    return 1;
}

/* ══════════════════════════════════════════════════════════════════════
 * Xcursor stub
 * ══════════════════════════════════════════════════════════════════════ */

Cursor XcursorImageLoadCursor(Display *dpy, void *img) {
    (void)dpy; (void)img;
    return XCreateFontCursor(dpy, 68); /* XC_left_ptr */
}

/* ══════════════════════════════════════════════════════════════════════
 * Region stubs
 * ══════════════════════════════════════════════════════════════════════ */

void *XCreateRegion(void) {
    return malloc(64);
}

int XDestroyRegion(void *r) {
    if (r) free(r);
    return 1;
}

int XUnionRegion(void *s1, void *s2, void *d) {
    (void)s1; (void)s2; (void)d;
    return 1;
}

int XIntersectRegion(void *s1, void *s2, void *d) {
    (void)s1; (void)s2; (void)d;
    return 1;
}

int XSubtractRegion(void *s1, void *s2, void *d) {
    (void)s1; (void)s2; (void)d;
    return 1;
}

int XEmptyRegion(void *r) {
    (void)r;
    return 1;
}

int XOffsetRegion(void *r, int dx, int dy) {
    (void)r; (void)dx; (void)dy;
    return 1;
}

int XPointInRegion(void *r, int x, int y) {
    (void)r; (void)x; (void)y;
    return 0;
}

/* ══════════════════════════════════════════════════════════════════════
 * Remaining stubs (return safe defaults)
 * ══════════════════════════════════════════════════════════════════════ */

int XmbTextPropertyToTextList(Display *dpy, XTextProperty *tp,
        char ***list, int *cnt) {
    return (int)XTextPropertyToStringList(tp, list, cnt);
}

int XFreeFontPath(char **list) {
    (void)list;
    return 1;
}

int XSetWMClientMachine(Display *dpy, Window w, XTextProperty *tp) {
    (void)dpy; (void)w; (void)tp;
    return 1;
}

int XGetWMClientMachine(Display *dpy, Window w, XTextProperty *tp) {
    (void)dpy; (void)w;
    if (tp) { tp->value = NULL; tp->nitems = 0; }
    return 0;
}

int XSetWMIconName(Display *dpy, Window w, XTextProperty *tp) {
    (void)dpy; (void)w; (void)tp;
    return 1;
}

int XGetWMIconName(Display *dpy, Window w, XTextProperty *tp) {
    (void)dpy; (void)w;
    if (tp) { tp->value = NULL; tp->nitems = 0; }
    return 0;
}

int XGetWMName(Display *dpy, Window w, XTextProperty *tp) {
    (void)dpy; (void)w;
    if (tp) { tp->value = NULL; tp->nitems = 0; }
    return 0;
}

int XSetWMCommand(Display *dpy, Window w, XTextProperty *tp) {
    (void)dpy; (void)w; (void)tp;
    return 1;
}

int XGetWMCommand(Display *dpy, Window w, XTextProperty *tp) {
    (void)dpy; (void)w;
    if (tp) { tp->value = NULL; tp->nitems = 0; }
    return 0;
}

int XSetTextProperty(Display *dpy, Window w, XTextProperty *tp, Atom prop) {
    (void)dpy; (void)w; (void)prop;
    if (tp) XSetWMName(dpy, w, tp);
    return 1;
}

Status XGetTextProperty(Display *dpy, Window w, XTextProperty *tp, Atom prop) {
    (void)dpy; (void)w; (void)prop;
    if (tp) { tp->value = NULL; tp->nitems = 0; }
    return 0;
}

int XAllocClassHint(Display *dpy, Window w, XClassHint **h) {
    (void)dpy; (void)w;
    if (h) {
        *h = (XClassHint *)malloc(sizeof(XClassHint));
        if (*h) { (*h)->res_name = NULL; (*h)->res_class = NULL; }
    }
    return 1;
}

int XAllocSizeHints(Display *dpy, Window w, XSizeHints **h) {
    (void)dpy; (void)w;
    if (h) {
        *h = (XSizeHints *)malloc(sizeof(XSizeHints));
        if (*h) memset(*h, 0, sizeof(XSizeHints));
    }
    return 1;
}

int XAllocWMHints(Display *dpy, Window w, XWMHints **h) {
    (void)dpy; (void)w;
    if (h) {
        *h = (XWMHints *)malloc(sizeof(XWMHints));
        if (*h) memset(*h, 0, sizeof(XWMHints));
    }
    return 1;
}

int XSetStandardProperties(Display *dpy, Window w, const char *wn,
        const char *in, Pixmap ip, char **argv, int argc, XSizeHints *sh) {
    (void)in; (void)ip; (void)argv; (void)argc;
    if (wn) {
        XTextProperty tp;
        tp.value = (unsigned char *)wn;
        tp.nitems = strlen(wn);
        tp.encoding = XA_STRING;
        tp.format = 8;
        XSetWMName(dpy, w, &tp);
    }
    if (sh) XSetWMNormalHints(dpy, w, sh);
    return 1;
}

int XGetTransientForHint(Display *dpy, Window w, Window *pw) {
    (void)dpy; (void)w;
    if (pw) *pw = 0;
    return 0;
}

int XSetTransientForHint(Display *dpy, Window w, Window pw) {
    (void)dpy; (void)w; (void)pw;
    return 1;
}

int XGetIconSizes(Display *dpy, Window w, int **sl, int *cnt) {
    (void)dpy; (void)w;
    if (sl) *sl = NULL;
    if (cnt) *cnt = 0;
    return 0;
}

int XSetIconSizes(Display *dpy, Window w, int *wl, int *hl, int cnt) {
    (void)dpy; (void)w; (void)wl; (void)hl; (void)cnt;
    return 1;
}

int XSetWMProperties(Display *dpy, Window w, XTextProperty *wn,
        XTextProperty *in, char **argv, int argc, XSizeHints *sh,
        XWMHints *wmh, XClassHint *ch) {
    if (wn) XSetWMName(dpy, w, wn);
    (void)in; (void)argv; (void)argc;
    if (sh) XSetWMNormalHints(dpy, w, sh);
    if (wmh) XSetWMHints(dpy, w, wmh);
    if (ch) XSetClassHint(dpy, w, ch);
    return 1;
}

int XGetStandardColormap(Display *dpy, Window w, void *cm, Atom prop) {
    (void)dpy; (void)w; (void)cm; (void)prop;
    return 0;
}

int XAddHost(Display *dpy, XHostAddress *h) {
    (void)dpy; (void)h;
    return 1;
}

int XAddHosts(Display *dpy, XHostAddress *h, int n) {
    (void)dpy; (void)h; (void)n;
    return 1;
}

int XRemoveHost(Display *dpy, XHostAddress *h) {
    (void)dpy; (void)h;
    return 1;
}

int XRemoveHosts(Display *dpy, XHostAddress *h, int n) {
    (void)dpy; (void)h; (void)n;
    return 1;
}

int XListHosts(Display *dpy, Bool *s, XHostAddress **h) {
    (void)dpy;
    if (s) *s = False;
    if (h) *h = NULL;
    return 0;
}

int XSetAccessControl(Display *dpy, int m) {
    (void)dpy; (void)m;
    return 1;
}

int XChangePointerControl(Display *dpy, Bool a, Bool t, int an, int ad, int tv) {
    (void)dpy; (void)a; (void)t; (void)an; (void)ad; (void)tv;
    return 1;
}

int XGetPointerControl(Display *dpy, int *an, int *ad, int *t) {
    (void)dpy;
    if (an) *an = 1;
    if (ad) *ad = 1;
    if (t) *t = 0;
    return 1;
}

int XSetScreenSaver(Display *dpy, int to, int iv, int pb, int ae) {
    (void)dpy; (void)to; (void)iv; (void)pb; (void)ae;
    return 1;
}

int XGetScreenSaver(Display *dpy, int *to, int *iv, int *pb, int *ae) {
    (void)dpy;
    if (to) *to = 0;
    if (iv) *iv = 0;
    if (pb) *pb = 1;
    if (ae) *ae = 0;
    return 1;
}

int XForceScreenSaver(Display *dpy, int m) {
    (void)dpy; (void)m;
    return 1;
}

int XSetWMZoomHints(Display *dpy, Window w, XSizeHints *h) {
    return XSetWMNormalHints(dpy, w, h);
}

int XSetZoomHints(Display *dpy, Window w, XSizeHints *h) {
    return XSetWMNormalHints(dpy, w, h);
}

int XPermStoreColors(Display *dpy, Colormap cm, void *c, int n) {
    (void)dpy; (void)cm; (void)c; (void)n;
    return 1;
}

int XPermAllocAllColors(Display *dpy, Colormap cm, void *c) {
    (void)dpy; (void)cm; (void)c;
    return 1;
}

int XAllocColorCells(Display *dpy, Colormap cm, Bool cont,
        unsigned long *pm, unsigned np, unsigned long *px, unsigned npx) {
    (void)dpy; (void)cm; (void)cont;
    if (pm) for (unsigned i = 0; i < np; i++) pm[i] = 0;
    if (px) for (unsigned i = 0; i < npx; i++) px[i] = i;
    return 1;
}

int XAllocColorPlanes(Display *dpy, Colormap cm, Bool cont,
        unsigned long *px, int nc, int nr, int ng, int nb,
        unsigned long *rm, unsigned long *gm, unsigned long *bm) {
    (void)dpy; (void)cm; (void)cont; (void)nc; (void)nr; (void)ng; (void)nb;
    if (px) for (int i = 0; i < nc; i++) px[i] = (unsigned long)i;
    if (rm) *rm = 0xFF0000;
    if (gm) *gm = 0x00FF00;
    if (bm) *bm = 0x0000FF;
    return 1;
}

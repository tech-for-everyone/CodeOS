#ifndef X11_STUBS_H
#define X11_STUBS_H
/* ── Xlib-compatible C stubs for in-kernel Rust crates (xlib, x11-dl) ──
 *
 * These provide the extern "C" symbols that the upstream xlib and x11-dl
 * Rust crates link against.  Internally they call x11_server.c and
 * penrose_bridge.c directly (all in-kernel, no IPC). */
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ── basic X11 typedefs ── */
typedef unsigned long  Window;
typedef unsigned long  Atom;
typedef unsigned long  Time;
typedef unsigned long  Cursor;
typedef unsigned long  Colormap;
typedef unsigned long  Pixmap;
typedef unsigned long  Drawable;
typedef unsigned long  VisualID;
typedef unsigned long  Pixel;
typedef unsigned long  KeySym;
typedef int            Status;
typedef int            Bool;
#define True  1
#define False 0

/* X constants */
#define Success             0
#define BadValue           12
#define BadAccess          10
#define None               0L
#define CurrentTime        0L
#define AnyPropertyType    0L
#define CopyFromParent     0L
#define AllPlanes          (~0L)
#define ExposureMask       (1L<<15)
#define KeyPressMask       (1L<<0)
#define KeyReleaseMask     (1L<<1)
#define ButtonPressMask    (1L<<2)
#define ButtonReleaseMask  (1L<<3)
#define PointerMotionMask  (1L<<6)
#define StructureNotifyMask (1L<<17)
#define SubstructureNotifyMask   (1L<<18)
#define SubstructureRedirectMask (1L<<19)
#define PropertyChangeMask (1L<<22)
#define InputOutput        1
#define IsTrue             1
#define IsFalse            0

/* Event types */
#define KeyPress           2
#define KeyRelease         3
#define ButtonPress        4
#define ButtonRelease      5
#define MotionNotify       6
#define EnterNotify        7
#define LeaveNotify        8
#define FocusIn            9
#define FocusOut          10
#define Expose            12
#define NoExposure        14
#define VisibilityNotify  15
#define CreateNotify      16
#define DestroyNotify     17
#define UnmapNotify       18
#define MapNotify         19
#define MapRequest        20
#define ReparentNotify    21
#define ConfigureNotify   22
#define ConfigureRequest  23
#define GravityNotify     24
#define ResizeRequest     25
#define PropertyNotify    28
#define SelectionClear    29
#define SelectionRequest  30
#define SelectionNotify   31
#define ColormapNotify    32
#define ClientMessage     33
#define MappingNotify     34

/* GC values */
#define GXcopy            3
#define GXand             1
#define GXor              7
#define GXxor             6
#define LineSolid         0
#define FillSolid         0
#define FillTiled         1

/* ── opaque handles ── */
typedef struct _XDisplay  Display;
typedef struct _XScreen   Screen;
typedef struct _XGC      *GC;
typedef struct _XFontStruct XFontStruct;
typedef struct _XImage    XImage;

/* ── Visual ── */
typedef struct {
    VisualID visualid;
    int      screen;
    int      depth;
    int      class;
    unsigned long red_mask, green_mask, blue_mask;
    int      colormap_entries;
    int      bits_per_rgb;
} XVisualInfo;

typedef struct {
    int depth, class;
    unsigned long red_mask, green_mask, blue_mask;
    int colormap_entries, bits_per_rgb;
} Visual;

/* ── XColor ── */
typedef struct {
    unsigned long pixel;
    unsigned short red, green, blue;
    char flags; char pad;
} XColor;

/* ── XGCValues ── */
typedef struct {
    int function, line_width, line_style, cap_style, join_style;
    int fill_style, fill_rule, arc_mode;
    unsigned long foreground, background;
    int dash_offset; char dashes;
} XGCValues;

/* ── XWindowAttributes ── */
typedef struct {
    int x, y, width, height, border_width, depth;
    int class, bit_gravity, win_gravity;
    int backing_store, map_state;
    int override_redirect, map_installed, save_under;
    Visual *visual;
    Window root;
    Colormap colormap;
    unsigned long backing_planes, backing_pixel;
    long do_not_propagate_mask;
    Screen *screen;
} XWindowAttributes;

/* ── XImage ── */
struct _XImage {
    int width, height, xoffset, format;
    char *data;
    int byte_order, bitmap_unit, bitmap_bit_order, bitmap_pad;
    int depth, bytes_per_line, bits_per_pixel;
    unsigned long red_mask, green_mask, blue_mask;
    void *obdata;
    struct {
        void (*free_image_data)(XImage *);
        int (*byte_order_handler)(XImage *);
        int (*create_image_handler)(XImage *);
        int (*get_pixel_handler)(XImage *, int, int);
        int (*put_pixel_handler)(XImage *, int, int, unsigned long);
        int (*sub_image_handler)(XImage *, int, int, unsigned, unsigned, XImage *, int, int);
        int (*add_pixel_handler)(XImage *, int, long);
    } f;
};

/* ── XRectangle / XArc / XPoint ── */
typedef struct { short x, y; unsigned short width, height; } XRectangle;
typedef struct { short x, y; unsigned short width, height; short angle1, angle2; } XArc;
typedef struct { int x, y; } XPoint;

/* ── XEvent union ── */
typedef struct {
    int type; Window window; unsigned long serial; Bool send_event;
    Display *display;
} XAnyEvent;

typedef struct {
    int type; Window window;
} XCreateWindowEvent;

typedef struct {
    int type; Window event; Window window; Bool override_redirect;
} XMapEvent;

typedef struct {
    int type; Window event; Window window;
} XUnmapEvent;

typedef struct {
    int type; Window event; Window window;
} XDestroyEvent;

typedef struct {
    int type; Window event; Window window;
    int x, y, width, height, border_width;
    Window above; Bool override_redirect;
} XConfigureEvent;

typedef struct {
    int type; Window parent; Window window;
    int x, y, width, height, border_width;
    unsigned long above; int detail; unsigned long value_mask;
} XConfigureRequestEvent;

typedef struct {
    int type; unsigned long serial; Bool send_event; Display *display;
    Drawable drawable;
    int x, y, width, height, count;
} XExposeEvent;

typedef struct {
    int type; unsigned long serial; Bool send_event; Display *display;
    Window window; int x, y; unsigned int state, button; Bool same_screen;
} XButtonEvent;

typedef struct {
    int type; unsigned long serial; Bool send_event; Display *display;
    Window window; int x, y; unsigned int state, keycode; Bool same_screen;
} XKeyEvent;

typedef struct {
    int type; unsigned long serial; Bool send_event; Display *display;
    Window window; int x, y; unsigned int state; Bool is_hint, same_screen;
} XMotionEvent;

typedef struct {
    int type; Window window; int state;
} XFocusChangeEvent;

typedef struct {
    int type; Window window; Atom atom; Time time; int state;
} XPropertyEvent;

typedef struct {
    int type; Atom selection; Time time; Window requestor;
    Atom target, property;
} XSelectionEvent;

typedef struct {
    int type; Atom message_type; int format;
    union { char b[20]; short s[10]; long l[5]; } data;
} XClientMessageEvent;

typedef union _XEvent {
    int type;
    XAnyEvent xany;
    XKeyEvent xkey;
    XButtonEvent xbutton;
    XMotionEvent xmotion;
    XFocusChangeEvent xfocus;
    XExposeEvent xexpose;
    XCreateWindowEvent xcreatewindow;
    XMapEvent xmap;
    XUnmapEvent xunmap;
    XDestroyEvent xdestroy;
    XConfigureRequestEvent xconfigurerequest;
    XConfigureEvent xconfigure;
    XPropertyEvent xproperty;
    XSelectionEvent xselection;
    XClientMessageEvent xclient;
    char pad[96];
} XEvent;

/* ── XSizeHints / XWMHints / XClassHint / XTextProperty ── */
typedef struct {
    long flags; int x, y, width, height;
    int min_width, min_height, max_width, max_height;
    int width_inc, height_inc;
    int min_aspect_num, min_aspect_den, max_aspect_num, max_aspect_den;
    int base_width, base_height, win_gravity;
} XSizeHints;

typedef struct {
    long flags; Bool input; int initial_state;
    Pixmap icon_pixmap; Window icon_window;
    int icon_x, icon_y; Pixmap icon_mask; unsigned long window_group;
} XWMHints;

typedef struct { char *res_name, *res_class; } XClassHint;

typedef struct {
    unsigned char *value; Atom encoding; int format; unsigned long nitems;
} XTextProperty;

typedef struct { int family; int numberOfBytes; char *address; } XHostAddress;

#define InputHint (1L<<0)
#define StateHint (1L<<1)
#define AllHints  (InputHint|StateHint)

/* ── Builtin atoms ── */
extern Atom XA_PRIMARY, XA_SECONDARY, XA_ATOM, XA_CARDINAL, XA_STRING;
extern Atom XA_WM_NAME, XA_WM_CLASS, XA_WM_HINTS, XA_WM_NORMAL_HINTS;
extern Atom XA_WM_DELETE_WINDOW, XA_WM_PROTOCOLS, XA_CLIPBOARD, XA_UTF8_STRING;

/* ── Display structure (in-kernel client handle) ── */
struct _XDisplay {
    int xfd;
    int client_id;
    unsigned long sequence;
    Window root_window;
    int screen_width, screen_height;
    Colormap default_cmap;
    int default_depth;
    Visual *default_visual;
    Screen *screens;
    int nscreens;
    char *display_name, *vendor;
    int proto_major_version, proto_minor_version;
    Atom next_atom;
    uint8_t req_buf[4096];
    uint8_t rep_buf[4096];
};

/* ── screen/display macros ── */
#define DefaultScreen(dpy)       0
#define DefaultRootWindow(dpy)   ((dpy)->root_window)
#define DisplayWidth(dpy,n)      ((dpy)->screen_width)
#define DisplayHeight(dpy,n)     ((dpy)->screen_height)
#define DisplayPlanes(dpy,n)     32
#define DefaultColormap(dpy,n)   ((dpy)->default_cmap)
#define BlackPixel(dpy,n)        0xFF000000UL
#define WhitePixel(dpy,n)        0xFFFFFFFFUL
#define ScreenCount(dpy)         1
#define ConnectionNumber(dpy)    ((dpy)->xfd)

/* ═══════════════════════════════════════════════════════════════════
 * Core Xlib API
 * ═══════════════════════════════════════════════════════════════════ */

/* Display */
Display *XOpenDisplay(const char *display_name);
int      XCloseDisplay(Display *dpy);

/* Windows */
Window   XCreateSimpleWindow(Display *dpy, Window parent,
         int x, int y, unsigned w, unsigned h, unsigned bw,
         unsigned long border, unsigned long bg);
Window   XCreateWindow(Display *dpy, Window parent,
         int x, int y, unsigned w, unsigned h, unsigned bw,
         int depth, unsigned klass, Visual *vis, unsigned long vmask, void *attr);
int      XDestroyWindow(Display *dpy, Window w);
int      XDestroySubwindows(Display *dpy, Window w);
int      XMapWindow(Display *dpy, Window w);
int      XMapSubwindows(Display *dpy, Window w);
int      XUnmapWindow(Display *dpy, Window w);
int      XUnmapSubwindows(Display *dpy, Window w);
int      XMapRaised(Display *dpy, Window w);
int      XRaiseWindow(Display *dpy, Window w);
int      XLowerWindow(Display *dpy, Window w);
int      XRestackWindows(Display *dpy, Window *wins, int n);
int      XMoveWindow(Display *dpy, Window w, int x, int y);
int      XResizeWindow(Display *dpy, Window w, unsigned w2, unsigned h2);
int      XMoveResizeWindow(Display *dpy, Window w, int x, int y, unsigned w2, unsigned h2);
int      XConfigureWindow(Display *dpy, Window w, unsigned vm, void *vals);
int      XReparentWindow(Display *dpy, Window w, Window parent, int x, int y);
int      XWithdrawWindow(Display *dpy, Window w);
int      XIconifyWindow(Display *dpy, Window w);

/* Queries */
Status   XGetWindowAttributes(Display *dpy, Window w, XWindowAttributes *a);
Status   XGetGeometry(Display *dpy, Drawable d, Window *root,
         int *x, int *y, unsigned *w, unsigned *h, unsigned *brd, unsigned *depth);
Status   XGetWindowProperty(Display *dpy, Window w, Atom prop,
         long off, long len, Bool del, Atom rtype, Atom *atype,
         int *afmt, unsigned long *nitems, unsigned long *bafter,
         unsigned char **data);
Status   XQueryTree(Display *dpy, Window w, Window *root, Window *parent,
         Window **children, unsigned *nchildren);

/* Properties */
int      XChangeProperty(Display *dpy, Window w, Atom prop, Atom type,
         int fmt, int mode, const unsigned char *data, int nelem);
int      XDeleteProperty(Display *dpy, Window w, Atom prop);
int      XRotateWindowProperties(Display *dpy, Window w, Atom *props,
         int nprop, int delta);

/* Atoms */
Atom     XInternAtom(Display *dpy, const char *name, Bool only);
Status   XGetAtomName(Display *dpy, Atom a, char *buf, int len);

/* Events */
int      XNextEvent(Display *dpy, XEvent *ev);
int      XPending(Display *dpy);
int      XPeekEvent(Display *dpy, XEvent *ev);
int      XSendEvent(Display *dpy, Window w, Bool prop, long mask, XEvent *ev);
int      XSelectInput(Display *dpy, Window w, long mask);
int      XSync(Display *dpy, Bool disc);
int      XFlush(Display *dpy);
Bool     XEventsQueued(Display *dpy, int mode);

/* Input */
int      XGrabPointer(Display *dpy, Window w, Bool oe, unsigned em,
         int pm, int km, Window confine, Cursor cur, Time t);
int      XUngrabPointer(Display *dpy, Time t);
int      XGrabKeyboard(Display *dpy, Window w, Bool oe, int pm, int km, Time t);
int      XUngrabKeyboard(Display *dpy, Time t);
int      XWarpPointer(Display *dpy, Window sw, Window dw,
         int sx, int sy, unsigned sw2, unsigned sh, int dx, int dy);
int      XQueryPointer(Display *dpy, Window w, Window *root, Window *child,
         int *rx, int *ry, int *wx, int *wy, unsigned *mask);
unsigned XKeysymToKeycode(Display *dpy, KeySym ks);
KeySym   XStringToKeysym(const char *s);
char *   XKeysymToString(KeySym ks);
int      XLookupKeysym(void *km, int idx);

/* GC */
GC       XCreateGC(Display *dpy, Drawable d, unsigned long vm, void *vals);
int      XFreeGC(Display *dpy, GC gc);
int      XChangeGC(Display *dpy, GC gc, unsigned long vm, void *vals);
int      XSetForeground(Display *dpy, GC gc, unsigned long fg);
int      XSetBackground(Display *dpy, GC gc, unsigned long bg);
int      XSetLineAttributes(Display *dpy, GC gc, unsigned lw, int ls, int cs, int js);
int      XSetFillStyle(Display *dpy, GC gc, int fs);
int      XSetFillRule(Display *dpy, GC gc, int fr);
int      XSetClipMask(Display *dpy, GC gc, Pixmap pm);
int      XSetClipRectangles(Display *dpy, GC gc, int cx, int cy,
         XRectangle *rects, int n);
int      XSetDashes(Display *dpy, GC gc, int off, const char *dl, int n);
int      XSetTile(Display *dpy, GC gc, Pixmap tile);
int      XSetStipple(Display *dpy, GC gc, Pixmap stipple);
int      XSetTSOrigin(Display *dpy, GC gc, int x, int y);
int      XSetSubwindowMode(Display *dpy, GC gc, int mode);
int      XGetGCValues(Display *dpy, GC gc, unsigned long vm, XGCValues *v);

/* Drawing */
int      XCopyArea(Display *dpy, Drawable src, Drawable dst, GC gc,
         int sx, int sy, unsigned w, unsigned h, int dx, int dy);
int      XCopyPlane(Display *dpy, Drawable src, Drawable dst, GC gc,
         int sx, int sy, unsigned w, unsigned h, int dx, int dy, unsigned long plane);
int      XFillRectangle(Display *dpy, Drawable d, GC gc,
         int x, int y, unsigned w, unsigned h);
int      XFillRectangles(Display *dpy, Drawable d, GC gc,
         XRectangle *rects, int n);
int      XFillPolygon(Display *dpy, Drawable d, GC gc,
         XPoint *pts, int n, int shape, int mode);
int      XFillArc(Display *dpy, Drawable d, GC gc,
         int x, int y, unsigned w, unsigned h, int a1, int a2);
int      XFillArcs(Display *dpy, Drawable d, GC gc, XArc *arcs, int n);
int      XDrawPoint(Display *dpy, Drawable d, GC gc, int x, int y);
int      XDrawPoints(Display *dpy, Drawable d, GC gc, XPoint *pts, int n, int mode);
int      XDrawLine(Display *dpy, Drawable d, GC gc, int x1, int y1, int x2, int y2);
int      XDrawLines(Display *dpy, Drawable d, GC gc, XPoint *pts, int n, int mode);
int      XDrawRectangle(Display *dpy, Drawable d, GC gc,
         int x, int y, unsigned w, unsigned h);
int      XDrawRectangles(Display *dpy, Drawable d, GC gc, XRectangle *r, int n);
int      XDrawArc(Display *dpy, Drawable d, GC gc,
         int x, int y, unsigned w, unsigned h, int a1, int a2);
int      XDrawArcs(Display *dpy, Drawable d, GC gc, XArc *arcs, int n);
int      XDrawString(Display *dpy, Drawable d, GC gc,
         int x, int y, const char *s, int len);
int      XDrawString16(Display *dpy, Drawable d, GC gc,
         int x, int y, const char *s, int len);
int      XDrawImageString(Display *dpy, Drawable d, GC gc,
         int x, int y, const char *s, int len);
int      XDrawImageString16(Display *dpy, Drawable d, GC gc,
         int x, int y, const char *s, int len);
int      XClearArea(Display *dpy, Window w, int x, int y,
         unsigned w2, unsigned h, Bool exp);
int      XClearWindow(Display *dpy, Window w);

/* Pixmap / Image */
Pixmap   XCreatePixmap(Display *dpy, Drawable d, unsigned w, unsigned h, unsigned depth);
int      XFreePixmap(Display *dpy, Pixmap pm);
XImage * XCreateImage(Display *dpy, Visual *vis, unsigned depth,
         int fmt, int off, char *data, unsigned w, unsigned h,
         int bpad, int bpl);
XImage * XGetImage(Display *dpy, Drawable d, int x, int y,
         unsigned w, unsigned h, unsigned long pm, int fmt);
XImage * XSubImage(XImage *img, int x, int y, unsigned w, unsigned h);
int      XPutImage(Display *dpy, Drawable d, GC gc, XImage *img,
         int sx, int sy, int dx, int dy, unsigned w, unsigned h);
int      XDestroyImage(XImage *img);
unsigned long XGetPixel(XImage *img, int x, int y);
int      XPutPixel(XImage *img, int x, int y, unsigned long px);

/* Font */
XFontStruct *XLoadQueryFont(Display *dpy, const char *name);
int      XFreeFont(Display *dpy, XFontStruct *f);
int      XSetFont(Display *dpy, GC gc, unsigned long font);
int      XTextWidth(XFontStruct *f, const char *s, int n);
int      XTextExtents(XFontStruct *f, const char *s, int n,
         int *dir, int *asc, int *desc, void *ov);
char **  XListFonts(Display *dpy, const char *pat, int max, int *cnt);
int      XFreeFontNames(char **list);

/* Color */
int      XAllocColor(Display *dpy, Colormap cm, XColor *c);
int      XAllocNamedColor(Display *dpy, Colormap cm, const char *name,
         XColor *sc, XColor *ec);
int      XFreeColors(Display *dpy, Colormap cm, unsigned long *px, int n, unsigned long pl);
Colormap XCreateColormap(Display *dpy, Window w, Visual *v, int a);
int      XFreeColormap(Display *dpy, Colormap cm);
int      XCopyColormapAndFree(Display *dpy, Colormap cm);
Colormap XDefaultColormap(Display *dpy, int scr);
Visual * XDefaultVisual(Display *dpy, int scr);
int      XMatchVisualInfo(Display *dpy, int scr, int d, int cls, XVisualInfo *vi);
int      XParseColor(Display *dpy, Colormap cm, const char *spec, XColor *c);
int      XStoreColors(Display *dpy, Colormap cm, XColor *c, int n);
int      XStoreNamedColor(Display *dpy, Colormap cm, const char *name, unsigned long px, int fl);
int      XQueryColor(Display *dpy, Colormap cm, XColor *c);
int      XQueryColors(Display *dpy, Colormap cm, XColor *cs, int n);
int      XLookupColor(Display *dpy, Colormap cm, const char *spec, XColor *ec, XColor *sc);
int      XInstallColormap(Display *dpy, Colormap cm);
int      XUninstallColormap(Display *dpy, Colormap cm);

/* Selection */
int      XSetSelectionOwner(Display *dpy, Atom sel, Window w, Time t);
Window   XGetSelectionOwner(Display *dpy, Atom sel);
int      XConvertSelection(Display *dpy, Atom sel, Atom tgt, Atom prop, Window w, Time t);

/* Cursor */
Cursor   XCreateFontCursor(Display *dpy, unsigned shape);
Cursor   XCreatePixmapCursor(Display *dpy, Pixmap src, Pixmap msk,
         XColor *fg, XColor *bg, unsigned x, unsigned y);
Cursor   XCreateGlyphCursor(Display *dpy, void *sf, void *mf,
         unsigned sc, unsigned mc, XColor *fg, XColor *bg);
int      XFreeCursor(Display *dpy, Cursor c);
int      XRecolorCursor(Display *dpy, Cursor c, XColor *fg, XColor *bg);

/* Window properties */
int      XSetWindowBackground(Display *dpy, Window w, unsigned long px);
int      XSetWindowBorderWidth(Display *dpy, Window w, unsigned bw);
int      XSetWindowBorder(Display *dpy, Window w, unsigned long px);
int      XSetWMName(Display *dpy, Window w, XTextProperty *tp);
int      XSetWMProtocols(Display *dpy, Window w, Atom *protos, int n);
int      XGetWMProtocols(Display *dpy, Window w, Atom **protos, int *n);
int      XGetWMNormalHints(Display *dpy, Window w, XSizeHints *h, long *sup);
int      XSetWMNormalHints(Display *dpy, Window w, XSizeHints *h);
int      XSetWMHints(Display *dpy, Window w, XWMHints *h);
int      XGetWMHints(Display *dpy, Window w, XWMHints *h);
int      XSetClassHint(Display *dpy, Window w, XClassHint *h);
int      XGetClassHint(Display *dpy, Window w, XClassHint *h);
int      XSetCommand(Display *dpy, Window w, char **argv, int argc);
int      XGetCommand(Display *dpy, Window w, char ***argv, int *argc);
int      XGetIconName(Display *dpy, Window w, char **name);
int      XSetIconName(Display *dpy, Window w, const char *name);
Bool     XStringListToTextProperty(char **list, int cnt, XTextProperty *tp);
Bool     XTextPropertyToStringList(XTextProperty *tp, char ***list, int *cnt);
int      XFreeStringList(char **list);

/* Misc */
int      XFree(void *data);
int      XKillClient(Display *dpy, Window w);
int      XSetCloseDownMode(Display *dpy, int mode);
int      XNoOp(Display *dpy);
int      XGrabServer(Display *dpy);
int      XUngrabServer(Display *dpy);
int      XFlushGC(Display *dpy, GC gc);
int      XChangeSaveSet(Display *dpy, Window w, int mode);
int      XAddToSaveSet(Display *dpy, Window w);
int      XRemoveFromSaveSet(Display *dpy, Window w);
int      XSetErrorHandler(void *handler);
int      XSetIOErrorHandler(void *handler);
Status   XGetErrorText(Display *dpy, int code, char *buf, int len);

/* Extension stubs */
Bool     XQueryExtension(Display *dpy, const char *name,
         int *major, int *first_event, int *first_error);
char **  XListExtensions(Display *dpy, int *n);
int      XFreeExtensionList(char **list);

/* XShm stubs */
Status   XShmQueryExtension(Display *dpy);
Status   XShmPixmapFormat(Display *dpy);
Status   XShmPutImage(Display *dpy, Drawable d, GC gc, XImage *img,
         int sx, int sy, int dx, int dy, unsigned sw, unsigned sh, Bool se);

/* XShape stubs */
int      XShapeQueryExtension(Display *dpy, int *ev, int *err);
void     XShapeCombineMask(Display *dpy, Window d, int dk, int x, int y, Pixmap m, int op);
void     XShapeCombineRegion(Display *dpy, Window d, int dk, int x, int y, void *r, int op);

/* XRandR stubs */
Status   XRRQueryExtension(Display *dpy, int *ev, int *err);
void *   XRRGetScreenResources(Display *dpy, Window w);
void     XRRFreeScreenResources(void *res);
int      XRRGetOutputPrimary(Display *dpy, Window w);

/* XComposite / XDamage / XFixes stubs */
Status   XCompositeQueryExtension(Display *dpy, int *ev, int *err);
void     XCompositeRedirectSubwindows(Display *dpy, Window w, int u);
int      XDamageQueryExtension(Display *dpy, int *ev, int *err);
void *   XDamageCreate(Display *dpy, Drawable d, int l);
void     XDamageDestroy(Display *dpy, void *dm);
void     XDamageSubtract(Display *dpy, void *dm, void *r, void *p);
Status   XFixesQueryExtension(Display *dpy, int *ev, int *err);
void     XFixesSelectSelectionInput(Display *dpy, Window w, Atom s, unsigned long m);
void     XFixesSelectCursorInput(Display *dpy, Window w, unsigned long m);
Cursor   XFixesCreateCursor(Display *dpy, Cursor src, int x, int y);
void     XFixesSetCursorName(Display *dpy, Cursor c, const char *n);
const char *XFixesGetCursorName(Display *dpy, Cursor c, Atom *a);

/* XRender stubs */
void *   XRenderCreatePicture(Display *dpy, Pixmap pm, void *fmt, unsigned long vm, void *v);
void     XRenderFreePicture(Display *dpy, void *p);
void     XRenderComposite(Display *dpy, int op, void *src, void *mask, void *dst,
         int sx, int sy, int mx, int my, int dx, int dy, unsigned w, unsigned h);
void *   XRenderFindVisualFormat(Display *dpy, Visual *v);

/* XInput2 stubs */
int      XISelectEvents(Display *dpy, Window w, void *m, int l);

/* XKB stubs */
int      XkbGetKeyboard(Display *dpy, unsigned w, unsigned ds);
int      XkbGetControls(Display *dpy, unsigned long w, void *s);
int      XkbSetControls(Display *dpy, unsigned long w, void *s);
int      XkbLatchLockState(Display *dpy, unsigned ds, unsigned aff, unsigned val,
         unsigned sl, unsigned ll, unsigned ul);

/* Xcursor stubs */
Cursor   XcursorImageLoadCursor(Display *dpy, void *img);

/* Region stubs (opaque) */
void *   XCreateRegion(void);
int      XDestroyRegion(void *r);
int      XUnionRegion(void *s1, void *s2, void *d);
int      XIntersectRegion(void *s1, void *s2, void *d);
int      XSubtractRegion(void *s1, void *s2, void *d);
int      XEmptyRegion(void *r);
int      XOffsetRegion(void *r, int dx, int dy);
int      XPointInRegion(void *r, int x, int y);

/* Text property */
int      XmbTextPropertyToTextList(Display *dpy, XTextProperty *tp,
         char ***list, int *cnt);
int      XFreeFontPath(char **list);

/* extra */
int      XSetWMClientMachine(Display *dpy, Window w, XTextProperty *tp);
int      XGetWMClientMachine(Display *dpy, Window w, XTextProperty *tp);
int      XSetWMIconName(Display *dpy, Window w, XTextProperty *tp);
int      XGetWMIconName(Display *dpy, Window w, XTextProperty *tp);
int      XGetWMName(Display *dpy, Window w, XTextProperty *tp);
int      XSetWMCommand(Display *dpy, Window w, XTextProperty *tp);
int      XGetWMCommand(Display *dpy, Window w, XTextProperty *tp);
int      XSetTextProperty(Display *dpy, Window w, XTextProperty *tp, Atom prop);
Status   XGetTextProperty(Display *dpy, Window w, XTextProperty *tp, Atom prop);
int      XAllocClassHint(Display *dpy, Window w, XClassHint **h);
int      XAllocSizeHints(Display *dpy, Window w, XSizeHints **h);
int      XAllocWMHints(Display *dpy, Window w, XWMHints **h);
int      XSetStandardProperties(Display *dpy, Window w, const char *wn,
         const char *in, Pixmap ip, char **argv, int argc, XSizeHints *sh);
int      XGetTransientForHint(Display *dpy, Window w, Window *pw);
int      XSetTransientForHint(Display *dpy, Window w, Window pw);
int      XGetIconSizes(Display *dpy, Window w, int **sl, int *cnt);
int      XSetIconSizes(Display *dpy, Window w, int *wl, int *hl, int cnt);
int      XSetWMProperties(Display *dpy, Window w, XTextProperty *wn,
         XTextProperty *in, char **argv, int argc, XSizeHints *sh,
         XWMHints *wmh, XClassHint *ch);
int      XGetStandardColormap(Display *dpy, Window w, void *cm, Atom prop);
int      XAddHost(Display *dpy, XHostAddress *h);
int      XAddHosts(Display *dpy, XHostAddress *h, int n);
int      XRemoveHost(Display *dpy, XHostAddress *h);
int      XRemoveHosts(Display *dpy, XHostAddress *h, int n);
int      XListHosts(Display *dpy, Bool *s, XHostAddress **h);
int      XSetAccessControl(Display *dpy, int m);
int      XChangePointerControl(Display *dpy, Bool a, Bool t, int an, int ad, int tv);
int      XGetPointerControl(Display *dpy, int *an, int *ad, int *t);
int      XSetScreenSaver(Display *dpy, int to, int iv, int pb, int ae);
int      XGetScreenSaver(Display *dpy, int *to, int *iv, int *pb, int *ae);
int      XForceScreenSaver(Display *dpy, int m);
int      XSetWMZoomHints(Display *dpy, Window w, XSizeHints *h);
int      XSetZoomHints(Display *dpy, Window w, XSizeHints *h);
int      XPermStoreColors(Display *dpy, Colormap cm, void *c, int n);
int      XPermAllocAllColors(Display *dpy, Colormap cm, void *c);
int      XAllocColorCells(Display *dpy, Colormap cm, Bool cont,
         unsigned long *pm, unsigned np, unsigned long *px, unsigned npx);
int      XAllocColorPlanes(Display *dpy, Colormap cm, Bool cont,
         unsigned long *px, int nc, int nr, int ng, int nb,
         unsigned long *rm, unsigned long *gm, unsigned long *bm);

#ifdef __cplusplus
}
#endif
#endif

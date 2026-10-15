/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * GTK6 GDK-on-Qt6 backend — the Phase 2 replacement for the X11/Wayland/
 * Broadway/Win32/macOS surfaces.  Display and toplevels are Qt objects
 * (QGuiApplication, QRasterWindow); input, cursors and clipboard are Qt's,
 * too, so GDK here is a thin translation layer rather than a
 * window-system implementation.
 *
 * The surface paints through the GSK renderer, so the paint callback is what
 * the toplevel caller installs to draw a frame.  No glib.h: see qttext.h.
 */

#ifndef GTK6_GDK_QT_H
#define GTK6_GDK_QT_H

#ifdef __cplusplus
extern "C" {
#endif

typedef struct _GdkQtDisplay GdkQtDisplay;
typedef struct _GdkQtSurface GdkQtSurface;

/* user_data → (QPainter *painter, width, height) in device pixels. */
typedef void (*GdkQtPaintFunc) (void   *user_data,
                                void   *painter,
                                double  width,
                                double  height);

/* Idempotent: bring up the QGuiApplication and the display singleton.  argc/
 * argv may be NULL; Qt6 only inspects the program name here. */
int           gdk_qt_init (int *argc, char ***argv);
GdkQtDisplay *gdk_qt_display_get_default (void);

GdkQtSurface *gdk_qt_surface_new_toplevel (const char *title,
                                           int         width,
                                           int         height);
void gdk_qt_surface_set_paint_func (GdkQtSurface   *surface,
                                    GdkQtPaintFunc  func,
                                    void           *user_data);

void gdk_qt_surface_get_size  (GdkQtSurface *surface, int *width, int *height);
void gdk_qt_surface_resize    (GdkQtSurface *surface, int  width, int  height);
void gdk_qt_surface_set_title (GdkQtSurface *surface, const char *title);
void gdk_qt_surface_show      (GdkQtSurface *surface);

/* Ask the platform to re-emit the paint callback (a "frame clock" tick). */
void gdk_qt_surface_present   (GdkQtSurface *surface);
void gdk_qt_surface_destroy   (GdkQtSurface *surface);

/* Drain queued input/update events. */
void gdk_qt_display_flush (void);

/* Close every toplevel and tear the QGuiApplication down. */
void gdk_qt_display_finish (void);

#ifdef __cplusplus
}
#endif

#endif /* GTK6_GDK_QT_H */

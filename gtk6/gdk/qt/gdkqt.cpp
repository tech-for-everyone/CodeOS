/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * GTK6 GDK-on-Qt6 backend — see gdkqt.h.
 *
 * A toplevel is a QRasterWindow.  Qt paints it through the raster engine with
 * no GPU involved, which matches CodeOS's framebuffer: QtFbSupport owns the
 * scanout, QtGui gives us the window, and the window's paintEvent is where the
 * GSK renderer runs.
 */

#include "gdkqt.h"

#include <QtCore/QCoreApplication>
#include <QtGui/QGuiApplication>
#include <QtGui/QPainter>
#include <QtGui/QRasterWindow>
#include <QtGui/QPaintEvent>

/* ── toplevel ──────────────────────────────────────────────────────────── */

struct _GdkQtSurface : public QRasterWindow
{
  GdkQtPaintFunc paint      = nullptr;
  void          *paint_data = nullptr;

protected:
  void paintEvent (QPaintEvent *event) override;
};

void
_GdkQtSurface::paintEvent (QPaintEvent *event)
{
  Q_UNUSED (event);

  if (!paint)
    return;

  QPainter painter (this);
  paint (paint_data, &painter, (double) width (), (double) height ());
}

/* ── display ───────────────────────────────────────────────────────────── */

struct _GdkQtDisplay
{
  QGuiApplication *app = nullptr;
};

static _GdkQtDisplay gdk_qt_display;
static bool         gdk_qt_initialized = false;

extern "C" int
gdk_qt_init (int *argc, char ***argv)
{
  if (gdk_qt_initialized)
    return 1;

  if (!QGuiApplication::instance ())
    {
      static int   fallback_argc = 1;
      static char *fallback_argv[] = { (char *) "codeos", nullptr };

      int   *a = argc ? argc : &fallback_argc;
      char **v = argv ? *argv : fallback_argv;

      gdk_qt_display.app = new QGuiApplication (*a, v);
    }

  gdk_qt_initialized = true;
  return 1;
}

extern "C" _GdkQtDisplay *
gdk_qt_display_get_default (void)
{
  return &gdk_qt_display;
}

extern "C" _GdkQtSurface *
gdk_qt_surface_new_toplevel (const char *title,
                             int         width,
                             int         height)
{
  _GdkQtSurface *surface = new _GdkQtSurface ();

  surface->setTitle (QString::fromUtf8 (title ? title : ""));
  surface->resize (width > 0 ? width : 1, height > 0 ? height : 1);

  return surface;
}

extern "C" void
gdk_qt_surface_set_paint_func (_GdkQtSurface  *surface,
                               GdkQtPaintFunc  func,
                               void           *user_data)
{
  if (!surface)
    return;

  surface->paint      = func;
  surface->paint_data = user_data;
}

extern "C" void
gdk_qt_surface_get_size (_GdkQtSurface *surface,
                         int           *width,
                         int           *height)
{
  if (!surface)
    return;

  if (width)
    *width = surface->width ();
  if (height)
    *height = surface->height ();
}

extern "C" void
gdk_qt_surface_resize (_GdkQtSurface *surface,
                       int            width,
                       int            height)
{
  if (surface)
    surface->resize (width, height);
}

extern "C" void
gdk_qt_surface_set_title (_GdkQtSurface *surface,
                          const char    *title)
{
  if (surface)
    surface->setTitle (QString::fromUtf8 (title ? title : ""));
}

extern "C" void
gdk_qt_surface_show (_GdkQtSurface *surface)
{
  if (surface)
    surface->show ();
}

extern "C" void
gdk_qt_surface_present (_GdkQtSurface *surface)
{
  if (surface)
    surface->requestUpdate ();
}

extern "C" void
gdk_qt_surface_destroy (_GdkQtSurface *surface)
{
  delete surface;
}

/* ── event pump ────────────────────────────────────────────────────────── */

extern "C" void
gdk_qt_display_flush (void)
{
  QCoreApplication::processEvents ();
}

extern "C" void
gdk_qt_display_finish (void)
{
  QCoreApplication::quit ();
}

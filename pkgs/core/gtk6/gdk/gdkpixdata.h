/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * gdkpixdata.h: the deprecated gdk-pixbuf on-disk "pixdata" format.
 *
 * GTK6 owns this the same way it owns gdkpixbuf.h (see there).  The only
 * user in the imported tree is gtk/gtkiconcachevalidator.c, which reads the
 * header of an embedded icon-cache image and calls gdk_pixdata_deserialize();
 * the structure must be complete because it is used by value.  The rest of
 * the format, and the serialising half, are declared for completeness.
 */

#ifndef GTK6_GDK_PIXDATA_H
#define GTK6_GDK_PIXDATA_H

#include <glib.h>

#include <gdk/gdkpixbuf.h>

G_BEGIN_DECLS

typedef struct _GdkPixdata GdkPixdata;

struct _GdkPixdata
{
  guint32 magic;        /* GDK_PIXBUF_MAGIC_NUMBER */
  gint32  length;       /* <1 to disable length checks, otherwise:
                         * GDK_PIXDATA_HEADER_LENGTH + pixel_data length
                         */
  guint32 pixdata_type; /* GdkPixdataType */
  guint32 rowstride;
  guint32 width;
  guint32 height;
  guint8 *pixel_data;
};

#define GDK_PIXDATA_HEADER_LENGTH       (4 + 4 + 4 + 4 + 4 + 4)

guint8 *   gdk_pixdata_serialize   (const GdkPixdata *pixdata,
                                    guint            *stream_length_p);
gboolean   gdk_pixdata_deserialize (GdkPixdata       *pixdata,
                                    guint             stream_length,
                                    const guint8     *stream,
                                    GError          **error);
gpointer   gdk_pixdata_from_pixbuf (GdkPixdata       *pixdata,
                                    const GdkPixbuf  *pixbuf,
                                    gboolean          use_rle);
GdkPixbuf *gdk_pixbuf_from_pixdata (const GdkPixdata *pixdata,
                                    gboolean          copy_pixels,
                                    GError          **error);

G_END_DECLS

#endif /* GTK6_GDK_PIXDATA_H */

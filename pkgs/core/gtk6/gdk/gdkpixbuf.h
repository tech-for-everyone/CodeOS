/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * gdkpixbuf.h: GDK's image-buffer API.
 *
 * GTK6 does not use the gdk-pixbuf library.  This header is the part of its
 * API the GTK4 sources actually name, restated as GDK's own so the port is a
 * mechanical include swap rather than a rewrite:
 *
 *     <gdk-pixbuf/gdk-pixbuf.h> -> <gdk/gdkpixbuf.h>
 *     <gdk/deprecated/gdkpixbuf.h> -> <gdk/gdkpixbuf.h>
 *
 * Unlike cairo and the text library, gdk-pixbuf is already spelled in the Gdk
 * namespace (`GdkPixbuf`, `GDK_TYPE_PIXBUF`, `GDK_COLORSPACE_*`), so nothing is
 * renamed: GDK simply owns the names now.  The implementations are Qt-backed
 * (the image is a QImage); they are wired in with the rest of the gdk/qt
 * backend, so this header only has to state the surface the sources name.
 *
 * Only the pieces the imported tree actually uses are declared here; the rest
 * of gdk-pixbuf (animations, loaders, the GdkPixbufSimpleAnim helpers, the
 * pixel-format writers) was never reached and is deliberately absent.
 */

#ifndef GTK6_GDK_PIXBUF_H
#define GTK6_GDK_PIXBUF_H

#include <glib.h>
#include <glib-object.h>
#include <gio/gio.h>

G_BEGIN_DECLS

/* GdkTexture is defined by gdktexture.h, which includes this header; the
 * redundant typedef is legal C11 (same type) and keeps gdkpixbuf.h
 * self-contained. */
typedef struct _GdkTexture GdkTexture;

typedef struct _GdkPixbuf       GdkPixbuf;
typedef struct _GdkPixbufFormat GdkPixbufFormat;

#define GDK_TYPE_PIXBUF              (gdk_pixbuf_get_type ())
#define GDK_PIXBUF(object)           (G_TYPE_CHECK_INSTANCE_CAST ((object), GDK_TYPE_PIXBUF, GdkPixbuf))
#define GDK_IS_PIXBUF(object)        (G_TYPE_CHECK_INSTANCE_TYPE ((object), GDK_TYPE_PIXBUF))

/* The header of the deprecated on-disk pixdata format.  gtkimage.c used to
 * define this itself behind #ifndef when gdk-pixbuf was not around. */
#define GDK_PIXBUF_MAGIC_NUMBER (0x47646b50)    /* 'GdkP' */

typedef enum
{
  GDK_COLORSPACE_RGB
} GdkColorspace;

typedef enum
{
  GDK_INTERP_NEAREST,
  GDK_INTERP_TILES,
  GDK_INTERP_BILINEAR,
  GDK_INTERP_HYPER
} GdkInterpType;

typedef void (* GdkPixbufDestroyNotify) (guchar *pixels, gpointer data);

GType gdk_pixbuf_get_type (void) G_GNUC_CONST;

/* Construction */
GdkPixbuf *gdk_pixbuf_new               (GdkColorspace          colorspace,
                                         gboolean               has_alpha,
                                         int                    bits_per_sample,
                                         int                    width,
                                         int                    height);
GdkPixbuf *gdk_pixbuf_new_from_file     (const char            *filename,
                                         GError               **error);
GdkPixbuf *gdk_pixbuf_new_from_file_at_size
                                        (const char            *filename,
                                         int                    width,
                                         int                    height,
                                         GError               **error);
GdkPixbuf *gdk_pixbuf_new_from_resource (const char            *resource_path,
                                         GError               **error);
GdkPixbuf *gdk_pixbuf_new_from_data     (const guchar          *data,
                                         GdkColorspace          colorspace,
                                         gboolean               has_alpha,
                                         int                    bits_per_sample,
                                         int                    width,
                                         int                    height,
                                         int                    rowstride,
                                         GdkPixbufDestroyNotify destroy_fn,
                                         gpointer               destroy_fn_data);
GdkPixbuf *gdk_pixbuf_new_from_stream   (GInputStream          *stream,
                                         GCancellable          *cancellable,
                                         GError               **error);
void       gdk_pixbuf_new_from_stream_async
                                        (GInputStream          *stream,
                                         GCancellable          *cancellable,
                                         GAsyncReadyCallback    callback,
                                         gpointer               user_data);
GdkPixbuf *gdk_pixbuf_new_from_stream_finish
                                        (GAsyncResult          *async_result,
                                         GError               **error);

/* Interop with GTK6's own texture type. */
GdkPixbuf *gdk_pixbuf_get_from_texture  (GdkTexture            *texture);

/* Information */
int            gdk_pixbuf_get_width      (const GdkPixbuf *pixbuf);
int            gdk_pixbuf_get_height     (const GdkPixbuf *pixbuf);
int            gdk_pixbuf_get_rowstride  (const GdkPixbuf *pixbuf);
int            gdk_pixbuf_get_n_channels (const GdkPixbuf *pixbuf);
gboolean       gdk_pixbuf_get_has_alpha  (const GdkPixbuf *pixbuf);
const guint8 * gdk_pixbuf_read_pixels    (const GdkPixbuf *pixbuf);

/* Modification */
GdkPixbuf *gdk_pixbuf_scale_simple       (const GdkPixbuf *src,
                                          int              dest_width,
                                          int              dest_height,
                                          GdkInterpType    interp_type);
void       gdk_pixbuf_fill               (GdkPixbuf       *pixbuf,
                                          guint32          pixel);
void       gdk_pixbuf_copy_area          (const GdkPixbuf *src_pixbuf,
                                          int              src_x,
                                          int              src_y,
                                          int              width,
                                          int              height,
                                          GdkPixbuf       *dest_pixbuf,
                                          int              dest_x,
                                          int              dest_y);

/* Saving */
void       gdk_pixbuf_save_to_stream_async
                                        (GdkPixbuf           *pixbuf,
                                         GOutputStream       *stream,
                                         const gchar         *type,
                                         GCancellable        *cancellable,
                                         GAsyncReadyCallback  callback,
                                         gpointer             user_data,
                                         ...) G_GNUC_NULL_TERMINATED;
gboolean   gdk_pixbuf_save_to_stream_finish
                                        (GAsyncResult        *async_result,
                                         GError             **error);

/* Formats (used by the file filter and the content type handlers) */
GSList *gdk_pixbuf_get_formats           (void);
gchar  *gdk_pixbuf_format_get_name       (GdkPixbufFormat *format);
gchar  *gdk_pixbuf_format_get_description(GdkPixbufFormat *format);
gchar **gdk_pixbuf_format_get_mime_types (GdkPixbufFormat *format);
gchar **gdk_pixbuf_format_get_extensions (GdkPixbufFormat *format);
gboolean gdk_pixbuf_format_is_writable   (GdkPixbufFormat *format);

G_END_DECLS

#endif /* GTK6_GDK_PIXBUF_H */

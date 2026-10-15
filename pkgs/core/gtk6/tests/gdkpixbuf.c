/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Compile-only probe for GDK's pixbuf API (gdk/gdkpixbuf.h, gdk/gdkpixdata.h).
 *
 * These headers are the gdk-pixbuf replacement: GTK6 owns the GdkPixbuf /
 * GdkPixbufFormat / GdkPixdata types and the small surface the imported
 * sources name, so that <gdk-pixbuf/gdk-pixbuf.h> is not on the include path
 * any more.  This probe names the types and constants and takes the address of
 * every function the sources call, so a missing declaration is a build failure
 * rather than a link error much later.  It is never linked or run (see
 * GTK4_PROBES in the Makefile).
 *
 * The negative control is the same probe spelling GdkPixbuf where the header
 * is not included: on this build the included-tree symbol GdkPixbuf is only
 * known through gdk/gdkpixbuf.h.
 */

#include <gdk/gdktypes.h>
#include <gdk/gdkpixbuf.h>
#include <gdk/gdkpixdata.h>

static GdkPixbuf       *pixbuf;
static GdkPixbufFormat *format;
static GdkPixdata       pixdata;
static GdkColorspace    colorspace;
static GdkInterpType    interp;

int
gdkpixbuf_probe (void)
{
  const void *symbols[] = {
    (const void *) gdk_pixbuf_get_type,
    (const void *) gdk_pixbuf_new,
    (const void *) gdk_pixbuf_new_from_file,
    (const void *) gdk_pixbuf_new_from_file_at_size,
    (const void *) gdk_pixbuf_new_from_resource,
    (const void *) gdk_pixbuf_new_from_data,
    (const void *) gdk_pixbuf_new_from_stream,
    (const void *) gdk_pixbuf_new_from_stream_async,
    (const void *) gdk_pixbuf_new_from_stream_finish,
    (const void *) gdk_pixbuf_get_from_texture,
    (const void *) gdk_pixbuf_get_width,
    (const void *) gdk_pixbuf_get_height,
    (const void *) gdk_pixbuf_get_rowstride,
    (const void *) gdk_pixbuf_get_n_channels,
    (const void *) gdk_pixbuf_get_has_alpha,
    (const void *) gdk_pixbuf_read_pixels,
    (const void *) gdk_pixbuf_scale_simple,
    (const void *) gdk_pixbuf_fill,
    (const void *) gdk_pixbuf_copy_area,
    (const void *) gdk_pixbuf_save_to_stream_async,
    (const void *) gdk_pixbuf_save_to_stream_finish,
    (const void *) gdk_pixbuf_get_formats,
    (const void *) gdk_pixbuf_format_get_name,
    (const void *) gdk_pixbuf_format_get_description,
    (const void *) gdk_pixbuf_format_get_mime_types,
    (const void *) gdk_pixbuf_format_get_extensions,
    (const void *) gdk_pixbuf_format_is_writable,
    (const void *) gdk_pixdata_deserialize,
  };
  const void *objects[] = {
    &pixbuf, &format, &pixdata, &colorspace, &interp,
  };
  const int constants[] = {
    GDK_COLORSPACE_RGB,
    GDK_INTERP_NEAREST,
    GDK_INTERP_BILINEAR,
    GDK_PIXBUF_MAGIC_NUMBER,
    GDK_PIXDATA_HEADER_LENGTH,
  };

  (void) symbols;
  (void) objects;
  (void) constants;
  (void) GDK_TYPE_PIXBUF;
  (void) GDK_IS_PIXBUF (pixbuf);

  return 0;
}

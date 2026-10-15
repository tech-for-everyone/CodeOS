/* GdkText
 * pangofc-decoder.h: Custom encoders/decoders on a per-font basis.
 *
 * Copyright (C) 2004 Red Hat Software
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.	 See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

#ifndef __PANGO_DECODER_H_
#define __PANGO_DECODER_H_

#include <pango/pangofc-font.h>

G_BEGIN_DECLS

#ifdef __GI_SCANNER__
#define GDK_TEXT_FC_TYPE_DECODER                   (gdk_text_fc_decoder_get_type())
#define GDK_TEXT_FC_DECODER(object)                (G_TYPE_CHECK_INSTANCE_CAST ((object), GDK_TEXT_FC_TYPE_DECODER, GdkTextFcDecoder))
#define GDK_TEXT_FC_IS_DECODER(object)             (G_TYPE_CHECK_INSTANCE_TYPE ((object), GDK_TEXT_FC_TYPE_DECODER))
#define GDK_TEXT_FC_DECODER_CLASS(klass)           (G_TYPE_CHECK_CLASS_CAST ((klass), GDK_TEXT_FC_TYPE_DECODER, GdkTextFcDecoderClass))
#define GDK_TEXT_FC_IS_DECODER_CLASS(klass)        (G_TYPE_CHECK_CLASS_TYPE ((klass), GDK_TEXT_FC_TYPE_DECODER))
#define GDK_TEXT_FC_DECODER_GET_CLASS(obj)         (G_TYPE_INSTANCE_GET_CLASS ((obj), GDK_TEXT_FC_TYPE_DECODER, GdkTextFcDecoderClass))
#else
#define GDK_TEXT_TYPE_FC_DECODER                   (gdk_text_fc_decoder_get_type())
#define GDK_TEXT_FC_DECODER(object)                (G_TYPE_CHECK_INSTANCE_CAST ((object), GDK_TEXT_TYPE_FC_DECODER, GdkTextFcDecoder))
#define GDK_TEXT_IS_FC_DECODER(object)             (G_TYPE_CHECK_INSTANCE_TYPE ((object), GDK_TEXT_TYPE_FC_DECODER))
#define GDK_TEXT_FC_DECODER_CLASS(klass)           (G_TYPE_CHECK_CLASS_CAST ((klass), GDK_TEXT_TYPE_FC_DECODER, GdkTextFcDecoderClass))
#define GDK_TEXT_IS_FC_DECODER_CLASS(klass)        (G_TYPE_CHECK_CLASS_TYPE ((klass), GDK_TEXT_TYPE_FC_DECODER))
#define GDK_TEXT_FC_DECODER_GET_CLASS(obj)         (G_TYPE_INSTANCE_GET_CLASS ((obj), GDK_TEXT_TYPE_FC_DECODER, GdkTextFcDecoderClass))
#endif

typedef struct _PangoFcDecoder      GdkTextFcDecoder;
typedef struct _PangoFcDecoderClass GdkTextFcDecoderClass;

/**
 * GdkTextFcDecoder:
 *
 * `GdkTextFcDecoder` is a virtual base class that implementations will
 * inherit from.
 *
 * It's the interface that is used to define a custom encoding for a font.
 * These objects are created in your code from a function callback that was
 * originally registered with [method@GdkTextFc.FontMap.add_decoder_find_func].
 * GdkText requires information about the supported charset for a font as well
 * as the individual character to glyph conversions. GdkText gets that
 * information via the #get_charset and #get_glyph callbacks into your
 * object implementation.
 *
 * Since: 1.6
 **/
struct _PangoFcDecoder
{
  /*< private >*/
  GObject parent_instance;
};

/**
 * GdkTextFcDecoderClass:
 * @get_charset: This returns an `FcCharset` given a `GdkTextFcFont` that
 *  includes a list of supported characters in the font.  The
 *  #FcCharSet that is returned should be an internal reference to your
 *  code.  GdkText will not free this structure.  It is important that
 *  you make this callback fast because this callback is called
 *  separately for each character to determine Unicode coverage.
 * @get_glyph: This returns a single `GdkTextGlyph` for a given Unicode
 *  code point.
 *
 * Class structure for `GdkTextFcDecoder`.
 *
 * Since: 1.6
 **/
struct _PangoFcDecoderClass
{
  /*< private >*/
  GObjectClass parent_class;

  /* vtable - not signals */
  /*< public >*/
  FcCharSet  *(*get_charset) (GdkTextFcDecoder *decoder,
			      GdkTextFcFont    *fcfont);
  GdkTextGlyph  (*get_glyph)   (GdkTextFcDecoder *decoder,
			      GdkTextFcFont    *fcfont,
			      guint32         wc);

  /*< private >*/

  /* Padding for future expansion */
  void (*_pango_reserved1) (void);
  void (*_pango_reserved2) (void);
  void (*_pango_reserved3) (void);
  void (*_pango_reserved4) (void);
};

GDK_TEXT_AVAILABLE_IN_1_6
GType      gdk_text_fc_decoder_get_type    (void) G_GNUC_CONST;

GDK_TEXT_AVAILABLE_IN_1_6
FcCharSet *gdk_text_fc_decoder_get_charset (GdkTextFcDecoder *decoder,
					 GdkTextFcFont    *fcfont);

GDK_TEXT_AVAILABLE_IN_1_6
GdkTextGlyph gdk_text_fc_decoder_get_glyph   (GdkTextFcDecoder *decoder,
					 GdkTextFcFont    *fcfont,
					 guint32         wc);

G_DEFINE_AUTOPTR_CLEANUP_FUNC (GdkTextFcDecoder, g_object_unref)

G_END_DECLS

#endif /* __PANGO_DECODER_H_ */


/* SPDX-License-Identifier: MIT */
/* GTK6: minimal stand-in for harfbuzz's hb.h.  GdkText's public headers only
 * mention these types by pointer (see pango-font.h, pango-coverage.h); the
 * real shaping is done by Qt6's bundled harfbuzz. */
#ifndef HB_H
#define HB_H
typedef struct hb_font_t hb_font_t;
typedef struct hb_face_t hb_face_t;
typedef struct hb_feature_t hb_feature_t;
typedef struct hb_glyph_info_t hb_glyph_info_t;
#endif

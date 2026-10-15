/* GdkText
 * pango-direction.h: Unicode text direction
 *
 * Copyright (C) 2018 Matthias Clasen
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

#ifndef __PANGO_DIRECTION_H__
#define __PANGO_DIRECTION_H__

#include <glib.h>

G_BEGIN_DECLS

/**
 * GdkTextDirection:
 * @GDK_TEXT_DIRECTION_LTR: A strong left-to-right direction
 * @GDK_TEXT_DIRECTION_RTL: A strong right-to-left direction
 * @GDK_TEXT_DIRECTION_TTB_LTR: Deprecated value; treated the
 *   same as `GDK_TEXT_DIRECTION_RTL`.
 * @GDK_TEXT_DIRECTION_TTB_RTL: Deprecated value; treated the
 *   same as `GDK_TEXT_DIRECTION_LTR`
 * @GDK_TEXT_DIRECTION_WEAK_LTR: A weak left-to-right direction
 * @GDK_TEXT_DIRECTION_WEAK_RTL: A weak right-to-left direction
 * @GDK_TEXT_DIRECTION_NEUTRAL: No direction specified
 *
 * `GdkTextDirection` represents a direction in the Unicode bidirectional
 * algorithm.
 *
 * Not every value in this enumeration makes sense for every usage of
 * `GdkTextDirection`; for example, the return value of [func@unichar_direction]
 * and [func@find_base_dir] cannot be `GDK_TEXT_DIRECTION_WEAK_LTR` or
 * `GDK_TEXT_DIRECTION_WEAK_RTL`, since every character is either neutral
 * or has a strong direction; on the other hand `GDK_TEXT_DIRECTION_NEUTRAL`
 * doesn't make sense to pass to [func@itemize_with_base_dir].
 *
 * The `GDK_TEXT_DIRECTION_TTB_LTR`, `GDK_TEXT_DIRECTION_TTB_RTL` values come from
 * an earlier interpretation of this enumeration as the writing direction
 * of a block of text and are no longer used. See `GdkTextGravity` for how
 * vertical text is handled in GdkText.
 *
 * If you are interested in text direction, you should really use fribidi
 * directly. `GdkTextDirection` is only retained because it is used in some
 * public apis.
 */
typedef enum {
  GDK_TEXT_DIRECTION_LTR,
  GDK_TEXT_DIRECTION_RTL,
  GDK_TEXT_DIRECTION_TTB_LTR,
  GDK_TEXT_DIRECTION_TTB_RTL,
  GDK_TEXT_DIRECTION_WEAK_LTR,
  GDK_TEXT_DIRECTION_WEAK_RTL,
  GDK_TEXT_DIRECTION_NEUTRAL
} GdkTextDirection;

G_END_DECLS

#endif /* __PANGO_DIRECTION_H__ */

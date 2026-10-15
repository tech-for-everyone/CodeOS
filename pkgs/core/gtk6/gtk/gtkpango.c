/* gtkpango.c - pango-related utilities
 *
 * Copyright (c) 2010 Red Hat, Inc.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library. If not, see <http://www.gnu.org/licenses/>.Free
 */
/*
 * Modified by the GTK+ Team and others 1997-2000.  See the AUTHORS
 * file for a list of people on the GTK+ Team.  See the ChangeLog
 * files for a list of changes.  These files are distributed with
 * GTK+ at ftp://ftp.gtk.org/pub/gtk/.
 */

#include "config.h"
#include "gtkpangoprivate.h"
#include <pango/pangocairo.h>
#include "gtkbuilderprivate.h"

static gboolean
attr_list_merge_filter (GdkTextAttribute *attribute,
                        gpointer        list)
{
  gdk_text_attr_list_change (list, gdk_text_attribute_copy (attribute));
  return FALSE;
}

/*
 * _gtk_pango_attr_list_merge:
 * @into: (nullable): a `GdkTextAttrList` where attributes are merged
 * @from: (nullable): a `GdkTextAttrList` with the attributes to merge
 *
 * Merges attributes from @from into @into.
 *
 * Returns: the merged list.
 */
GdkTextAttrList *
_gtk_pango_attr_list_merge (GdkTextAttrList *into,
                            GdkTextAttrList *from)
{
  if (from)
    {
      if (into)
        gdk_text_attr_list_filter (from, attr_list_merge_filter, into);
      else
        return gdk_text_attr_list_ref (from);
    }

  return into;
}

static GdkTextAttribute *
attribute_from_text (GtkBuilder  *builder,
                     const char  *name,
                     const char  *value,
                     GError     **error)
{
  GdkTextAttribute *attribute = NULL;
  GdkTextAttrType type;
  GdkTextLanguage *language;
  GdkTextFontDescription *font_desc;
  GdkRGBA *color;
  GValue val = G_VALUE_INIT;

  if (!gtk_builder_value_from_string_type (builder, GDK_TEXT_TYPE_ATTR_TYPE, name, &val, error))
    return NULL;

  type = g_value_get_enum (&val);
  g_value_unset (&val);

  switch (type)
    {
      /* GdkTextAttrLanguage */
    case GDK_TEXT_ATTR_LANGUAGE:
      if ((language = gdk_text_language_from_string (value)))
        {
          attribute = gdk_text_attr_language_new (language);
          g_value_init (&val, G_TYPE_INT);
        }
      break;
      /* GdkTextAttrInt */
    case GDK_TEXT_ATTR_STYLE:
      if (gtk_builder_value_from_string_type (builder, GDK_TEXT_TYPE_STYLE, value, &val, error))
        attribute = gdk_text_attr_style_new (g_value_get_enum (&val));
      break;
    case GDK_TEXT_ATTR_WEIGHT:
      if (gtk_builder_value_from_string_type (builder, GDK_TEXT_TYPE_WEIGHT, value, &val, error))
        attribute = gdk_text_attr_weight_new (g_value_get_enum (&val));
      else if (gtk_builder_value_from_string_type (builder, G_TYPE_INT, value, &val, NULL))
        {
          g_clear_error (error);
          attribute = gdk_text_attr_weight_new (g_value_get_int (&val));
        }
      break;
    case GDK_TEXT_ATTR_VARIANT:
      if (gtk_builder_value_from_string_type (builder, GDK_TEXT_TYPE_VARIANT, value, &val, error))
        attribute = gdk_text_attr_variant_new (g_value_get_enum (&val));
      break;
    case GDK_TEXT_ATTR_STRETCH:
      if (gtk_builder_value_from_string_type (builder, GDK_TEXT_TYPE_STRETCH, value, &val, error))
        attribute = gdk_text_attr_stretch_new (g_value_get_enum (&val));
      break;
#if GDK_TEXT_VERSION_CHECK (1, 58, 0)
    case GDK_TEXT_ATTR_WIDTH:
      if (gtk_builder_value_from_string_type (builder, GDK_TEXT_TYPE_WIDTH, value, &val, error))
        attribute = gdk_text_attr_width_new (g_value_get_enum (&val));
      else if (gtk_builder_value_from_string_type (builder, G_TYPE_INT, value, &val, NULL))
        {
          g_clear_error (error);
          attribute = gdk_text_attr_width_new (g_value_get_int (&val));
        }
      break;
#endif
    case GDK_TEXT_ATTR_UNDERLINE:
      if (gtk_builder_value_from_string_type (builder, GDK_TEXT_TYPE_UNDERLINE, value, &val, NULL))
        attribute = gdk_text_attr_underline_new (g_value_get_enum (&val));
      else
        {
          /* XXX: allow boolean for backwards compat, so ignore error */
          /* Deprecate this somehow */
          g_value_unset (&val);
          if (gtk_builder_value_from_string_type (builder, G_TYPE_BOOLEAN, value, &val, error))
            attribute = gdk_text_attr_underline_new (g_value_get_boolean (&val));
        }
      break;
    case GDK_TEXT_ATTR_STRIKETHROUGH:
      if (gtk_builder_value_from_string_type (builder, G_TYPE_BOOLEAN, value, &val, error))
        attribute = gdk_text_attr_strikethrough_new (g_value_get_boolean (&val));
      break;
    case GDK_TEXT_ATTR_GRAVITY:
      if (gtk_builder_value_from_string_type (builder, GDK_TEXT_TYPE_GRAVITY, value, &val, error))
        attribute = gdk_text_attr_gravity_new (g_value_get_enum (&val));
      break;
    case GDK_TEXT_ATTR_GRAVITY_HINT:
      if (gtk_builder_value_from_string_type (builder, GDK_TEXT_TYPE_GRAVITY_HINT, value, &val, error))
        attribute = gdk_text_attr_gravity_hint_new (g_value_get_enum (&val));
      break;
      /* GdkTextAttrString */
    case GDK_TEXT_ATTR_FAMILY:
      attribute = gdk_text_attr_family_new (value);
      g_value_init (&val, G_TYPE_INT);
      break;

      /* GdkTextAttrSize */
    case GDK_TEXT_ATTR_SIZE:
      if (gtk_builder_value_from_string_type (builder, G_TYPE_INT, value, &val, error))
        attribute = gdk_text_attr_size_new (g_value_get_int (&val));
      break;
    case GDK_TEXT_ATTR_ABSOLUTE_SIZE:
      if (gtk_builder_value_from_string_type (builder, G_TYPE_INT, value, &val, error))
        attribute = gdk_text_attr_size_new_absolute (g_value_get_int (&val));
      break;

      /* GdkTextAttrFontDesc */
    case GDK_TEXT_ATTR_FONT_DESC:
      if ((font_desc = gdk_text_font_description_from_string (value)))
        {
          attribute = gdk_text_attr_font_desc_new (font_desc);
          gdk_text_font_description_free (font_desc);
          g_value_init (&val, G_TYPE_INT);
        }
      break;
      /* GdkTextAttrColor */
    case GDK_TEXT_ATTR_FOREGROUND:
      if (gtk_builder_value_from_string_type (builder, GDK_TYPE_RGBA, value, &val, error))
        {
          color = g_value_get_boxed (&val);
          attribute = gdk_text_attr_foreground_new (color->red * 65535,
                                                 color->green * 65535,
                                                 color->blue * 65535);
        }
      break;
    case GDK_TEXT_ATTR_BACKGROUND:
      if (gtk_builder_value_from_string_type (builder, GDK_TYPE_RGBA, value, &val, error))
        {
          color = g_value_get_boxed (&val);
          attribute = gdk_text_attr_background_new (color->red * 65535,
                                                 color->green * 65535,
                                                 color->blue * 65535);
        }
      break;
    case GDK_TEXT_ATTR_UNDERLINE_COLOR:
      if (gtk_builder_value_from_string_type (builder, GDK_TYPE_RGBA, value, &val, error))
        {
          color = g_value_get_boxed (&val);
          attribute = gdk_text_attr_underline_color_new (color->red * 65535,
                                                      color->green * 65535,
                                                      color->blue * 65535);
        }
      break;
    case GDK_TEXT_ATTR_STRIKETHROUGH_COLOR:
      if (gtk_builder_value_from_string_type (builder, GDK_TYPE_RGBA, value, &val, error))
        {
          color = g_value_get_boxed (&val);
          attribute = gdk_text_attr_strikethrough_color_new (color->red * 65535,
                                                          color->green * 65535,
                                                          color->blue * 65535);
        }
      break;
      /* GdkTextAttrShape */
    case GDK_TEXT_ATTR_SHAPE:
      /* Unsupported for now */
      break;
      /* GdkTextAttrFloat */
    case GDK_TEXT_ATTR_SCALE:
      if (gtk_builder_value_from_string_type (builder, G_TYPE_DOUBLE, value, &val, error))
        attribute = gdk_text_attr_scale_new (g_value_get_double (&val));
      break;
    case GDK_TEXT_ATTR_LETTER_SPACING:
      if (gtk_builder_value_from_string_type (builder, G_TYPE_INT, value, &val, error))
        attribute = gdk_text_attr_letter_spacing_new (g_value_get_int (&val));
      break;
    case GDK_TEXT_ATTR_RISE:
      if (gtk_builder_value_from_string_type (builder, G_TYPE_INT, value, &val, error))
        attribute = gdk_text_attr_rise_new (g_value_get_int (&val));
      break;
    case GDK_TEXT_ATTR_FALLBACK:
      if (gtk_builder_value_from_string_type (builder, G_TYPE_BOOLEAN, value, &val, error))
        attribute = gdk_text_attr_fallback_new (g_value_get_boolean (&val));
      break;
    case GDK_TEXT_ATTR_FONT_FEATURES:
      attribute = gdk_text_attr_font_features_new (value);
      break;
    case GDK_TEXT_ATTR_FOREGROUND_ALPHA:
      if (gtk_builder_value_from_string_type (builder, G_TYPE_INT, value, &val, error))
        attribute = gdk_text_attr_foreground_alpha_new ((guint16)g_value_get_int (&val));
      break;
    case GDK_TEXT_ATTR_BACKGROUND_ALPHA:
      if (gtk_builder_value_from_string_type (builder, G_TYPE_INT, value, &val, error))
        attribute = gdk_text_attr_background_alpha_new ((guint16)g_value_get_int (&val));
      break;
    case GDK_TEXT_ATTR_ALLOW_BREAKS:
      if (gtk_builder_value_from_string_type (builder, G_TYPE_BOOLEAN, value, &val, error))
        attribute = gdk_text_attr_allow_breaks_new (g_value_get_boolean (&val));
      break;
    case GDK_TEXT_ATTR_SHOW:
      if (gtk_builder_value_from_string_type (builder, GDK_TEXT_TYPE_SHOW_FLAGS, value, &val, error))
        attribute = gdk_text_attr_show_new (g_value_get_flags (&val));
      break;
    case GDK_TEXT_ATTR_INSERT_HYPHENS:
      if (gtk_builder_value_from_string_type (builder, G_TYPE_BOOLEAN, value, &val, error))
        attribute = gdk_text_attr_insert_hyphens_new (g_value_get_boolean (&val));
      break;
    case GDK_TEXT_ATTR_OVERLINE:
      if (gtk_builder_value_from_string_type (builder, GDK_TEXT_TYPE_OVERLINE, value, &val, NULL))
        attribute = gdk_text_attr_overline_new (g_value_get_enum (&val));
      break;
    case GDK_TEXT_ATTR_OVERLINE_COLOR:
      if (gtk_builder_value_from_string_type (builder, GDK_TYPE_RGBA, value, &val, error))
        {
          color = g_value_get_boxed (&val);
          attribute = gdk_text_attr_overline_color_new (color->red * 65535,
                                                     color->green * 65535,
                                                     color->blue * 65535);
        }
      break;
    case GDK_TEXT_ATTR_LINE_HEIGHT:
      if (gtk_builder_value_from_string_type (builder, G_TYPE_DOUBLE, value, &val, error))
        attribute = gdk_text_attr_line_height_new (g_value_get_double (&val));
      break;
    case GDK_TEXT_ATTR_ABSOLUTE_LINE_HEIGHT:
      if (gtk_builder_value_from_string_type (builder, G_TYPE_INT, value, &val, error))
        attribute = gdk_text_attr_line_height_new_absolute (g_value_get_int (&val) * GDK_TEXT_SCALE);
      break;
    case GDK_TEXT_ATTR_TEXT_TRANSFORM:
      if (gtk_builder_value_from_string_type (builder, GDK_TEXT_TYPE_TEXT_TRANSFORM, value, &val, error))
        attribute = gdk_text_attr_text_transform_new (g_value_get_enum (&val));
      break;
    case GDK_TEXT_ATTR_WORD:
      attribute = gdk_text_attr_word_new ();
      break;
    case GDK_TEXT_ATTR_SENTENCE:
      attribute = gdk_text_attr_sentence_new ();
      break;
    case GDK_TEXT_ATTR_BASELINE_SHIFT:
      if (gtk_builder_value_from_string_type (builder, GDK_TEXT_TYPE_BASELINE_SHIFT, value, &val, NULL))
        attribute = gdk_text_attr_baseline_shift_new (g_value_get_enum (&val));
      else if (gtk_builder_value_from_string_type (builder, G_TYPE_INT, value, &val, NULL))
        attribute = gdk_text_attr_baseline_shift_new (g_value_get_enum (&val));
      else
        g_set_error (error,
                     GTK_BUILDER_ERROR,
                     GTK_BUILDER_ERROR_INVALID_VALUE,
                     "Could not parse '%s' as baseline shift value", value);
      break;
    case GDK_TEXT_ATTR_FONT_SCALE:
      if (gtk_builder_value_from_string_type (builder, GDK_TEXT_TYPE_FONT_SCALE, value, &val, error))
        attribute = gdk_text_attr_font_scale_new (g_value_get_enum (&val));
      break;
    case GDK_TEXT_ATTR_INVALID:
    default:
      break;
    }

  g_value_unset (&val);

  return attribute;
}

void
gtk_text_attribute_start_element (GtkBuildableParseContext  *context,
                                   const char                *element_name,
                                   const char               **names,
                                   const char               **values,
                                   gpointer                   user_data,
                                   GError                   **error)
{
  GtkTextAttributeParserData *data = user_data;

  if (strcmp (element_name, "attribute") == 0)
    {
      GdkTextAttribute *attr = NULL;
      const char *name = NULL;
      const char *value = NULL;
      const char *start = NULL;
      const char *end = NULL;
      guint start_val = 0;
      guint end_val = G_MAXUINT;
      GValue val = G_VALUE_INIT;

      if (!_gtk_builder_check_parent (data->builder, context, "attributes", error))
        return;

      if (!g_markup_collect_attributes (element_name, names, values, error,
                                        G_MARKUP_COLLECT_STRING, "name", &name,
                                        G_MARKUP_COLLECT_STRING, "value", &value,
                                        G_MARKUP_COLLECT_STRING|G_MARKUP_COLLECT_OPTIONAL, "start", &start,
                                        G_MARKUP_COLLECT_STRING|G_MARKUP_COLLECT_OPTIONAL, "end", &end,
                                        G_MARKUP_COLLECT_INVALID))
        {
          _gtk_builder_prefix_error (data->builder, context, error);
          return;
        }

      if (start)
        {
          if (!gtk_builder_value_from_string_type (data->builder, G_TYPE_UINT, start, &val, error))
            {
              _gtk_builder_prefix_error (data->builder, context, error);
              return;
            }
          start_val = g_value_get_uint (&val);
          g_value_unset (&val);
        }

      if (end)
        {
          if (!gtk_builder_value_from_string_type (data->builder, G_TYPE_UINT, end, &val, error))
            {
              _gtk_builder_prefix_error (data->builder, context, error);
              return;
            }
          end_val = g_value_get_uint (&val);
          g_value_unset (&val);
        }

      attr = attribute_from_text (data->builder, name, value, error);
      if (!attr)
        {
          _gtk_builder_prefix_error (data->builder, context, error);
          return;
        }

      attr->start_index = start_val;
      attr->end_index = end_val;

      if (!data->attrs)
        data->attrs = gdk_text_attr_list_new ();

      gdk_text_attr_list_insert (data->attrs, attr);
    }
  else if (strcmp (element_name, "attributes") == 0)
    {
      if (!_gtk_builder_check_parent (data->builder, context, "object", error))
        return;

      if (!g_markup_collect_attributes (element_name, names, values, error,
                                        G_MARKUP_COLLECT_INVALID, NULL, NULL,
                                        G_MARKUP_COLLECT_INVALID))
        _gtk_builder_prefix_error (data->builder, context, error);
    }
  else
    {
      _gtk_builder_error_unhandled_tag (data->builder, context,
                                        "GtkWidget", element_name,
                                        error);
    }
}

const char *
gdk_text_style_to_string (GdkTextStyle style)
{
  switch (style)
    {
    case GDK_TEXT_STYLE_NORMAL:
      return "normal";
    case GDK_TEXT_STYLE_OBLIQUE:
      return "oblique";
    case GDK_TEXT_STYLE_ITALIC:
      return "italic";
    default:
      g_assert_not_reached ();
    }
}

const char *
gdk_text_variant_to_string (GdkTextVariant variant)
{
  switch (variant)
    {
    case GDK_TEXT_VARIANT_NORMAL:
      return "normal";
    case GDK_TEXT_VARIANT_SMALL_CAPS:
      return "small_caps";
    case GDK_TEXT_VARIANT_ALL_SMALL_CAPS:
      return "all_small_caps";
    case GDK_TEXT_VARIANT_PETITE_CAPS:
      return "petite_caps";
    case GDK_TEXT_VARIANT_ALL_PETITE_CAPS:
      return "all_petite_caps";
    case GDK_TEXT_VARIANT_UNICASE:
      return "unicase";
    case GDK_TEXT_VARIANT_TITLE_CAPS:
      return "title_caps";
    default:
      g_assert_not_reached ();
    }
}

const char *
gdk_text_stretch_to_string (GdkTextStretch stretch)
{
  switch (stretch)
    {
    case GDK_TEXT_STRETCH_ULTRA_CONDENSED:
      return "ultra_condensed";
    case GDK_TEXT_STRETCH_EXTRA_CONDENSED:
      return "extra_condensed";
    case GDK_TEXT_STRETCH_CONDENSED:
      return "condensed";
    case GDK_TEXT_STRETCH_SEMI_CONDENSED:
      return "semi_condensed";
    case GDK_TEXT_STRETCH_NORMAL:
      return "normal";
    case GDK_TEXT_STRETCH_SEMI_EXPANDED:
      return "semi_expanded";
    case GDK_TEXT_STRETCH_EXPANDED:
      return "expanded";
    case GDK_TEXT_STRETCH_EXTRA_EXPANDED:
      return "extra_expanded";
    case GDK_TEXT_STRETCH_ULTRA_EXPANDED:
      return "ultra_expanded";
    default:
      g_assert_not_reached ();
    }
}

const char *
gdk_text_underline_to_string (GdkTextUnderline underline)
{
  switch (underline)
    {
    case GDK_TEXT_UNDERLINE_NONE:
      return "none";
    case GDK_TEXT_UNDERLINE_SINGLE:
    case GDK_TEXT_UNDERLINE_SINGLE_LINE:
      return "single";
    case GDK_TEXT_UNDERLINE_DOUBLE:
    case GDK_TEXT_UNDERLINE_DOUBLE_LINE:
      return "double";
    case GDK_TEXT_UNDERLINE_LOW:
      return "low";
    case GDK_TEXT_UNDERLINE_ERROR:
    case GDK_TEXT_UNDERLINE_ERROR_LINE:
      return "error";
    default:
      g_assert_not_reached ();
    }
}

const char *
gdk_text_overline_to_string (GdkTextOverline overline)
{
  switch (overline)
    {
    case GDK_TEXT_OVERLINE_NONE:
      return "none";
    case GDK_TEXT_OVERLINE_SINGLE:
      return "single";
    default:
      g_assert_not_reached ();
    }
}

const char *
gdk_text_wrap_mode_to_string (GdkTextWrapMode mode)
{
  /* Keep these in sync with gtk_wrap_mode_to_string() */
  switch (mode)
    {
    case GDK_TEXT_WRAP_WORD:
      return "word";
    case GDK_TEXT_WRAP_CHAR:
      return "char";
    case GDK_TEXT_WRAP_WORD_CHAR:
      return "word-char";
    case GDK_TEXT_WRAP_NONE:
      return "none";
    default:
      g_assert_not_reached ();
    }
}

const char *
gdk_text_align_to_string (GdkTextAlignment align)
{
  switch (align)
    {
    case GDK_TEXT_ALIGN_LEFT:
      return "left";
    case GDK_TEXT_ALIGN_CENTER:
      return "center";
    case GDK_TEXT_ALIGN_RIGHT:
      return "right";
    default:
      g_assert_not_reached ();
    }
}

static void
accumulate_font_attributes (GdkTextFontDescription *font,
                            GPtrArray *names,
                            GPtrArray *values)
{
  char weight[60];
  char size[60];

  g_ptr_array_add (names, g_strdup ("style"));
  g_ptr_array_add (values, g_strdup (gdk_text_style_to_string (gdk_text_font_description_get_style (font))));

  g_ptr_array_add (names, g_strdup ("variant"));
  g_ptr_array_add (values, g_strdup (gdk_text_variant_to_string (gdk_text_font_description_get_variant (font))));

  g_ptr_array_add (names, g_strdup ("stretch"));
  g_ptr_array_add (values, g_strdup (gdk_text_stretch_to_string (gdk_text_font_description_get_stretch (font))));

  g_ptr_array_add (names, g_strdup ("family-name"));
  g_ptr_array_add (values, g_strdup (gdk_text_font_description_get_family (font)));

  g_snprintf (weight, sizeof weight, "%d", gdk_text_font_description_get_weight (font));
  g_ptr_array_add (names, g_strdup ("weight"));
  g_ptr_array_add (values, g_strdup (weight));

  g_snprintf (size, sizeof size, "%i", gdk_text_font_description_get_size (font) / GDK_TEXT_SCALE);
  g_ptr_array_add (names, g_strdup ("size"));
  g_ptr_array_add (values, g_strdup (size));
}

void
gtk_text_get_font_attributes (GdkTextFontDescription   *font,
                               char                 ***attribute_names,
                               char                 ***attribute_values)
{
  GPtrArray *names = g_ptr_array_new_null_terminated (6, g_free, TRUE);
  GPtrArray *values = g_ptr_array_new_null_terminated (6, g_free, TRUE);

  accumulate_font_attributes (font, names, values);

  *attribute_names = g_strdupv ((char **) names->pdata);
  *attribute_values = g_strdupv ((char **) values->pdata);

  g_ptr_array_unref (names);
  g_ptr_array_unref (values);
}

/*
 * gtk_text_get_default_attributes:
 * @layout: the `GdkTextLayout` from which to get attributes
 * @attribute_names: (out) (array zero-terminated=1) (transfer full): the attribute names
 * @attribute_values: (out) (array zero-terminated=1) (transfer full): the attribute values
 *
 * Returns the default text attributes from @layout,
 * after translating them from GdkText attributes to atspi
 * attributes.
 *
 * This is a convenience function that can be used to implement
 * support for the `Text` interface in widgets using GdkText
 * layouts.
 */
void
gtk_text_get_default_attributes (GdkTextLayout   *layout,
                                  char        ***attribute_names,
                                  char        ***attribute_values)
{
  GdkTextContext *context;
  GPtrArray *names = g_ptr_array_new_null_terminated (16, g_free, TRUE);
  GPtrArray *values = g_ptr_array_new_null_terminated (16, g_free, TRUE);

  context = gdk_text_layout_get_context (layout);
  if (context)
    {
      GdkTextLanguage *language;
      GdkTextFontDescription *font;

      language = gdk_text_context_get_language (context);
      if (language)
        {
          g_ptr_array_add (names, g_strdup ("language"));
          g_ptr_array_add (values, g_strdup (gdk_text_language_to_string (language)));
        }

      font = gdk_text_context_get_font_description (context);
      if (font)
        accumulate_font_attributes (font, names, values);
    }

  g_ptr_array_add (names, g_strdup ("justification"));
  g_ptr_array_add (values,
                   g_strdup (gdk_text_align_to_string (gdk_text_layout_get_alignment (layout))));

  g_ptr_array_add (names, g_strdup ("wrap-mode"));
  g_ptr_array_add (values,
                   g_strdup (gdk_text_wrap_mode_to_string (gdk_text_layout_get_wrap (layout))));

  g_ptr_array_add (names, g_strdup ("strikethrough"));
  g_ptr_array_add (values, g_strdup ("false"));

  g_ptr_array_add (names, g_strdup ("underline"));
  g_ptr_array_add (values, g_strdup ("false"));

  g_ptr_array_add (names, g_strdup ("rise"));
  g_ptr_array_add (values, g_strdup ("0"));

  g_ptr_array_add (names, g_strdup ("scale"));
  g_ptr_array_add (values, g_strdup ("1"));

  g_ptr_array_add (names, g_strdup ("bg-full-height"));
  g_ptr_array_add (values, g_strdup ("0"));

  g_ptr_array_add (names, g_strdup ("pixels-inside-wrap"));
  g_ptr_array_add (values, g_strdup ("0"));

  g_ptr_array_add (names, g_strdup ("pixels-below-lines"));
  g_ptr_array_add (values, g_strdup ("0"));

  g_ptr_array_add (names, g_strdup ("pixels-above-lines"));
  g_ptr_array_add (values, g_strdup ("0"));

  g_ptr_array_add (names, g_strdup ("editable"));
  g_ptr_array_add (values, g_strdup ("false"));

  g_ptr_array_add (names, g_strdup ("invisible"));
  g_ptr_array_add (values, g_strdup ("false"));

  g_ptr_array_add (names, g_strdup ("indent"));
  g_ptr_array_add (values, g_strdup ("0"));

  g_ptr_array_add (names, g_strdup ("right-margin"));
  g_ptr_array_add (values, g_strdup ("0"));

  g_ptr_array_add (names, g_strdup ("left-margin"));
  g_ptr_array_add (values, g_strdup ("0"));

  *attribute_names = g_strdupv ((char **) names->pdata);
  *attribute_values = g_strdupv ((char **) values->pdata);

  g_ptr_array_unref (names);
  g_ptr_array_unref (values);
}

/*
 * gtk_text_get_run_attributes:
 * @layout: the `GdkTextLayout` to get the attributes from
 * @offset: the offset at which the attributes are wanted
 * @attribute_names: (array zero-terminated=1) (out) (transfer full): the
 *   attribute names
 * @attribute_values: (array zero-terminated=1) (out) (transfer full): the
 *   attribute values
 * @start_offset: return location for the starting offset
 *   of the current run
 * @end_offset: return location for the ending offset of the
 *   current run
 *
 * Finds the “run” around index (i.e. the maximal range of characters
 * where the set of applicable attributes remains constant) and
 * returns the starting and ending offsets for it.
 *
 * The attributes for the run are added to @attributes, after
 * translating them from GdkText attributes to atspi attributes.
 *
 * This is a convenience function that can be used to implement
 * support for the #AtkText interface in widgets using GdkText
 * layouts.
 */
void
gtk_text_get_run_attributes (GdkTextLayout     *layout,
                              unsigned int     offset,
                              char          ***attribute_names,
                              char          ***attribute_values,
                              unsigned int    *start_offset,
                              unsigned int    *end_offset)
{
  GdkTextAttrIterator *iter;
  GdkTextAttrList *attr;
  GdkTextAttrString *gdk_text_string;
  GdkTextAttrInt *gdk_text_int;
  GdkTextAttrColor *gdk_text_color;
  GdkTextAttrLanguage *gdk_text_lang;
  GdkTextAttrFloat *gdk_text_float;
  int index, start_index, end_index;
  gboolean is_next;
  glong len;
  const char *text;
  GPtrArray *names, *values;

  text = gdk_text_layout_get_text (layout);
  len = g_utf8_strlen (text, -1);

  /* Grab the attributes of the GdkTextLayout, if any */
  attr = gdk_text_layout_get_attributes (layout);

  if (attr == NULL)
    {
      *attribute_names = g_new0 (char *, 1);
      *attribute_values = g_new0 (char *, 1);
      *start_offset = 0;
      *end_offset = len;
      return;
    }

  iter = gdk_text_attr_list_get_iterator (attr);
  /* Get invariant range offsets */
  /* If offset out of range, set offset in range */
  if (offset > len)
    offset = len;
  else if (offset < 0)
    offset = 0;

  names = g_ptr_array_new_null_terminated (16, g_free, TRUE);
  values = g_ptr_array_new_null_terminated (16, g_free, TRUE);

  index = g_utf8_offset_to_pointer (text, offset) - text;
  gdk_text_attr_iterator_range (iter, &start_index, &end_index);
  is_next = TRUE;
  while (is_next)
    {
      if (index >= start_index && index < end_index)
        {
          *start_offset = g_utf8_pointer_to_offset (text, text + start_index);
          if (end_index == G_MAXINT) /* Last iterator */
            end_index = len;

          *end_offset = g_utf8_pointer_to_offset (text, text + end_index);
          break;
        }
      is_next = gdk_text_attr_iterator_next (iter);
      gdk_text_attr_iterator_range (iter, &start_index, &end_index);
    }

  /* Get attributes */
  gdk_text_string = (GdkTextAttrString *) gdk_text_attr_iterator_get (iter, GDK_TEXT_ATTR_FAMILY);
  if (gdk_text_string != NULL)
    {
      g_ptr_array_add (names, g_strdup ("family-name"));
      g_ptr_array_add (values, g_strdup_printf ("%s", gdk_text_string->value));
    }

  gdk_text_int = (GdkTextAttrInt *) gdk_text_attr_iterator_get (iter, GDK_TEXT_ATTR_STYLE);
  if (gdk_text_int != NULL)
    {
      g_ptr_array_add (names, g_strdup ("style"));
      g_ptr_array_add (values, g_strdup (gdk_text_style_to_string (gdk_text_int->value)));
    }

  gdk_text_int = (GdkTextAttrInt *) gdk_text_attr_iterator_get (iter, GDK_TEXT_ATTR_WEIGHT);
  if (gdk_text_int != NULL)
    {
      g_ptr_array_add (names, g_strdup ("weight"));
      g_ptr_array_add (values, g_strdup_printf ("%i", gdk_text_int->value));
    }

  gdk_text_int = (GdkTextAttrInt *) gdk_text_attr_iterator_get (iter, GDK_TEXT_ATTR_VARIANT);
  if (gdk_text_int != NULL)
    {
      g_ptr_array_add (names, g_strdup ("variant"));
      g_ptr_array_add (values, g_strdup (gdk_text_variant_to_string (gdk_text_int->value)));
    }

  gdk_text_int = (GdkTextAttrInt *) gdk_text_attr_iterator_get (iter, GDK_TEXT_ATTR_STRETCH);
  if (gdk_text_int != NULL)
    {
      g_ptr_array_add (names, g_strdup ("stretch"));
      g_ptr_array_add (values, g_strdup (gdk_text_stretch_to_string (gdk_text_int->value)));
    }

  gdk_text_int = (GdkTextAttrInt *) gdk_text_attr_iterator_get (iter, GDK_TEXT_ATTR_SIZE);
  if (gdk_text_int != NULL)
    {
      g_ptr_array_add (names, g_strdup ("size"));
      g_ptr_array_add (values, g_strdup_printf ("%i", gdk_text_int->value / GDK_TEXT_SCALE));
    }

  gdk_text_int = (GdkTextAttrInt *) gdk_text_attr_iterator_get (iter, GDK_TEXT_ATTR_UNDERLINE);
  if (gdk_text_int != NULL)
    {
      g_ptr_array_add (names, g_strdup ("underline"));
      g_ptr_array_add (values, g_strdup (gdk_text_underline_to_string (gdk_text_int->value)));
    }

  gdk_text_int = (GdkTextAttrInt *) gdk_text_attr_iterator_get (iter, GDK_TEXT_ATTR_OVERLINE);
  if (gdk_text_int != NULL)
    {
      g_ptr_array_add (names, g_strdup ("overline"));
      g_ptr_array_add (values, g_strdup (gdk_text_overline_to_string (gdk_text_int->value)));
    }

  gdk_text_int = (GdkTextAttrInt *) gdk_text_attr_iterator_get (iter, GDK_TEXT_ATTR_STRIKETHROUGH);
  if (gdk_text_int != NULL)
    {
      g_ptr_array_add (names, g_strdup ("strikethrough"));
      g_ptr_array_add (values, gdk_text_int->value ? g_strdup ("true") : g_strdup ("false"));
    }

  gdk_text_int = (GdkTextAttrInt *) gdk_text_attr_iterator_get (iter, GDK_TEXT_ATTR_RISE);
  if (gdk_text_int != NULL)
    {
      g_ptr_array_add (names, g_strdup ("rise"));
      g_ptr_array_add (values, g_strdup_printf ("%i", gdk_text_int->value));
    }

  gdk_text_lang = (GdkTextAttrLanguage *) gdk_text_attr_iterator_get (iter, GDK_TEXT_ATTR_LANGUAGE);
  if (gdk_text_lang != NULL)
    {
      g_ptr_array_add (names, g_strdup ("language"));
      g_ptr_array_add (values, g_strdup (gdk_text_language_to_string (gdk_text_lang->value)));
    }

  gdk_text_float = (GdkTextAttrFloat *) gdk_text_attr_iterator_get (iter, GDK_TEXT_ATTR_SCALE);
  if (gdk_text_float != NULL)
    {
      g_ptr_array_add (names, g_strdup ("scale"));
      g_ptr_array_add (values, g_strdup_printf ("%g", gdk_text_float->value));
    }

  gdk_text_color = (GdkTextAttrColor *) gdk_text_attr_iterator_get (iter, GDK_TEXT_ATTR_FOREGROUND);
  if (gdk_text_color != NULL)
    {
      g_ptr_array_add (names, g_strdup ("fg-color"));
      g_ptr_array_add (values, g_strdup_printf ("%u,%u,%u",
                                                gdk_text_color->color.red,
                                                gdk_text_color->color.green,
                                                gdk_text_color->color.blue));
    }

  gdk_text_color = (GdkTextAttrColor *) gdk_text_attr_iterator_get (iter, GDK_TEXT_ATTR_BACKGROUND);
  if (gdk_text_color != NULL)
    {
      g_ptr_array_add (names, g_strdup ("bg-color"));
      g_ptr_array_add (values, g_strdup_printf ("%u,%u,%u",
                                                gdk_text_color->color.red,
                                                gdk_text_color->color.green,
                                                gdk_text_color->color.blue));
    }

  gdk_text_attr_iterator_destroy (iter);

  *attribute_names = g_strdupv ((char **) names->pdata);
  *attribute_values = g_strdupv ((char **) values->pdata);

  g_ptr_array_unref (names);
  g_ptr_array_unref (values);
}

/*
 * gtk_text_move_chars:
 * @layout: a `GdkTextLayout`
 * @offset: a character offset in @layout
 * @count: the number of characters to move from @offset
 *
 * Returns the position that is @count characters from the
 * given @offset. @count may be positive or negative.
 *
 * For the purpose of this function, characters are defined
 * by what GdkText considers cursor positions.
 *
 * Returns: the new position
 */
static int
gtk_text_move_chars (GdkTextLayout *layout,
                      int          offset,
                      int          count)
{
  const GdkTextLogAttr *attrs;
  int n_attrs;

  attrs = gdk_text_layout_get_log_attrs_readonly (layout, &n_attrs);

  while (count > 0 && offset < n_attrs - 1)
    {
      do
        offset++;
      while (offset < n_attrs - 1 && !attrs[offset].is_cursor_position);

      count--;
    }
  while (count < 0 && offset > 0)
    {
      do
        offset--;
      while (offset > 0 && !attrs[offset].is_cursor_position);

      count++;
    }

  return offset;
}

/*
 * gtk_text_move_words:
 * @layout: a `GdkTextLayout`
 * @offset: a character offset in @layout
 * @count: the number of words to move from @offset
 *
 * Returns the position that is @count words from the
 * given @offset. @count may be positive or negative.
 *
 * If @count is positive, the returned position will
 * be a word end, otherwise it will be a word start.
 * See the GdkText documentation for details on how
 * word starts and ends are defined.
 *
 * Returns: the new position
 */
static int
gtk_text_move_words (GdkTextLayout  *layout,
                      int           offset,
                      int           count)
{
  const GdkTextLogAttr *attrs;
  int n_attrs;

  attrs = gdk_text_layout_get_log_attrs_readonly (layout, &n_attrs);

  while (count > 0 && offset < n_attrs - 1)
    {
      do
        offset++;
      while (offset < n_attrs - 1 && !attrs[offset].is_word_end);

      count--;
    }
  while (count < 0 && offset > 0)
    {
      do
        offset--;
      while (offset > 0 && !attrs[offset].is_word_start);

      count++;
    }

  return offset;
}

/*
 * gtk_text_move_sentences:
 * @layout: a `GdkTextLayout`
 * @offset: a character offset in @layout
 * @count: the number of sentences to move from @offset
 *
 * Returns the position that is @count sentences from the
 * given @offset. @count may be positive or negative.
 *
 * If @count is positive, the returned position will
 * be a sentence end, otherwise it will be a sentence start.
 * See the GdkText documentation for details on how
 * sentence starts and ends are defined.
 *
 * Returns: the new position
 */
static int
gtk_text_move_sentences (GdkTextLayout  *layout,
                          int           offset,
                          int           count)
{
  const GdkTextLogAttr *attrs;
  int n_attrs;

  attrs = gdk_text_layout_get_log_attrs_readonly (layout, &n_attrs);

  while (count > 0 && offset < n_attrs - 1)
    {
      do
        offset++;
      while (offset < n_attrs - 1 && !attrs[offset].is_sentence_end);

      count--;
    }
  while (count < 0 && offset > 0)
    {
      do
        offset--;
      while (offset > 0 && !attrs[offset].is_sentence_start);

      count++;
    }

  return offset;
}

#if 0
/*
 * gtk_text_move_lines:
 * @layout: a `GdkTextLayout`
 * @offset: a character offset in @layout
 * @count: the number of lines to move from @offset
 *
 * Returns the position that is @count lines from the
 * given @offset. @count may be positive or negative.
 *
 * If @count is negative, the returned position will
 * be the start of a line, else it will be the end of
 * line.
 *
 * Returns: the new position
 */
static int
gtk_text_move_lines (GdkTextLayout *layout,
                      int          offset,
                      int          count)
{
  GSList *lines, *l;
  GdkTextLayoutLine *line;
  int num;
  const char *text;
  int pos, line_pos;
  int index;
  int len;

  text = gdk_text_layout_get_text (layout);
  index = g_utf8_offset_to_pointer (text, offset) - text;
  lines = gdk_text_layout_get_lines (layout);
  line = NULL;

  num = 0;
  for (l = lines; l; l = l->next)
    {
      line = l->data;
      if (index < line->start_index + line->length)
        break;
      num++;
    }

  if (count < 0)
    {
      num += count;
      if (num < 0)
        num = 0;

      line = g_slist_nth_data (lines, num);

      return g_utf8_pointer_to_offset (text, text + line->start_index);
    }
  else
    {
      line_pos = index - line->start_index;

      len = g_slist_length (lines);
      num += count;
      if (num >= len || (count == 0 && num == len - 1))
        return g_utf8_strlen (text, -1) - 1;

      line = l->data;
      pos = line->start_index + line_pos;
      if (pos >= line->start_index + line->length)
        pos = line->start_index + line->length - 1;

      return g_utf8_pointer_to_offset (text, text + pos);
    }
}
#endif

/*
 * gtk_text_is_inside_word:
 * @layout: a `GdkTextLayout`
 * @offset: a character offset in @layout
 *
 * Returns whether the given position is inside
 * a word.
 *
 * Returns: %TRUE if @offset is inside a word
 */
static gboolean
gtk_text_is_inside_word (GdkTextLayout  *layout,
                          int           offset)
{
  const GdkTextLogAttr *attrs;
  int n_attrs;

  attrs = gdk_text_layout_get_log_attrs_readonly (layout, &n_attrs);

  while (offset >= 0 &&
         !(attrs[offset].is_word_start || attrs[offset].is_word_end))
    offset--;

  if (offset >= 0)
    return attrs[offset].is_word_start;

  return FALSE;
}

/*
 * gtk_text_is_inside_sentence:
 * @layout: a `GdkTextLayout`
 * @offset: a character offset in @layout
 *
 * Returns whether the given position is inside
 * a sentence.
 *
 * Returns: %TRUE if @offset is inside a sentence
 */
static gboolean
gtk_text_is_inside_sentence (GdkTextLayout  *layout,
                              int           offset)
{
  const GdkTextLogAttr *attrs;
  int n_attrs;

  attrs = gdk_text_layout_get_log_attrs_readonly (layout, &n_attrs);

  while (offset >= 0 &&
         !(attrs[offset].is_sentence_start || attrs[offset].is_sentence_end))
    offset--;

  if (offset >= 0)
    return attrs[offset].is_sentence_start;

  return FALSE;
}

static void
gtk_text_get_line_start (GdkTextLayout *layout,
                          int          offset,
                          int         *start_offset,
                          int         *end_offset)
{
  GdkTextLayoutIter *iter;
  GdkTextLayoutLine *line, *prev_line = NULL;
  int index, start_index, length, end_index;
  const char *text;
  gboolean found = FALSE;

  text = gdk_text_layout_get_text (layout);
  index = g_utf8_offset_to_pointer (text, offset) - text;
  iter = gdk_text_layout_get_iter (layout);
  do
    {
      line = gdk_text_layout_iter_get_line (iter);
      start_index = gdk_text_layout_line_get_start_index (line);
      length = gdk_text_layout_line_get_length (line);
      end_index = start_index + length;

      if (index >= start_index && (index < end_index || gdk_text_layout_iter_at_last_line (iter)))
        {
          /* Found line for offset */
          if (gdk_text_layout_iter_next_line (iter))
            end_index = gdk_text_layout_line_get_start_index (gdk_text_layout_iter_get_line (iter));

          found = TRUE;
          break;
        }

      prev_line = line;
    }
  while (gdk_text_layout_iter_next_line (iter));

  if (!found)
    {
      start_index = gdk_text_layout_line_get_start_index (prev_line) + gdk_text_layout_line_get_length (prev_line);
      end_index = start_index;
    }

  gdk_text_layout_iter_free (iter);

  *start_offset = g_utf8_pointer_to_offset (text, text + start_index);
  *end_offset = g_utf8_pointer_to_offset (text, text + end_index);
}

char *
gtk_text_get_string_at (GdkTextLayout                  *layout,
                         unsigned int                  offset,
                         GtkAccessibleTextGranularity  granularity,
                         unsigned int                 *start_offset,
                         unsigned int                 *end_offset)
{
  const char *text;
  int start, end;
  const GdkTextLogAttr *attrs;
  int n_attrs;

  text = gdk_text_layout_get_text (layout);

  if (text[0] == 0)
    {
      *start_offset = 0;
      *end_offset = 0;
      return g_strdup ("");
    }

  attrs = gdk_text_layout_get_log_attrs_readonly (layout, &n_attrs);

  start = offset;
  end = start;

  switch (granularity)
    {
    case GTK_ACCESSIBLE_TEXT_GRANULARITY_CHARACTER:
      end = gtk_text_move_chars (layout, end, 1);
      break;

    case GTK_ACCESSIBLE_TEXT_GRANULARITY_WORD:
      if (!attrs[start].is_word_start)
        start = gtk_text_move_words (layout, start, -1);
      if (gtk_text_is_inside_word (layout, end))
        end = gtk_text_move_words (layout, end, 1);
      while (!attrs[end].is_word_start && end < n_attrs - 1)
        end = gtk_text_move_chars (layout, end, 1);
      break;

    case GTK_ACCESSIBLE_TEXT_GRANULARITY_SENTENCE:
      if (!attrs[start].is_sentence_start)
        start = gtk_text_move_sentences (layout, start, -1);
      if (gtk_text_is_inside_sentence (layout, end))
        end = gtk_text_move_sentences (layout, end, 1);
      while (!attrs[end].is_sentence_start && end < n_attrs - 1)
        end = gtk_text_move_chars (layout, end, 1);
      break;

    case GTK_ACCESSIBLE_TEXT_GRANULARITY_LINE:
      gtk_text_get_line_start (layout, offset, &start, &end);
      break;

    case GTK_ACCESSIBLE_TEXT_GRANULARITY_PARAGRAPH:
      /* FIXME: In theory, a layout can hold more than one paragraph */
      start = 0;
      end = g_utf8_strlen (text, -1);
      break;

    default:
      g_assert_not_reached ();
      break;
    }

  *start_offset = start;
  *end_offset = end;

  g_assert (start <= end);

  return g_utf8_substring (text, start, end);
}

gboolean
gtk_text_glyph_item_has_color_glyphs (GdkTextGlyphItem *run)
{
  for (int i = 0; i < run->glyphs->num_glyphs; i++)
    {
      if (run->glyphs->glyphs[i].attr.is_color)
        return TRUE;
   }

  return FALSE;
}

gboolean
gtk_text_layout_has_color_glyphs (GdkTextLayout *layout)
{
  gboolean ret = FALSE;
  GdkTextLayoutIter *iter;

  iter = gdk_text_layout_get_iter (layout);

  while (gdk_text_layout_iter_next_run (iter) && !ret)
    {
      GdkTextLayoutRun *run = gdk_text_layout_iter_get_run_readonly (iter);

      if (!run)
        continue;

      if (gtk_text_glyph_item_has_color_glyphs (run))
        {
          ret = TRUE;
          break;
        }
    }

  gdk_text_layout_iter_free (iter);

  return ret;
}


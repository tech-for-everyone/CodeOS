/* GTK - The GIMP Toolkit
 * Copyright (C) 2017 Benjamin Otte
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
 * License along with this library. If not, see <http://www.gnu.org/licenses/>.
 */

#pragma once

#if !defined (__GDK_H_INSIDE__) && !defined (GTK_COMPILATION)
#error "Only <gdk/gdk.h> can be included directly."
#endif

#include <gdk/gdktypes.h>

G_BEGIN_DECLS

#define GDK_TYPE_CONTENT_SERIALIZER         (gdk_content_serializer_get_type ())

GDK_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (GdkContentSerializer, gdk_content_serializer, GDK, CONTENT_SERIALIZER, GObject)

/**
 * GdkContentSerializeFunc:
 * @serializer: a `GdkContentSerializer`
 *
 * The type of a function that can be registered with gdk_content_register_serializer().
 *
 * When the function gets called to operate on content, it can call functions on the
 * @serializer object to obtain the mime type, output stream, user data, etc. for its
 * operation.
 */
typedef void (* GdkContentSerializeFunc) (GdkContentSerializer *serializer);

GDK_AVAILABLE_IN_ALL
const char *            gdk_content_serializer_get_mime_type            (GdkContentSerializer   *serializer);
GDK_AVAILABLE_IN_ALL
GType                   gdk_content_serializer_get_gtype                (GdkContentSerializer   *serializer);
GDK_AVAILABLE_IN_ALL
const GValue *          gdk_content_serializer_get_value                (GdkContentSerializer   *serializer);
GDK_AVAILABLE_IN_ALL
GOutputStream *         gdk_content_serializer_get_output_stream        (GdkContentSerializer   *serializer);
GDK_AVAILABLE_IN_ALL
int                     gdk_content_serializer_get_priority             (GdkContentSerializer   *serializer);
GDK_AVAILABLE_IN_ALL
GCancellable *          gdk_content_serializer_get_cancellable          (GdkContentSerializer   *serializer);
GDK_AVAILABLE_IN_ALL
gpointer                gdk_content_serializer_get_user_data            (GdkContentSerializer   *serializer);
GDK_AVAILABLE_IN_ALL
void                    gdk_content_serializer_set_task_data            (GdkContentSerializer   *serializer,
                                                                         gpointer                data,
                                                                         GDestroyNotify          notify);
GDK_AVAILABLE_IN_ALL
gpointer                gdk_content_serializer_get_task_data            (GdkContentSerializer   *serializer);

GDK_AVAILABLE_IN_ALL
void                    gdk_content_serializer_return_success           (GdkContentSerializer   *serializer);
GDK_AVAILABLE_IN_ALL
void                    gdk_content_serializer_return_error             (GdkContentSerializer   *serializer,
                                                                         GError                 *error);

GDK_AVAILABLE_IN_ALL
GdkContentFormats *     gdk_content_formats_union_serialize_gtypes      (GdkContentFormats      *formats);
GDK_AVAILABLE_IN_ALL
GdkContentFormats *     gdk_content_formats_union_serialize_mime_types  (GdkContentFormats      *formats);

GDK_AVAILABLE_IN_ALL
void                    gdk_content_register_serializer                 (GType                   type,
                                                                         const char             *mime_type,
                                                                         GdkContentSerializeFunc serialize,
                                                                         gpointer                data,
                                                                         GDestroyNotify          notify);
GDK_AVAILABLE_IN_ALL
void                    gdk_content_serialize_async                     (GOutputStream          *stream,
                                                                         const char             *mime_type,
                                                                         const GValue           *value,
                                                                         int                     io_priority,
                                                                         GCancellable           *cancellable,
                                                                         GAsyncReadyCallback     callback,
                                                                         gpointer                user_data);
GDK_AVAILABLE_IN_ALL
gboolean                gdk_content_serialize_finish                    (GAsyncResult           *result,
                                                                         GError                **error);

G_END_DECLS

/* GDK - The GIMP Drawing Kit
 *
 * Copyright (C) 2017 Benjamin Otte <otte@gnome.org>
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

#include "config.h"

#include "gdkpipeiostreamprivate.h"

#include <string.h>

/* PIPE */

typedef enum {
  GDK_IO_PIPE_EMPTY,
  GDK_IO_PIPE_INPUT_BUFFER,
  GDK_IO_PIPE_OUTPUT_BUFFER
} GdkIOPipeState;

typedef struct _GdkIOPipe GdkIOPipe;

struct _GdkIOPipe
{
  int ref_count;

  GMutex mutex;
  GCond cond;
  guchar *buffer;
  gsize size;
  guint state : 2; /* GdkIOPipeState */
  guint input_closed : 1;
  guint output_closed : 1;
};

static GdkIOPipe *
gdk_io_pipe_new (void)
{
  GdkIOPipe *pipe;

  pipe = g_new0 (GdkIOPipe, 1);
  pipe->ref_count = 1;

  g_mutex_init (&pipe->mutex);
  g_cond_init (&pipe->cond);

  return pipe;
}

static GdkIOPipe *
gdk_io_pipe_ref (GdkIOPipe *pipe)
{
  g_atomic_int_inc (&pipe->ref_count);

  return pipe;
}

static void
gdk_io_pipe_unref (GdkIOPipe *pipe)
{
  if (!g_atomic_int_dec_and_test (&pipe->ref_count))
    return;

  g_cond_clear (&pipe->cond);
  g_mutex_clear (&pipe->mutex);

  g_free (pipe);
}

static void
gdk_io_pipe_lock (GdkIOPipe *pipe)
{
  g_mutex_lock (&pipe->mutex);
}

static void
gdk_io_pipe_unlock (GdkIOPipe *pipe)
{
  g_mutex_unlock (&pipe->mutex);
}

/* INPUT STREAM */

#define GDK_TYPE_PIPE_INPUT_STREAM (gdk_pipe_input_stream_get_type ())
GDK_DECLARE_INTERNAL_TYPE (GdkPipeInputStream, gdk_pipe_input_stream, GDK, PIPE_INPUT_STREAM, GInputStream)

struct _GdkPipeInputStream
{
  GInputStream parent;

  GdkIOPipe *pipe;
};

struct _GdkPipeInputStreamClass
{
  GInputStreamClass parent_class;
};

G_DEFINE_TYPE (GdkPipeInputStream, gdk_pipe_input_stream, G_TYPE_INPUT_STREAM)

static void
gdk_pipe_input_stream_finalize (GObject *object)
{
  GdkPipeInputStream *pipe = GDK_PIPE_INPUT_STREAM (object);

  g_clear_pointer (&pipe->pipe, gdk_io_pipe_unref);

  G_OBJECT_CLASS (gdk_pipe_input_stream_parent_class)->finalize (object);
}

static gssize
gdk_pipe_input_stream_read (GInputStream  *stream,
                            void          *buffer,
                            gsize          count,
                            GCancellable  *cancellable,
                            GError       **error)
{
  GdkPipeInputStream *pipe_stream = GDK_PIPE_INPUT_STREAM (stream);
  GdkIOPipe *pipe = pipe_stream->pipe;
  gsize amount;

  gdk_io_pipe_lock (pipe);

  switch (pipe->state)
  {
    case GDK_IO_PIPE_EMPTY:
      if (pipe->output_closed)
        {
          amount = 0;
          break;
        }
      pipe->buffer = buffer;
      pipe->size = count;
      pipe->state = GDK_IO_PIPE_INPUT_BUFFER;
      do
        g_cond_wait (&pipe->cond, &pipe->mutex);
      while (pipe->size == count && 
             pipe->state == GDK_IO_PIPE_INPUT_BUFFER &&
             !pipe->output_closed);
      if (pipe->state == GDK_IO_PIPE_INPUT_BUFFER)
        {
          amount = count - pipe->size;
          pipe->state = GDK_IO_PIPE_EMPTY;
          pipe->size = 0;
        }
      else
        {
          amount = count;
        }
      break;

    case GDK_IO_PIPE_OUTPUT_BUFFER:
      amount = MIN (count, pipe->size);
      
      memcpy (buffer, pipe->buffer, amount);
      pipe->size -= amount;

      if (pipe->size == 0)
        pipe->state = GDK_IO_PIPE_EMPTY;
      else
        pipe->buffer += amount;
      break;

    case GDK_IO_PIPE_INPUT_BUFFER:
    default:
      g_assert_not_reached ();
      amount = 0;
      break;
  }
    
  g_cond_broadcast (&pipe->cond);
  gdk_io_pipe_unlock (pipe);

  return amount;
}

static gboolean
gdk_pipe_input_stream_close (GInputStream  *stream,
                             GCancellable  *cancellable,
                             GError       **error)
{
  GdkPipeInputStream *pipe_stream = GDK_PIPE_INPUT_STREAM (stream);
  GdkIOPipe *pipe = pipe_stream->pipe;

  gdk_io_pipe_lock (pipe);

  pipe->input_closed = TRUE;
  g_cond_broadcast (&pipe->cond);
    
  gdk_io_pipe_unlock (pipe);

  return TRUE;
}

static void
gdk_pipe_input_stream_class_init (GdkPipeInputStreamClass *class)
{
  GObjectClass *object_class = G_OBJECT_CLASS (class);
  GInputStreamClass *input_stream_class = G_INPUT_STREAM_CLASS (class);

  object_class->finalize = gdk_pipe_input_stream_finalize;

  input_stream_class->read_fn = gdk_pipe_input_stream_read;
  input_stream_class->close_fn = gdk_pipe_input_stream_close;
}

static void
gdk_pipe_input_stream_init (GdkPipeInputStream *pipe)
{
}

/* OUTPUT STREAM */

#define GDK_TYPE_PIPE_OUTPUT_STREAM (gdk_pipe_output_stream_get_type ())
GDK_DECLARE_INTERNAL_TYPE (GdkPipeOutputStream, gdk_pipe_output_stream, GDK, PIPE_OUTPUT_STREAM, GOutputStream)

struct _GdkPipeOutputStream
{
  GOutputStream parent;

  GdkIOPipe *pipe;
};

struct _GdkPipeOutputStreamClass
{
  GOutputStreamClass parent_class;
};

G_DEFINE_TYPE (GdkPipeOutputStream, gdk_pipe_output_stream, G_TYPE_OUTPUT_STREAM)

static void
gdk_pipe_output_stream_finalize (GObject *object)
{
  GdkPipeOutputStream *pipe = GDK_PIPE_OUTPUT_STREAM (object);

  g_clear_pointer (&pipe->pipe, gdk_io_pipe_unref);

  G_OBJECT_CLASS (gdk_pipe_output_stream_parent_class)->finalize (object);
}

static gssize
gdk_pipe_output_stream_write (GOutputStream  *stream,
                              const void     *buffer,
                              gsize           count,
                              GCancellable   *cancellable,
                              GError        **error)
{
  GdkPipeOutputStream *pipe_stream = GDK_PIPE_OUTPUT_STREAM (stream);
  GdkIOPipe *pipe = pipe_stream->pipe;
  gsize amount;

  gdk_io_pipe_lock (pipe);

  switch (pipe->state)
  {
    case GDK_IO_PIPE_EMPTY:
      pipe->buffer = (void *) buffer;
      pipe->size = count;
      pipe->state = GDK_IO_PIPE_OUTPUT_BUFFER;
      while (pipe->size == count && 
             pipe->state == GDK_IO_PIPE_OUTPUT_BUFFER &&
             !pipe->input_closed)
        g_cond_wait (&pipe->cond, &pipe->mutex);
      if (pipe->state == GDK_IO_PIPE_OUTPUT_BUFFER)
        {
          amount = count - pipe->size;
          pipe->state = GDK_IO_PIPE_EMPTY;
          pipe->size = 0;
          if (pipe->input_closed && amount == 0)
            amount = count;
        }
      else
        {
          amount = count;
        }
      break;

    case GDK_IO_PIPE_INPUT_BUFFER:
      amount = MIN (count, pipe->size);
      
      memcpy (pipe->buffer, buffer, amount);
      pipe->size -= amount;

      if (pipe->size == 0)
        pipe->state = GDK_IO_PIPE_EMPTY;
      else
        pipe->buffer += amount;
      break;

    case GDK_IO_PIPE_OUTPUT_BUFFER:
    default:
      g_assert_not_reached ();
      amount = 0;
      break;
  }
    
  g_cond_broadcast (&pipe->cond);
  gdk_io_pipe_unlock (pipe);

  return amount;
}

static gboolean
gdk_pipe_output_stream_close (GOutputStream  *stream,
                              GCancellable  *cancellable,
                              GError       **error)
{
  GdkPipeOutputStream *pipe_stream = GDK_PIPE_OUTPUT_STREAM (stream);
  GdkIOPipe *pipe = pipe_stream->pipe;

  gdk_io_pipe_lock (pipe);

  pipe->output_closed = TRUE;
    
  g_cond_broadcast (&pipe->cond);
  gdk_io_pipe_unlock (pipe);

  return TRUE;
}

static void
gdk_pipe_output_stream_class_init (GdkPipeOutputStreamClass *class)
{
  GObjectClass *object_class = G_OBJECT_CLASS (class);
  GOutputStreamClass *output_stream_class = G_OUTPUT_STREAM_CLASS (class);

  object_class->finalize = gdk_pipe_output_stream_finalize;

  output_stream_class->write_fn = gdk_pipe_output_stream_write;
  output_stream_class->close_fn = gdk_pipe_output_stream_close;
}

static void
gdk_pipe_output_stream_init (GdkPipeOutputStream *pipe)
{
}

/* IOSTREAM */

#define GDK_TYPE_PIPE_IO_STREAM (gdk_pipe_io_stream_get_type ())
GDK_DECLARE_INTERNAL_TYPE (GdkPipeIOStream, gdk_pipe_io_stream, GDK, PIPE_IO_STREAM, GIOStream)

struct _GdkPipeIOStream
{
  GIOStream parent;

  GInputStream *input_stream;
  GOutputStream *output_stream;
  GdkIOPipe *pipe;
};

struct _GdkPipeIOStreamClass
{
  GIOStreamClass parent_class;
};

G_DEFINE_TYPE (GdkPipeIOStream, gdk_pipe_io_stream, G_TYPE_IO_STREAM)

static void
gdk_pipe_io_stream_finalize (GObject *object)
{
  GdkPipeIOStream *pipe = GDK_PIPE_IO_STREAM (object);

  g_clear_object (&pipe->input_stream);
  g_clear_object (&pipe->output_stream);
  g_clear_pointer (&pipe->pipe, gdk_io_pipe_unref);

  G_OBJECT_CLASS (gdk_pipe_io_stream_parent_class)->finalize (object);
}

static GInputStream *
gdk_pipe_io_stream_get_input_stream (GIOStream *stream)
{
  GdkPipeIOStream *pipe = GDK_PIPE_IO_STREAM (stream);

  return pipe->input_stream;
}

static GOutputStream *
gdk_pipe_io_stream_get_output_stream (GIOStream *stream)
{
  GdkPipeIOStream *pipe = GDK_PIPE_IO_STREAM (stream);

  return pipe->output_stream;
}

static gboolean
gdk_pipe_io_stream_close (GIOStream     *stream,
                          GCancellable  *cancellable,
                          GError       **error)
{
  /* overwrite so we don't close the 2 streams */
  return TRUE;
}

static void
gdk_pipe_io_stream_class_init (GdkPipeIOStreamClass *class)
{
  GObjectClass *object_class = G_OBJECT_CLASS (class);
  GIOStreamClass *io_class = G_IO_STREAM_CLASS (class);

  object_class->finalize = gdk_pipe_io_stream_finalize;

  io_class->get_input_stream = gdk_pipe_io_stream_get_input_stream;
  io_class->get_output_stream = gdk_pipe_io_stream_get_output_stream;
  io_class->close_fn = gdk_pipe_io_stream_close;
}

static void
gdk_pipe_io_stream_init (GdkPipeIOStream *pipe)
{
  pipe->pipe = gdk_io_pipe_new ();

  pipe->input_stream = g_object_new (GDK_TYPE_PIPE_INPUT_STREAM, NULL);
  GDK_PIPE_INPUT_STREAM (pipe->input_stream)->pipe = gdk_io_pipe_ref (pipe->pipe);

  pipe->output_stream = g_object_new (GDK_TYPE_PIPE_OUTPUT_STREAM, NULL);
  GDK_PIPE_OUTPUT_STREAM (pipe->output_stream)->pipe = gdk_io_pipe_ref (pipe->pipe);
}

/**
 * gdk_pipe_io_stream_new:
 *
 * Creates a `GIOStream` whose input- and output-stream behave like a pipe.
 *
 * Data written into the output stream becomes available for reading on
 * the input stream.
 *
 * Note that this is data transfer in the opposite direction to
 * g_output_stream_splice().
 *
 * Returns: a new `GIOStream`
 */
GIOStream *
gdk_pipe_io_stream_new (void)
{
  return g_object_new (GDK_TYPE_PIPE_IO_STREAM, NULL);
}

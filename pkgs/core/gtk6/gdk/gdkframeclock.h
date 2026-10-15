/* GDK - The GIMP Drawing Kit
 * Copyright (C) 1995-1997 Peter Mattis, Spencer Kimball and Josh MacDonald
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
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 */

/*
 * Modified by the GTK+ Team and others 1997-2010.  See the AUTHORS
 * file for a list of people on the GTK+ Team.  See the ChangeLog
 * files for a list of changes.  These files are distributed with
 * GTK+ at ftp://ftp.gtk.org/pub/gtk/.
 */

#pragma once

#if !defined (__GDK_H_INSIDE__) && !defined (GTK_COMPILATION)
#error "Only <gdk/gdk.h> can be included directly."
#endif

#include <gdk/gdkframetimings.h>

G_BEGIN_DECLS

#define GDK_TYPE_FRAME_CLOCK            (gdk_frame_clock_get_type ())

GDK_AVAILABLE_IN_ALL
GDK_DECLARE_INTERNAL_TYPE (GdkFrameClock, gdk_frame_clock, GDK, FRAME_CLOCK, GObject)

/**
 * GdkFrameClockPhase:
 * @GDK_FRAME_CLOCK_PHASE_NONE: no phase
 * @GDK_FRAME_CLOCK_PHASE_FLUSH_EVENTS: corresponds to GdkFrameClock::flush-events. Should not be handled by applications.
 * @GDK_FRAME_CLOCK_PHASE_BEFORE_PAINT: corresponds to GdkFrameClock::before-paint. Should not be handled by applications.
 * @GDK_FRAME_CLOCK_PHASE_UPDATE: corresponds to GdkFrameClock::update.
 * @GDK_FRAME_CLOCK_PHASE_LAYOUT: corresponds to GdkFrameClock::layout. Should not be handled by applications.
 * @GDK_FRAME_CLOCK_PHASE_PAINT: corresponds to GdkFrameClock::paint.
 * @GDK_FRAME_CLOCK_PHASE_RESUME_EVENTS: corresponds to GdkFrameClock::resume-events. Should not be handled by applications.
 * @GDK_FRAME_CLOCK_PHASE_AFTER_PAINT: corresponds to GdkFrameClock::after-paint. Should not be handled by applications.
 *
 * Used to represent the different paint clock phases that can be requested.
 *
 * The elements of the enumeration correspond to the signals of `GdkFrameClock`.
 */
typedef enum {
  GDK_FRAME_CLOCK_PHASE_NONE          = 0,
  GDK_FRAME_CLOCK_PHASE_FLUSH_EVENTS  = 1 << 0,
  GDK_FRAME_CLOCK_PHASE_BEFORE_PAINT  = 1 << 1,
  GDK_FRAME_CLOCK_PHASE_UPDATE        = 1 << 2,
  GDK_FRAME_CLOCK_PHASE_LAYOUT        = 1 << 3,
  GDK_FRAME_CLOCK_PHASE_PAINT         = 1 << 4,
  GDK_FRAME_CLOCK_PHASE_RESUME_EVENTS = 1 << 5,
  GDK_FRAME_CLOCK_PHASE_AFTER_PAINT   = 1 << 6
} GdkFrameClockPhase;

GDK_AVAILABLE_IN_ALL
gint64             gdk_frame_clock_get_frame_time               (GdkFrameClock          *self);

GDK_AVAILABLE_IN_ALL
void               gdk_frame_clock_request_phase                (GdkFrameClock          *self,
                                                                 GdkFrameClockPhase      phase);

GDK_AVAILABLE_IN_ALL
void               gdk_frame_clock_begin_updating (GdkFrameClock      *self);
GDK_AVAILABLE_IN_ALL
void               gdk_frame_clock_end_updating   (GdkFrameClock      *self);

/* Frame history */
GDK_AVAILABLE_IN_ALL
gint64           gdk_frame_clock_get_frame_counter (GdkFrameClock *frame_clock);
GDK_AVAILABLE_IN_ALL
gint64           gdk_frame_clock_get_history_start (GdkFrameClock *frame_clock);
GDK_AVAILABLE_IN_ALL
GdkFrameTimings *gdk_frame_clock_get_timings       (GdkFrameClock *frame_clock,
                                                    gint64         frame_counter);

GDK_AVAILABLE_IN_ALL
GdkFrameTimings *gdk_frame_clock_get_current_timings (GdkFrameClock *frame_clock);

GDK_AVAILABLE_IN_ALL
void gdk_frame_clock_get_refresh_info (GdkFrameClock *frame_clock,
                                       gint64         base_time,
                                       gint64        *refresh_interval_return,
                                       gint64        *presentation_time_return);

GDK_AVAILABLE_IN_ALL
double gdk_frame_clock_get_fps (GdkFrameClock *frame_clock);

G_END_DECLS

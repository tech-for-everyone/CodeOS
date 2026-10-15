Title: GIOScheduler
SPDX-License-Identifier: GPL-3.0-or-later
SPDX-FileCopyrightText: 2007 Andrew Walton

# GIOScheduler

Schedules asynchronous I/O operations. `GIOScheduler` integrates
into the main event loop ([struct@GLib.MainLoop]) and uses threads.

Deprecated: 2.36: As of GLib 2.36, `GIOScheduler` is deprecated in favor of
[struct@GLib.ThreadPool] and [class@Gio.Task].

The `GIOScheduler` API is:
 * [type@Gio.IOSchedulerJobFunc]
 * [func@Gio.io_scheduler_push_job]
 * [func@Gio.io_scheduler_cancel_all_jobs]
 * [type@Gio.IOSchedulerJob]
 * [method@Gio.IOSchedulerJob.send_to_mainloop]
 * [method@Gio.IOSchedulerJob.send_to_mainloop_async]


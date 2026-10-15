/* GLib testing framework examples and tests
 *
 * Copyright © 2009 Canonical Limited
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License as published by the Free Software Foundation; either
 * version 3 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General
 * Public License along with this library; if not, see <http://www.gnu.org/licenses/>.
 */

#include "config.h"
#include <glib.h>

int
main (int    argc,
      char **argv)
{
  /* We expect this test to abort, so try to avoid that creating a coredump */
  g_test_disable_crash_reporting ();

  g_assert (42 < 0);
  return 0;
}

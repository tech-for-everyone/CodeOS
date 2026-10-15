/*
 * Copyright © 2025 Luca Bacci
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
 * You should have received a copy of the GNU General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 *
 * Author: Luca Bacci <luca.bacci@outlook.com>
 */

#ifndef __G_PRINTPRIVATE_H__
#define __G_PRINTPRIVATE_H__

#include <glib.h>
#include <stdio.h>
#include <stdarg.h>

G_BEGIN_DECLS

int
g_fputs (const char *string,
         FILE       *stream);

char *
g_print_convert (const char *string,
                 const char *charset);

G_END_DECLS

#endif /* __G_PRINTPRIVATE_H__ */

/* GdkText - Internationalized text layout and rendering library
 * Copyright (C) 1999 Red Hat Software
 * Copyright (C) 2012 Ryan Lortie, Matthias Clasen and Emmanuele Bassi
 * Copyright (C) 2016 Chun-wei Fan
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 */

#ifndef __PANGO_VERSION_H__
#define __PANGO_VERSION_H__

#include <pango/pango-features.h>

#include <glib.h>

#ifndef _PANGO_EXTERN
#define _PANGO_EXTERN extern
#endif

#define GDK_TEXT_AVAILABLE_IN_ALL                   _PANGO_EXTERN

/* XXX: Every new stable minor release bump should add a macro here */

/**
 * GDK_TEXT_VERSION_1_2:
 *
 * A macro that evaluates to the 1.2 version of GdkText, in a format
 * that can be used by the C pre-processor.
 *
 * Since: 1.42
 */
#define GDK_TEXT_VERSION_1_2        (G_ENCODE_VERSION (1, 2))

/**
 * GDK_TEXT_VERSION_1_4:
 *
 * A macro that evaluates to the 1.4 version of GdkText, in a format
 * that can be used by the C pre-processor.
 *
 * Since: 1.42
 */
#define GDK_TEXT_VERSION_1_4        (G_ENCODE_VERSION (1, 4))

/**
 * GDK_TEXT_VERSION_1_6:
 *
 * A macro that evaluates to the 1.6 version of GdkText, in a format
 * that can be used by the C pre-processor.
 *
 * Since: 1.42
 */
#define GDK_TEXT_VERSION_1_6        (G_ENCODE_VERSION (1, 6))

/**
 * GDK_TEXT_VERSION_1_8:
 *
 * A macro that evaluates to the 1.8 version of GdkText, in a format
 * that can be used by the C pre-processor.
 *
 * Since: 1.42
 */
#define GDK_TEXT_VERSION_1_8        (G_ENCODE_VERSION (1, 8))

/**
 * GDK_TEXT_VERSION_1_10:
 *
 * A macro that evaluates to the 1.10 version of GdkText, in a format
 * that can be used by the C pre-processor.
 *
 * Since: 1.42
 */
#define GDK_TEXT_VERSION_1_10       (G_ENCODE_VERSION (1, 10))

/**
 * GDK_TEXT_VERSION_1_12:
 *
 * A macro that evaluates to the 1.12 version of GdkText, in a format
 * that can be used by the C pre-processor.
 *
 * Since: 1.42
 */
#define GDK_TEXT_VERSION_1_12       (G_ENCODE_VERSION (1, 12))

/**
 * GDK_TEXT_VERSION_1_14:
 *
 * A macro that evaluates to the 1.14 version of GdkText, in a format
 * that can be used by the C pre-processor.
 *
 * Since: 1.42
 */
#define GDK_TEXT_VERSION_1_14       (G_ENCODE_VERSION (1, 14))

/**
 * GDK_TEXT_VERSION_1_16:
 *
 * A macro that evaluates to the 1.16 version of GdkText, in a format
 * that can be used by the C pre-processor.
 *
 * Since: 1.42
 */
#define GDK_TEXT_VERSION_1_16       (G_ENCODE_VERSION (1, 16))

/**
 * GDK_TEXT_VERSION_1_18:
 *
 * A macro that evaluates to the 1.18 version of GdkText, in a format
 * that can be used by the C pre-processor.
 *
 * Since: 1.42
 */
#define GDK_TEXT_VERSION_1_18       (G_ENCODE_VERSION (1, 18))

/**
 * GDK_TEXT_VERSION_1_20:
 *
 * A macro that evaluates to the 1.20 version of GdkText, in a format
 * that can be used by the C pre-processor.
 *
 * Since: 1.42
 */
#define GDK_TEXT_VERSION_1_20       (G_ENCODE_VERSION (1, 20))

/**
 * GDK_TEXT_VERSION_1_22:
 *
 * A macro that evaluates to the 1.22 version of GdkText, in a format
 * that can be used by the C pre-processor.
 *
 * Since: 1.42
 */
#define GDK_TEXT_VERSION_1_22       (G_ENCODE_VERSION (1, 22))

/**
 * GDK_TEXT_VERSION_1_24:
 *
 * A macro that evaluates to the 1.24 version of GdkText, in a format
 * that can be used by the C pre-processor.
 *
 * Since: 1.42
 */
#define GDK_TEXT_VERSION_1_24       (G_ENCODE_VERSION (1, 24))

/**
 * GDK_TEXT_VERSION_1_26:
 *
 * A macro that evaluates to the 1.26 version of GdkText, in a format
 * that can be used by the C pre-processor.
 *
 * Since: 1.42
 */
#define GDK_TEXT_VERSION_1_26       (G_ENCODE_VERSION (1, 26))

/**
 * GDK_TEXT_VERSION_1_28:
 *
 * A macro that evaluates to the 1.28 version of GdkText, in a format
 * that can be used by the C pre-processor.
 *
 * Since: 1.42
 */
#define GDK_TEXT_VERSION_1_28       (G_ENCODE_VERSION (1, 28))

/**
 * GDK_TEXT_VERSION_1_30:
 *
 * A macro that evaluates to the 1.30 version of GdkText, in a format
 * that can be used by the C pre-processor.
 *
 * Since: 1.42
 */
#define GDK_TEXT_VERSION_1_30       (G_ENCODE_VERSION (1, 30))

/**
 * GDK_TEXT_VERSION_1_32:
 *
 * A macro that evaluates to the 1.32 version of GdkText, in a format
 * that can be used by the C pre-processor.
 *
 * Since: 1.42
 */
#define GDK_TEXT_VERSION_1_32       (G_ENCODE_VERSION (1, 32))

/**
 * GDK_TEXT_VERSION_1_34:
 *
 * A macro that evaluates to the 1.34 version of GdkText, in a format
 * that can be used by the C pre-processor.
 *
 * Since: 1.42
 */
#define GDK_TEXT_VERSION_1_34       (G_ENCODE_VERSION (1, 34))

/**
 * GDK_TEXT_VERSION_1_36:
 *
 * A macro that evaluates to the 1.36 version of GdkText, in a format
 * that can be used by the C pre-processor.
 *
 * Since: 1.42
 */
#define GDK_TEXT_VERSION_1_36       (G_ENCODE_VERSION (1, 36))

/**
 * GDK_TEXT_VERSION_1_38:
 *
 * A macro that evaluates to the 1.38 version of GdkText, in a format
 * that can be used by the C pre-processor.
 *
 * Since: 1.42
 */
#define GDK_TEXT_VERSION_1_38       (G_ENCODE_VERSION (1, 38))

/**
 * GDK_TEXT_VERSION_1_40:
 *
 * A macro that evaluates to the 1.40 version of GdkText, in a format
 * that can be used by the C pre-processor.
 *
 * Since: 1.42
 */
#define GDK_TEXT_VERSION_1_40       (G_ENCODE_VERSION (1, 40))

/**
 * GDK_TEXT_VERSION_1_42:
 *
 * A macro that evaluates to the 1.42 version of GdkText, in a format
 * that can be used by the C pre-processor.
 *
 * Since: 1.42
 */
#define GDK_TEXT_VERSION_1_42       (G_ENCODE_VERSION (1, 42))

/**
 * GDK_TEXT_VERSION_1_44:
 *
 * A macro that evaluates to the 1.44 version of GdkText, in a format
 * that can be used by the C pre-processor.
 *
 * Since: 1.44
 */
#define GDK_TEXT_VERSION_1_44       (G_ENCODE_VERSION (1, 44))

/**
 * GDK_TEXT_VERSION_1_46:
 *
 * A macro that evaluates to the 1.46 version of GdkText, in a format
 * that can be used by the C pre-processor.
 *
 * Since: 1.46
 */
#define GDK_TEXT_VERSION_1_46       (G_ENCODE_VERSION (1, 46))

/**
 * GDK_TEXT_VERSION_1_48:
 *
 * A macro that evaluates to the 1.48 version of GdkText, in a format
 * that can be used by the C pre-processor.
 *
 * Since: 1.48
 */
#define GDK_TEXT_VERSION_1_48       (G_ENCODE_VERSION (1, 48))

/**
 * GDK_TEXT_VERSION_1_50:
 *
 * A macro that evaluates to the 1.50 version of GdkText, in a format
 * that can be used by the C pre-processor.
 *
 * Since: 1.50
 */
#define GDK_TEXT_VERSION_1_50       (G_ENCODE_VERSION (1, 50))

/**
 * GDK_TEXT_VERSION_1_52:
 *
 * A macro that evaluates to the 1.52 version of GdkText, in a format
 * that can be used by the C pre-processor.
 *
 * Since: 1.52
 */
#define GDK_TEXT_VERSION_1_52       (G_ENCODE_VERSION (1, 52))

/**
 * GDK_TEXT_VERSION_1_54:
 *
 * A macro that evaluates to the 1.54 version of GdkText, in a format
 * that can be used by the C pre-processor.
 *
 * Since: 1.54
 */
#define GDK_TEXT_VERSION_1_54       (G_ENCODE_VERSION (1, 54))

/**
 * GDK_TEXT_VERSION_1_55:
 *
 * A macro that evaluates to the 1.55 version of GdkText, in a format
 * that can be used by the C pre-processor.
 *
 * Since: 1.55
 */
#define GDK_TEXT_VERSION_1_55       (G_ENCODE_VERSION (1, 55))

/**
 * GDK_TEXT_VERSION_1_56:
 *
 * A macro that evaluates to the 1.56 version of GdkText, in a format
 * that can be used by the C pre-processor.
 *
 * Since: 1.56
 */
#define GDK_TEXT_VERSION_1_56       (G_ENCODE_VERSION (1, 56))

/**
 * GDK_TEXT_VERSION_1_57:
 *
 * A macro that evaluates to the 1.57 version of GdkText, in a format
 * that can be used by the C pre-processor.
 *
 * Since: 1.57
 */
#define GDK_TEXT_VERSION_1_57       (G_ENCODE_VERSION (1, 57))

/**
 * GDK_TEXT_VERSION_1_58:
 *
 * A macro that evaluates to the 1.58 version of GdkText, in a format
 * that can be used by the C pre-processor.
 *
 * Since: 1.58
 */
#define GDK_TEXT_VERSION_1_58       (G_ENCODE_VERSION (1, 58))

/* evaluates to the current stable version; for development cycles,
 * this means the next stable target
 */
#if (GDK_TEXT_VERSION_MINOR % 2)
#define GDK_TEXT_VERSION_CUR_STABLE         (G_ENCODE_VERSION (GDK_TEXT_VERSION_MAJOR, GDK_TEXT_VERSION_MINOR + 1))
#else
#define GDK_TEXT_VERSION_CUR_STABLE         (G_ENCODE_VERSION (GDK_TEXT_VERSION_MAJOR, GDK_TEXT_VERSION_MINOR))
#endif

/* evaluates to the previous stable version */
#if (GDK_TEXT_VERSION_MINOR % 2)
#define GDK_TEXT_VERSION_PREV_STABLE        (G_ENCODE_VERSION (GDK_TEXT_VERSION_MAJOR, GDK_TEXT_VERSION_MINOR - 1))
#else
#define GDK_TEXT_VERSION_PREV_STABLE        (G_ENCODE_VERSION (GDK_TEXT_VERSION_MAJOR, GDK_TEXT_VERSION_MINOR - 2))
#endif

/**
 * GDK_TEXT_VERSION_MIN_REQUIRED:
 *
 * A macro that should be defined by the user prior to including
 * the pango.h header.
 * The definition should be one of the predefined GdkText version
 * macros: %GDK_TEXT_VERSION_1_2, %GDK_TEXT_VERSION_1_4,...
 *
 * This macro defines the earliest version of GdkText that the package is
 * required to be able to compile against.
 *
 * If the compiler is configured to warn about the use of deprecated
 * functions, then using functions that were deprecated in version
 * %GDK_TEXT_VERSION_MIN_REQUIRED or earlier will cause warnings (but
 * using functions deprecated in later releases will not).
 *
 * Since: 1.42
 */
/* If the package sets GDK_TEXT_VERSION_MIN_REQUIRED to some future
 * GDK_TEXT_VERSION_X_Y value that we don't know about, it will compare as
 * 0 in preprocessor tests.
 */
#ifndef GDK_TEXT_VERSION_MIN_REQUIRED
# define GDK_TEXT_VERSION_MIN_REQUIRED      (GDK_TEXT_VERSION_CUR_STABLE)
#elif GDK_TEXT_VERSION_MIN_REQUIRED == 0
# undef  GDK_TEXT_VERSION_MIN_REQUIRED
# define GDK_TEXT_VERSION_MIN_REQUIRED      (GDK_TEXT_VERSION_CUR_STABLE + 2)
#endif

/**
 * GDK_TEXT_VERSION_MAX_ALLOWED:
 *
 * A macro that should be defined by the user prior to including
 * the glib.h header.
 * The definition should be one of the predefined GdkText version
 * macros: %GDK_TEXT_VERSION_1_2, %GDK_TEXT_VERSION_1_4,...
 *
 * This macro defines the latest version of the GdkText API that the
 * package is allowed to make use of.
 *
 * If the compiler is configured to warn about the use of deprecated
 * functions, then using functions added after version
 * %GDK_TEXT_VERSION_MAX_ALLOWED will cause warnings.
 *
 * Unless you are using GDK_TEXT_VERSION_CHECK() or the like to compile
 * different code depending on the GdkText version, then this should be
 * set to the same value as %GDK_TEXT_VERSION_MIN_REQUIRED.
 *
 * Since: 1.42
 */
#if !defined (GDK_TEXT_VERSION_MAX_ALLOWED) || (GDK_TEXT_VERSION_MAX_ALLOWED == 0)
# undef GDK_TEXT_VERSION_MAX_ALLOWED
# define GDK_TEXT_VERSION_MAX_ALLOWED      (GDK_TEXT_VERSION_CUR_STABLE)
#endif

/* sanity checks */
#if GDK_TEXT_VERSION_MIN_REQUIRED > GDK_TEXT_VERSION_CUR_STABLE
#error "GDK_TEXT_VERSION_MIN_REQUIRED must be <= GDK_TEXT_VERSION_CUR_STABLE"
#endif
#if GDK_TEXT_VERSION_MAX_ALLOWED < GDK_TEXT_VERSION_MIN_REQUIRED
#error "GDK_TEXT_VERSION_MAX_ALLOWED must be >= GDK_TEXT_VERSION_MIN_REQUIRED"
#endif
#if GDK_TEXT_VERSION_MIN_REQUIRED < GDK_TEXT_VERSION_1_2
#error "GDK_TEXT_VERSION_MIN_REQUIRED must be >= GDK_TEXT_VERSION_1_2"
#endif

/* These macros are used to mark deprecated functions in GdkText headers,
 * and thus have to be exposed in installed headers.
 */
#ifdef GDK_TEXT_DISABLE_DEPRECATION_WARNINGS
# define GDK_TEXT_DEPRECATED                       _PANGO_EXTERN
# define GDK_TEXT_DEPRECATED_FOR(f)                _PANGO_EXTERN
# define GDK_TEXT_UNAVAILABLE(maj,min)             _PANGO_EXTERN
#else
# define GDK_TEXT_DEPRECATED                       G_DEPRECATED _PANGO_EXTERN
# define GDK_TEXT_DEPRECATED_FOR(f)                G_DEPRECATED_FOR(f) _PANGO_EXTERN
# define GDK_TEXT_UNAVAILABLE(maj,min)             G_UNAVAILABLE(maj,min) _PANGO_EXTERN
#endif

/* XXX: Every new stable minor release should add a set of macros here */
#if GDK_TEXT_VERSION_MIN_REQUIRED >= GDK_TEXT_VERSION_1_2
# define GDK_TEXT_DEPRECATED_IN_1_2                GDK_TEXT_DEPRECATED
# define GDK_TEXT_DEPRECATED_IN_1_2_FOR(f)         GDK_TEXT_DEPRECATED_FOR(f)
#else
# define GDK_TEXT_DEPRECATED_IN_1_2                _PANGO_EXTERN
# define GDK_TEXT_DEPRECATED_IN_1_2_FOR(f)         _PANGO_EXTERN
#endif

#if GDK_TEXT_VERSION_MAX_ALLOWED < GDK_TEXT_VERSION_1_2
# define GDK_TEXT_AVAILABLE_IN_1_2                 GDK_TEXT_UNAVAILABLE(1, 2)
#else
# define GDK_TEXT_AVAILABLE_IN_1_2                 _PANGO_EXTERN
#endif

#if GDK_TEXT_VERSION_MIN_REQUIRED >= GDK_TEXT_VERSION_1_4
# define GDK_TEXT_DEPRECATED_IN_1_4                GDK_TEXT_DEPRECATED
# define GDK_TEXT_DEPRECATED_IN_1_4_FOR(f)         GDK_TEXT_DEPRECATED_FOR(f)
#else
# define GDK_TEXT_DEPRECATED_IN_1_4                _PANGO_EXTERN
# define GDK_TEXT_DEPRECATED_IN_1_4_FOR(f)         _PANGO_EXTERN
#endif

#if GDK_TEXT_VERSION_MAX_ALLOWED < GDK_TEXT_VERSION_1_4
# define GDK_TEXT_AVAILABLE_IN_1_4                 GDK_TEXT_UNAVAILABLE(1, 4)
#else
# define GDK_TEXT_AVAILABLE_IN_1_4                 _PANGO_EXTERN
#endif


#if GDK_TEXT_VERSION_MIN_REQUIRED >= GDK_TEXT_VERSION_1_6
# define GDK_TEXT_DEPRECATED_IN_1_6                GDK_TEXT_DEPRECATED
# define GDK_TEXT_DEPRECATED_IN_1_6_FOR(f)         GDK_TEXT_DEPRECATED_FOR(f)
#else
# define GDK_TEXT_DEPRECATED_IN_1_6                _PANGO_EXTERN
# define GDK_TEXT_DEPRECATED_IN_1_6_FOR(f)         _PANGO_EXTERN
#endif

#if GDK_TEXT_VERSION_MAX_ALLOWED < GDK_TEXT_VERSION_1_6
# define GDK_TEXT_AVAILABLE_IN_1_6                 GDK_TEXT_UNAVAILABLE(1, 6)
#else
# define GDK_TEXT_AVAILABLE_IN_1_6                 _PANGO_EXTERN
#endif

#if GDK_TEXT_VERSION_MIN_REQUIRED >= GDK_TEXT_VERSION_1_8
# define GDK_TEXT_DEPRECATED_IN_1_8                GDK_TEXT_DEPRECATED
# define GDK_TEXT_DEPRECATED_IN_1_8_FOR(f)         GDK_TEXT_DEPRECATED_FOR(f)
#else
# define GDK_TEXT_DEPRECATED_IN_1_8                _PANGO_EXTERN
# define GDK_TEXT_DEPRECATED_IN_1_8_FOR(f)         _PANGO_EXTERN
#endif

#if GDK_TEXT_VERSION_MAX_ALLOWED < GDK_TEXT_VERSION_1_8
# define GDK_TEXT_AVAILABLE_IN_1_8                 GDK_TEXT_UNAVAILABLE(1, 8)
#else
# define GDK_TEXT_AVAILABLE_IN_1_8                 _PANGO_EXTERN
#endif

#if GDK_TEXT_VERSION_MIN_REQUIRED >= GDK_TEXT_VERSION_1_10
# define GDK_TEXT_DEPRECATED_IN_1_10               GDK_TEXT_DEPRECATED
# define GDK_TEXT_DEPRECATED_IN_1_10_FOR(f)        GDK_TEXT_DEPRECATED_FOR(f)
#else
# define GDK_TEXT_DEPRECATED_IN_1_10               _PANGO_EXTERN
# define GDK_TEXT_DEPRECATED_IN_1_10_FOR(f)        _PANGO_EXTERN
#endif

#if GDK_TEXT_VERSION_MAX_ALLOWED < GDK_TEXT_VERSION_1_10
# define GDK_TEXT_AVAILABLE_IN_1_10                GDK_TEXT_UNAVAILABLE(1, 10)
#else
# define GDK_TEXT_AVAILABLE_IN_1_10                _PANGO_EXTERN
#endif

#if GDK_TEXT_VERSION_MAX_ALLOWED < GDK_TEXT_VERSION_1_12
# define GDK_TEXT_AVAILABLE_IN_1_12                GDK_TEXT_UNAVAILABLE(1, 12)
#else
# define GDK_TEXT_AVAILABLE_IN_1_12                _PANGO_EXTERN
#endif

#if GDK_TEXT_VERSION_MIN_REQUIRED >= GDK_TEXT_VERSION_1_12
# define GDK_TEXT_DEPRECATED_IN_1_12               GDK_TEXT_DEPRECATED
# define GDK_TEXT_DEPRECATED_IN_1_12_FOR(f)        GDK_TEXT_DEPRECATED_FOR(f)
#else
# define GDK_TEXT_DEPRECATED_IN_1_12               _PANGO_EXTERN
# define GDK_TEXT_DEPRECATED_IN_1_12_FOR(f)        _PANGO_EXTERN
#endif

#if GDK_TEXT_VERSION_MAX_ALLOWED < GDK_TEXT_VERSION_1_14
# define GDK_TEXT_AVAILABLE_IN_1_14                GDK_TEXT_UNAVAILABLE(1, 14)
#else
# define GDK_TEXT_AVAILABLE_IN_1_14                _PANGO_EXTERN
#endif

#if GDK_TEXT_VERSION_MIN_REQUIRED >= GDK_TEXT_VERSION_1_14
# define GDK_TEXT_DEPRECATED_IN_1_14               GDK_TEXT_DEPRECATED
# define GDK_TEXT_DEPRECATED_IN_1_14_FOR(f)        GDK_TEXT_DEPRECATED_FOR(f)
#else
# define GDK_TEXT_DEPRECATED_IN_1_14               _PANGO_EXTERN
# define GDK_TEXT_DEPRECATED_IN_1_14_FOR(f)        _PANGO_EXTERN
#endif

#if GDK_TEXT_VERSION_MAX_ALLOWED < GDK_TEXT_VERSION_1_16
# define GDK_TEXT_AVAILABLE_IN_1_16                GDK_TEXT_UNAVAILABLE(1, 16)
#else
# define GDK_TEXT_AVAILABLE_IN_1_16                _PANGO_EXTERN
#endif

#if GDK_TEXT_VERSION_MIN_REQUIRED >= GDK_TEXT_VERSION_1_16
# define GDK_TEXT_DEPRECATED_IN_1_16               GDK_TEXT_DEPRECATED
# define GDK_TEXT_DEPRECATED_IN_1_16_FOR(f)        GDK_TEXT_DEPRECATED_FOR(f)
#else
# define GDK_TEXT_DEPRECATED_IN_1_16               _PANGO_EXTERN
# define GDK_TEXT_DEPRECATED_IN_1_16_FOR(f)        _PANGO_EXTERN
#endif

#if GDK_TEXT_VERSION_MIN_REQUIRED >= GDK_TEXT_VERSION_1_18
# define GDK_TEXT_DEPRECATED_IN_1_18               GDK_TEXT_DEPRECATED
# define GDK_TEXT_DEPRECATED_IN_1_18_FOR(f)        GDK_TEXT_DEPRECATED_FOR(f)
#else
# define GDK_TEXT_DEPRECATED_IN_1_18               _PANGO_EXTERN
# define GDK_TEXT_DEPRECATED_IN_1_18_FOR(f)        _PANGO_EXTERN
#endif

#if GDK_TEXT_VERSION_MAX_ALLOWED < GDK_TEXT_VERSION_1_18
# define GDK_TEXT_AVAILABLE_IN_1_18                GDK_TEXT_UNAVAILABLE(1, 18)
#else
# define GDK_TEXT_AVAILABLE_IN_1_18                _PANGO_EXTERN
#endif

#if GDK_TEXT_VERSION_MIN_REQUIRED >= GDK_TEXT_VERSION_1_20
# define GDK_TEXT_DEPRECATED_IN_1_20               GDK_TEXT_DEPRECATED
# define GDK_TEXT_DEPRECATED_IN_1_20_FOR(f)        GDK_TEXT_DEPRECATED_FOR(f)
#else
# define GDK_TEXT_DEPRECATED_IN_1_20               _PANGO_EXTERN
# define GDK_TEXT_DEPRECATED_IN_1_20_FOR(f)        _PANGO_EXTERN
#endif

#if GDK_TEXT_VERSION_MAX_ALLOWED < GDK_TEXT_VERSION_1_20
# define GDK_TEXT_AVAILABLE_IN_1_20                GDK_TEXT_UNAVAILABLE(1, 20)
#else
# define GDK_TEXT_AVAILABLE_IN_1_20                _PANGO_EXTERN
#endif

#if GDK_TEXT_VERSION_MIN_REQUIRED >= GDK_TEXT_VERSION_1_22
# define GDK_TEXT_DEPRECATED_IN_1_22               GDK_TEXT_DEPRECATED
# define GDK_TEXT_DEPRECATED_IN_1_22_FOR(f)        GDK_TEXT_DEPRECATED_FOR(f)
#else
# define GDK_TEXT_DEPRECATED_IN_1_22               _PANGO_EXTERN
# define GDK_TEXT_DEPRECATED_IN_1_22_FOR(f)        _PANGO_EXTERN
#endif

#if GDK_TEXT_VERSION_MAX_ALLOWED < GDK_TEXT_VERSION_1_22
# define GDK_TEXT_AVAILABLE_IN_1_22                GDK_TEXT_UNAVAILABLE(1, 22)
#else
# define GDK_TEXT_AVAILABLE_IN_1_22                _PANGO_EXTERN
#endif

#if GDK_TEXT_VERSION_MIN_REQUIRED >= GDK_TEXT_VERSION_1_24
# define GDK_TEXT_DEPRECATED_IN_1_24               GDK_TEXT_DEPRECATED
# define GDK_TEXT_DEPRECATED_IN_1_24_FOR(f)        GDK_TEXT_DEPRECATED_FOR(f)
#else
# define GDK_TEXT_DEPRECATED_IN_1_24               _PANGO_EXTERN
# define GDK_TEXT_DEPRECATED_IN_1_24_FOR(f)        _PANGO_EXTERN
#endif

#if GDK_TEXT_VERSION_MAX_ALLOWED < GDK_TEXT_VERSION_1_24
# define GDK_TEXT_AVAILABLE_IN_1_24                GDK_TEXT_UNAVAILABLE(1, 24)
#else
# define GDK_TEXT_AVAILABLE_IN_1_24                _PANGO_EXTERN
#endif

#if GDK_TEXT_VERSION_MIN_REQUIRED >= GDK_TEXT_VERSION_1_26
# define GDK_TEXT_DEPRECATED_IN_1_26               GDK_TEXT_DEPRECATED
# define GDK_TEXT_DEPRECATED_IN_1_26_FOR(f)        GDK_TEXT_DEPRECATED_FOR(f)
#else
# define GDK_TEXT_DEPRECATED_IN_1_26               _PANGO_EXTERN
# define GDK_TEXT_DEPRECATED_IN_1_26_FOR(f)        _PANGO_EXTERN
#endif

#if GDK_TEXT_VERSION_MAX_ALLOWED < GDK_TEXT_VERSION_1_26
# define GDK_TEXT_AVAILABLE_IN_1_26                GDK_TEXT_UNAVAILABLE(1, 26)
#else
# define GDK_TEXT_AVAILABLE_IN_1_26                _PANGO_EXTERN
#endif

#if GDK_TEXT_VERSION_MIN_REQUIRED >= GDK_TEXT_VERSION_1_28
# define GDK_TEXT_DEPRECATED_IN_1_28               GDK_TEXT_DEPRECATED
# define GDK_TEXT_DEPRECATED_IN_1_28_FOR(f)        GDK_TEXT_DEPRECATED_FOR(f)
#else
# define GDK_TEXT_DEPRECATED_IN_1_28               _PANGO_EXTERN
# define GDK_TEXT_DEPRECATED_IN_1_28_FOR(f)        _PANGO_EXTERN
#endif

#if GDK_TEXT_VERSION_MAX_ALLOWED < GDK_TEXT_VERSION_1_28
# define GDK_TEXT_AVAILABLE_IN_1_28                GDK_TEXT_UNAVAILABLE(1, 28)
#else
# define GDK_TEXT_AVAILABLE_IN_1_28                _PANGO_EXTERN
#endif

#if GDK_TEXT_VERSION_MIN_REQUIRED >= GDK_TEXT_VERSION_1_30
# define GDK_TEXT_DEPRECATED_IN_1_30               GDK_TEXT_DEPRECATED
# define GDK_TEXT_DEPRECATED_IN_1_30_FOR(f)        GDK_TEXT_DEPRECATED_FOR(f)
#else
# define GDK_TEXT_DEPRECATED_IN_1_30               _PANGO_EXTERN
# define GDK_TEXT_DEPRECATED_IN_1_30_FOR(f)        _PANGO_EXTERN
#endif

#if GDK_TEXT_VERSION_MAX_ALLOWED < GDK_TEXT_VERSION_1_30
# define GDK_TEXT_AVAILABLE_IN_1_30                GDK_TEXT_UNAVAILABLE(1, 30)
#else
# define GDK_TEXT_AVAILABLE_IN_1_30                _PANGO_EXTERN
#endif

#if GDK_TEXT_VERSION_MIN_REQUIRED >= GDK_TEXT_VERSION_1_32
# define GDK_TEXT_DEPRECATED_IN_1_32               GDK_TEXT_DEPRECATED
# define GDK_TEXT_DEPRECATED_IN_1_32_FOR(f)        GDK_TEXT_DEPRECATED_FOR(f)
#else
# define GDK_TEXT_DEPRECATED_IN_1_32               _PANGO_EXTERN
# define GDK_TEXT_DEPRECATED_IN_1_32_FOR(f)        _PANGO_EXTERN
#endif

#if GDK_TEXT_VERSION_MAX_ALLOWED < GDK_TEXT_VERSION_1_32
# define GDK_TEXT_AVAILABLE_IN_1_32                GDK_TEXT_UNAVAILABLE(1, 32)
#else
# define GDK_TEXT_AVAILABLE_IN_1_32                _PANGO_EXTERN
#endif

#if GDK_TEXT_VERSION_MIN_REQUIRED >= GDK_TEXT_VERSION_1_34
# define GDK_TEXT_DEPRECATED_IN_1_34               GDK_TEXT_DEPRECATED
# define GDK_TEXT_DEPRECATED_IN_1_34_FOR(f)        GDK_TEXT_DEPRECATED_FOR(f)
#else
# define GDK_TEXT_DEPRECATED_IN_1_34               _PANGO_EXTERN
# define GDK_TEXT_DEPRECATED_IN_1_34_FOR(f)        _PANGO_EXTERN
#endif

#if GDK_TEXT_VERSION_MAX_ALLOWED < GDK_TEXT_VERSION_1_34
# define GDK_TEXT_AVAILABLE_IN_1_34                GDK_TEXT_UNAVAILABLE(1, 34)
#else
# define GDK_TEXT_AVAILABLE_IN_1_34                _PANGO_EXTERN
#endif

#if GDK_TEXT_VERSION_MIN_REQUIRED >= GDK_TEXT_VERSION_1_36
# define GDK_TEXT_DEPRECATED_IN_1_36               GDK_TEXT_DEPRECATED
# define GDK_TEXT_DEPRECATED_IN_1_36_FOR(f)        GDK_TEXT_DEPRECATED_FOR(f)
#else
# define GDK_TEXT_DEPRECATED_IN_1_36               _PANGO_EXTERN
# define GDK_TEXT_DEPRECATED_IN_1_36_FOR(f)        _PANGO_EXTERN
#endif

#if GDK_TEXT_VERSION_MAX_ALLOWED < GDK_TEXT_VERSION_1_36
# define GDK_TEXT_AVAILABLE_IN_1_36                GDK_TEXT_UNAVAILABLE(1, 36)
#else
# define GDK_TEXT_AVAILABLE_IN_1_36                _PANGO_EXTERN
#endif

#if GDK_TEXT_VERSION_MIN_REQUIRED >= GDK_TEXT_VERSION_1_38
# define GDK_TEXT_DEPRECATED_IN_1_38               GDK_TEXT_DEPRECATED
# define GDK_TEXT_DEPRECATED_IN_1_38_FOR(f)        GDK_TEXT_DEPRECATED_FOR(f)
#else
# define GDK_TEXT_DEPRECATED_IN_1_38               _PANGO_EXTERN
# define GDK_TEXT_DEPRECATED_IN_1_38_FOR(f)        _PANGO_EXTERN
#endif

#if GDK_TEXT_VERSION_MAX_ALLOWED < GDK_TEXT_VERSION_1_38
# define GDK_TEXT_AVAILABLE_IN_1_38                GDK_TEXT_UNAVAILABLE(1, 38)
#else
# define GDK_TEXT_AVAILABLE_IN_1_38                _PANGO_EXTERN
#endif

#if GDK_TEXT_VERSION_MIN_REQUIRED >= GDK_TEXT_VERSION_1_40
# define GDK_TEXT_DEPRECATED_IN_1_40               GDK_TEXT_DEPRECATED
# define GDK_TEXT_DEPRECATED_IN_1_40_FOR(f)        GDK_TEXT_DEPRECATED_FOR(f)
#else
# define GDK_TEXT_DEPRECATED_IN_1_40               _PANGO_EXTERN
# define GDK_TEXT_DEPRECATED_IN_1_40_FOR(f)        _PANGO_EXTERN
#endif

#if GDK_TEXT_VERSION_MAX_ALLOWED < GDK_TEXT_VERSION_1_40
# define GDK_TEXT_AVAILABLE_IN_1_40                GDK_TEXT_UNAVAILABLE(1, 40)
#else
# define GDK_TEXT_AVAILABLE_IN_1_40                _PANGO_EXTERN
#endif

#if GDK_TEXT_VERSION_MIN_REQUIRED >= GDK_TEXT_VERSION_1_42
# define GDK_TEXT_DEPRECATED_IN_1_42               GDK_TEXT_DEPRECATED
# define GDK_TEXT_DEPRECATED_IN_1_42_FOR(f)        GDK_TEXT_DEPRECATED_FOR(f)
#else
# define GDK_TEXT_DEPRECATED_IN_1_42               _PANGO_EXTERN
# define GDK_TEXT_DEPRECATED_IN_1_42_FOR(f)        _PANGO_EXTERN
#endif

#if GDK_TEXT_VERSION_MAX_ALLOWED < GDK_TEXT_VERSION_1_42
# define GDK_TEXT_AVAILABLE_IN_1_42                GDK_TEXT_UNAVAILABLE(1, 42)
#else
# define GDK_TEXT_AVAILABLE_IN_1_42                _PANGO_EXTERN
#endif

#if GDK_TEXT_VERSION_MIN_REQUIRED >= GDK_TEXT_VERSION_1_44
# define GDK_TEXT_DEPRECATED_IN_1_44               GDK_TEXT_DEPRECATED
# define GDK_TEXT_DEPRECATED_IN_1_44_FOR(f)        GDK_TEXT_DEPRECATED_FOR(f)
#else
# define GDK_TEXT_DEPRECATED_IN_1_44               _PANGO_EXTERN
# define GDK_TEXT_DEPRECATED_IN_1_44_FOR(f)        _PANGO_EXTERN
#endif

#if GDK_TEXT_VERSION_MAX_ALLOWED < GDK_TEXT_VERSION_1_44
# define GDK_TEXT_AVAILABLE_IN_1_44                GDK_TEXT_UNAVAILABLE(1, 44)
#else
# define GDK_TEXT_AVAILABLE_IN_1_44                _PANGO_EXTERN
#endif

#if GDK_TEXT_VERSION_MIN_REQUIRED >= GDK_TEXT_VERSION_1_46
# define GDK_TEXT_DEPRECATED_IN_1_46               GDK_TEXT_DEPRECATED
# define GDK_TEXT_DEPRECATED_IN_1_46_FOR(f)        GDK_TEXT_DEPRECATED_FOR(f)
#else
# define GDK_TEXT_DEPRECATED_IN_1_46               _PANGO_EXTERN
# define GDK_TEXT_DEPRECATED_IN_1_46_FOR(f)        _PANGO_EXTERN
#endif

#if GDK_TEXT_VERSION_MAX_ALLOWED < GDK_TEXT_VERSION_1_46
# define GDK_TEXT_AVAILABLE_IN_1_46                GDK_TEXT_UNAVAILABLE(1, 46)
#else
# define GDK_TEXT_AVAILABLE_IN_1_46                _PANGO_EXTERN
#endif

#if GDK_TEXT_VERSION_MIN_REQUIRED >= GDK_TEXT_VERSION_1_48
# define GDK_TEXT_DEPRECATED_IN_1_48               GDK_TEXT_DEPRECATED
# define GDK_TEXT_DEPRECATED_IN_1_48_FOR(f)        GDK_TEXT_DEPRECATED_FOR(f)
#else
# define GDK_TEXT_DEPRECATED_IN_1_48               _PANGO_EXTERN
# define GDK_TEXT_DEPRECATED_IN_1_48_FOR(f)        _PANGO_EXTERN
#endif

#if GDK_TEXT_VERSION_MAX_ALLOWED < GDK_TEXT_VERSION_1_48
# define GDK_TEXT_AVAILABLE_IN_1_48                GDK_TEXT_UNAVAILABLE(1, 48)
#else
# define GDK_TEXT_AVAILABLE_IN_1_48                _PANGO_EXTERN
#endif

#if GDK_TEXT_VERSION_MIN_REQUIRED >= GDK_TEXT_VERSION_1_50
# define GDK_TEXT_DEPRECATED_IN_1_50               GDK_TEXT_DEPRECATED
# define GDK_TEXT_DEPRECATED_IN_1_50_FOR(f)        GDK_TEXT_DEPRECATED_FOR(f)
#else
# define GDK_TEXT_DEPRECATED_IN_1_50               _PANGO_EXTERN
# define GDK_TEXT_DEPRECATED_IN_1_50_FOR(f)        _PANGO_EXTERN
#endif

#if GDK_TEXT_VERSION_MAX_ALLOWED < GDK_TEXT_VERSION_1_50
# define GDK_TEXT_AVAILABLE_IN_1_50                GDK_TEXT_UNAVAILABLE(1, 50)
#else
# define GDK_TEXT_AVAILABLE_IN_1_50                _PANGO_EXTERN
#endif

#if GDK_TEXT_VERSION_MIN_REQUIRED >= GDK_TEXT_VERSION_1_52
# define GDK_TEXT_DEPRECATED_IN_1_52               GDK_TEXT_DEPRECATED
# define GDK_TEXT_DEPRECATED_IN_1_52_FOR(f)        GDK_TEXT_DEPRECATED_FOR(f)
#else
# define GDK_TEXT_DEPRECATED_IN_1_52               _PANGO_EXTERN
# define GDK_TEXT_DEPRECATED_IN_1_52_FOR(f)        _PANGO_EXTERN
#endif

#if GDK_TEXT_VERSION_MAX_ALLOWED < GDK_TEXT_VERSION_1_52
# define GDK_TEXT_AVAILABLE_IN_1_52                GDK_TEXT_UNAVAILABLE(1, 52)
#else
# define GDK_TEXT_AVAILABLE_IN_1_52                _PANGO_EXTERN
#endif

#if GDK_TEXT_VERSION_MIN_REQUIRED >= GDK_TEXT_VERSION_1_54
# define GDK_TEXT_DEPRECATED_IN_1_54               GDK_TEXT_DEPRECATED
# define GDK_TEXT_DEPRECATED_IN_1_54_FOR(f)        GDK_TEXT_DEPRECATED_FOR(f)
#else
# define GDK_TEXT_DEPRECATED_IN_1_54               _PANGO_EXTERN
# define GDK_TEXT_DEPRECATED_IN_1_54_FOR(f)        _PANGO_EXTERN
#endif

#if GDK_TEXT_VERSION_MAX_ALLOWED < GDK_TEXT_VERSION_1_54
# define GDK_TEXT_AVAILABLE_IN_1_54                GDK_TEXT_UNAVAILABLE(1, 54)
#else
# define GDK_TEXT_AVAILABLE_IN_1_54                _PANGO_EXTERN
#endif

#if GDK_TEXT_VERSION_MIN_REQUIRED >= GDK_TEXT_VERSION_1_55
# define GDK_TEXT_DEPRECATED_IN_1_55               GDK_TEXT_DEPRECATED
# define GDK_TEXT_DEPRECATED_IN_1_55_FOR(f)        GDK_TEXT_DEPRECATED_FOR(f)
#else
# define GDK_TEXT_DEPRECATED_IN_1_55               _PANGO_EXTERN
# define GDK_TEXT_DEPRECATED_IN_1_55_FOR(f)        _PANGO_EXTERN
#endif

#if GDK_TEXT_VERSION_MAX_ALLOWED < GDK_TEXT_VERSION_1_55
# define GDK_TEXT_AVAILABLE_IN_1_55                GDK_TEXT_UNAVAILABLE(1, 55)
# define GDK_TEXT_AVAILABLE_ENUMERATOR_IN_1_55     GDK_TEXT_UNAVAILABLE (1, 55)
#else
# define GDK_TEXT_AVAILABLE_IN_1_55                _PANGO_EXTERN
# define GDK_TEXT_AVAILABLE_ENUMERATOR_IN_1_55
#endif

#if GDK_TEXT_VERSION_MIN_REQUIRED >= GDK_TEXT_VERSION_1_56
# define GDK_TEXT_DEPRECATED_IN_1_56               GDK_TEXT_DEPRECATED
# define GDK_TEXT_DEPRECATED_IN_1_56_FOR(f)        GDK_TEXT_DEPRECATED_FOR(f)
#else
# define GDK_TEXT_DEPRECATED_IN_1_56               _PANGO_EXTERN
# define GDK_TEXT_DEPRECATED_IN_1_56_FOR(f)        _PANGO_EXTERN
#endif

#if GDK_TEXT_VERSION_MAX_ALLOWED < GDK_TEXT_VERSION_1_56
# define GDK_TEXT_AVAILABLE_IN_1_56                GDK_TEXT_UNAVAILABLE(1, 56)
# define GDK_TEXT_AVAILABLE_ENUMERATOR_IN_1_56     GDK_TEXT_UNAVAILABLE (1, 56)
#else
# define GDK_TEXT_AVAILABLE_IN_1_56                _PANGO_EXTERN
# define GDK_TEXT_AVAILABLE_ENUMERATOR_IN_1_56
#endif

#if GDK_TEXT_VERSION_MIN_REQUIRED >= GDK_TEXT_VERSION_1_57
# define GDK_TEXT_DEPRECATED_IN_1_57               GDK_TEXT_DEPRECATED
# define GDK_TEXT_DEPRECATED_IN_1_57_FOR(f)        GDK_TEXT_DEPRECATED_FOR(f)
#else
# define GDK_TEXT_DEPRECATED_IN_1_57               _PANGO_EXTERN
# define GDK_TEXT_DEPRECATED_IN_1_57_FOR(f)        _PANGO_EXTERN
#endif

#if GDK_TEXT_VERSION_MAX_ALLOWED < GDK_TEXT_VERSION_1_57
# define GDK_TEXT_AVAILABLE_IN_1_57                GDK_TEXT_UNAVAILABLE(1, 57)
# define GDK_TEXT_AVAILABLE_ENUMERATOR_IN_1_57     GDK_TEXT_UNAVAILABLE (1, 57)
#else
# define GDK_TEXT_AVAILABLE_IN_1_57                _PANGO_EXTERN
# define GDK_TEXT_AVAILABLE_ENUMERATOR_IN_1_57
#endif

#if GDK_TEXT_VERSION_MIN_REQUIRED >= GDK_TEXT_VERSION_1_58
# define GDK_TEXT_DEPRECATED_IN_1_58               GDK_TEXT_DEPRECATED
# define GDK_TEXT_DEPRECATED_IN_1_58_FOR(f)        GDK_TEXT_DEPRECATED_FOR(f)
#else
# define GDK_TEXT_DEPRECATED_IN_1_58               _PANGO_EXTERN
# define GDK_TEXT_DEPRECATED_IN_1_58_FOR(f)        _PANGO_EXTERN
#endif

#if GDK_TEXT_VERSION_MAX_ALLOWED < GDK_TEXT_VERSION_1_58
# define GDK_TEXT_AVAILABLE_IN_1_58                GDK_TEXT_UNAVAILABLE(1, 58)
# define GDK_TEXT_AVAILABLE_ENUMERATOR_IN_1_58     GDK_TEXT_UNAVAILABLE (1, 58)
#else
# define GDK_TEXT_AVAILABLE_IN_1_58                _PANGO_EXTERN
# define GDK_TEXT_AVAILABLE_ENUMERATOR_IN_1_58
#endif

#endif /* __PANGO_VERSION_H__ */


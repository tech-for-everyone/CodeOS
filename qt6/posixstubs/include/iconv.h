/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * CodeOS iconv — declarations for the conversion implementation in
 * src/codeos_compat.c.  glib's g_convert()/g_locale_*() consumers link the
 * symbols from libcodeos_posixstubs.
 */

#ifndef _CODEOS_ICONV_H
#define _CODEOS_ICONV_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef void *iconv_t;

iconv_t iconv_open(const char *tocode, const char *fromcode);
size_t  iconv(iconv_t cd, char **inbuf, size_t *inbytesleft,
              char **outbuf, size_t *outbytesleft);
int     iconv_close(iconv_t cd);

#ifdef __cplusplus
}
#endif

#endif /* _CODEOS_ICONV_H */

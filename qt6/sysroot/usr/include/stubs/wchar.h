#ifndef _COMPAT_WCHAR_H
#define _COMPAT_WCHAR_H 1

#include <stddef.h>
#include <stdio.h>

#ifndef __DEFINED_FILE
#define __DEFINED_FILE
typedef struct _FILE FILE;
#endif

/* cxx_compat.h (force-included) declares most <wchar.h> symbols with FILE*.
   This header only adds the ones <cwchar> needs that are not there yet. */

#ifndef __DEFINED_wint_t
#define __DEFINED_wint_t
typedef __WINT_TYPE__ wint_t;
#endif

#ifdef __cplusplus
extern "C" {
#endif

typedef unsigned int wctype_t;
typedef const int *wctrans_t;

wint_t fgetwc(FILE *);
wchar_t *fgetws(wchar_t *, int, FILE *);
wint_t fputwc(wchar_t, FILE *);
int fputws(const wchar_t *, FILE *);

wint_t iswalnum(wint_t);
wint_t towlower(wint_t);
wint_t towupper(wint_t);
wint_t towctrans(wint_t, wctrans_t);
wint_t wctrans(const char *);
wctype_t wctype(const char *);
wint_t iswctype(wint_t, wctype_t);
wint_t iswalpha(wint_t);
wint_t iswblank(wint_t);
wint_t iswcntrl(wint_t);
wint_t iswdigit(wint_t);
wint_t iswgraph(wint_t);
wint_t iswlower(wint_t);
wint_t iswprint(wint_t);
wint_t iswpunct(wint_t);
wint_t iswspace(wint_t);
wint_t iswupper(wint_t);
wint_t iswxdigit(wint_t);

size_t wcstombs(char *, const wchar_t *, size_t);
int wctomb(char *, wchar_t);
int mbtowc(wchar_t *, const char *, size_t);
int mblen(const char *, size_t);

#ifdef __cplusplus
}
#endif

#endif

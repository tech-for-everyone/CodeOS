/* CodeOS iconv provider (Phase 1): UTF-8 passthrough.
 * Real charset conversion lands when gio's file/encoding paths need it. */
#include <stddef.h>

typedef void *iconv_t;

iconv_t iconv_open(const char *tocode, const char *fromcode)
{
    (void)tocode;
    (void)fromcode;
    return (iconv_t)1;
}

size_t iconv(iconv_t cd, char **inbuf, size_t *inbytesleft,
             char **outbuf, size_t *outbytesleft)
{
    (void)cd;
    if (!inbuf || !*inbuf || !inbytesleft || !outbuf || !*outbuf || !outbytesleft)
        return 0;
    while (*inbytesleft > 0 && *outbytesleft > 0)
    {
        **outbuf = **inbuf;
        (*inbuf)++; (*outbuf)++;
        (*inbytesleft)--; (*outbytesleft)--;
    }
    if (*inbytesleft > 0)
        return (size_t)-1;
    return 0;
}

int iconv_close(iconv_t cd)
{
    (void)cd;
    return 0;
}

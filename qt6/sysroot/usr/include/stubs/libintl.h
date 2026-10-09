#ifndef _LIBINTL_H
#define _LIBINTL_H 1

#ifdef __cplusplus
extern "C" {
#endif

#define gettext(str) (str)
#define dgettext(domain, str) (str)
#define dcgettext(domain, str, category) (str)
#define textdomain(domain) ((char *)0)
#define bindtextdomain(domain, dir) ((char *)0)
#define bind_textdomain_codeset(domain, codeset) ((char *)0)

#ifdef __cplusplus
}
#endif

#endif

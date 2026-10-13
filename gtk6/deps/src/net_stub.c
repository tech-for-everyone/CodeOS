/* CodeOS network shim — weak link-time stubs for the resolver/socket entry
 * points gio's build and code reference. CodeOS has no userspace network stack
 * yet, so every call fails cleanly. `weak` so the kernel can override any of
 * these when a real stack lands.
 */
#include <stddef.h>

#define WEAK __attribute__((weak))

/* BIND resolver entry point (gio/gthreadedresolver.c). */
WEAK int res_query(const char *dname, int class_, int type, unsigned char *answer, int anslen)
{
    (void)dname; (void)class_; (void)type; (void)answer; (void)anslen;
    return -1;
}

WEAK int socket(int domain, int type, int protocol)
{ (void)domain; (void)type; (void)protocol; return -1; }

WEAK int connect(int fd, const void *addr, unsigned int len)
{ (void)fd; (void)addr; (void)len; return -1; }

WEAK int bind(int fd, const void *addr, unsigned int len)
{ (void)fd; (void)addr; (void)len; return -1; }

WEAK int listen(int fd, int backlog)
{ (void)fd; (void)backlog; return -1; }

WEAK int accept(int fd, void *addr, unsigned int *len)
{ (void)fd; (void)addr; (void)len; return -1; }

WEAK int shutdown(int fd, int how)
{ (void)fd; (void)how; return -1; }

WEAK long send(int fd, const void *buf, unsigned long len, int flags)
{ (void)fd; (void)buf; (void)len; (void)flags; return -1; }

WEAK long recv(int fd, void *buf, unsigned long len, int flags)
{ (void)fd; (void)buf; (void)len; (void)flags; return -1; }

WEAK long sendto(int fd, const void *buf, unsigned long len, int flags, const void *addr, unsigned int alen)
{ (void)fd; (void)buf; (void)len; (void)flags; (void)addr; (void)alen; return -1; }

WEAK long recvfrom(int fd, void *buf, unsigned long len, int flags, void *addr, unsigned int *alen)
{ (void)fd; (void)buf; (void)len; (void)flags; (void)addr; (void)alen; return -1; }

WEAK int getsockname(int fd, void *addr, unsigned int *len)
{ (void)fd; (void)addr; (void)len; return -1; }

WEAK int getpeername(int fd, void *addr, unsigned int *len)
{ (void)fd; (void)addr; (void)len; return -1; }

WEAK int setsockopt(int fd, int level, int optname, const void *optval, unsigned int optlen)
{ (void)fd; (void)level; (void)optname; (void)optval; (void)optlen; return -1; }

WEAK int getsockopt(int fd, int level, int optname, void *optval, unsigned int *optlen)
{ (void)fd; (void)level; (void)optname; (void)optval; (void)optlen; return -1; }

WEAK int getaddrinfo(const char *node, const char *service, const void *hints, void **res)
{ (void)node; (void)service; (void)hints; (void)res; return -1; }

WEAK void freeaddrinfo(void *res)
{ (void)res; }

WEAK const char *gai_strerror(int err)
{ (void)err; return "network unavailable"; }

WEAK int getnameinfo(const void *addr, unsigned int addrlen, char *host, unsigned int hostlen,
                     char *serv, unsigned int servlen, int flags)
{ (void)addr; (void)addrlen; (void)host; (void)hostlen; (void)serv; (void)servlen; (void)flags; return -1; }

WEAK unsigned int if_nametoindex(const char *name)
{ (void)name; return 0; }

WEAK char *if_indextoname(unsigned int idx, char *name)
{ (void)idx; (void)name; return (char *)0; }

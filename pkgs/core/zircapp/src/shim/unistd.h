/* unistd.h shim for the CodeOS ZircApp host.
 *
 * Zircon's apps are written against the Zircon libc, whose unistd.h
 * declares the POSIX getpid()/getppid() names.  CodeOS's freestanding
 * unistd.h only exposes sys_getpid()/sys_getppid(), so pulling it in and
 * adding the two POSIX declarations keeps the unmodified Zircon sources
 * compiling.  The definitions live in zircapp_platform.c, next to the
 * other platform glue (usleep, the zs_* syscall wrappers).
 *
 * The shim directory is placed before CodeOS's include/ in ZC_CFLAGS, and
 * uses #include_next so the rest of unistd.h is the CodeOS header. */
#ifndef ZIRCAPP_PORT_UNISTD_H
#define ZIRCAPP_PORT_UNISTD_H
#include_next <unistd.h>
int getpid(void);
int getppid(void);
#endif /* ZIRCAPP_PORT_UNISTD_H */

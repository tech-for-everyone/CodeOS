/* bas_os.h -- the C side of the bas OS SDK.
 *
 * Every entry point here is declared identically in sdk/bas/os.ba, which is
 * what a bas program's `extern fn` binds to.  The two files are a pair and
 * must be kept in step; there is no shared source because one is bas syntax
 * and the other is C.
 *
 * The prefix is `bas_` rather than `write`/`open`/... on purpose.  Zircon's
 * libc already defines POSIX names with its own semantics -- its `write`
 * ignores the fd, for one -- so shadowing those would make the same bas
 * program behave differently on the two operating systems.
 *
 * WHAT THE SIGNATURES CAN AND CANNOT SAY
 *
 * A bas program sees `&str` (which lowers to a C string pointer), `String`,
 * `Vec<T>` and the integers.  There is no slice or buffer type to hand out, so
 * nothing here takes a caller-provided buffer: `bas_read_file` and
 * `bas_list_dir` allocate and return a `String` instead.  That costs a
 * transient allocation and buys an API a bas program can actually write.
 */
#ifndef BAS_OS_H
#define BAS_OS_H

#include <stdint.h>
#include <stddef.h>

/* ── Console ──
 * `s` is a NUL-terminated C string, which is what a bas string literal lowers
 * to, so printing is a single call with no length to compute. */
int bas_print(const char *s);
int bas_write(int fd, const char *s);

/* Read one byte from `fd`.  Returns the byte, or -1 at end of input.  Chosen
 * over a buffer for the reason above: it needs no out-parameter. */
int bas_read_char(int fd);

/* ── Process ── */
void bas_exit(int status);
int  bas_getpid(void);
int  bas_getppid(void);
int  bas_sleep_ms(int ms);
int  bas_yield(void);

/* ── Filesystem ── */
int  bas_open(const char *path, int flags);
int  bas_close(int fd);
int  bas_lseek(int fd, long long offset, int whence);

/* 0 when the path is reachable, -errno otherwise (SYSCALL_ACCESS). */
int  bas_access(const char *path);

/* Size in bytes, or -errno.  Returning the value rather than filling a caller
 * pointer keeps it inside what a bas program can express. */
long long bas_stat_size(const char *path);

/* The whole file.  The length is returned through `out_len` (nullable)
 * because bas has no way to express a buffer, and the buffer is owned by the
 * caller -- copy what you need out of it, then bas_release.  Returns NULL when
 * the file cannot be read. */
const char *bas_read_file(const char *path, long long *out_len);

/* Write the whole NUL-terminated string.  Returns the byte count written, or
 * -errno.
 *
 * TWO parameters, deliberately.  This took a length argument first, and the
 * .ba declaration (which is two parameters, because bas cannot express a
 * strnlen) was not changed with it -- so the C function read a third argument
 * that was never passed and wrote 45 bytes of stack garbage instead of the 24
 * that were asked for.  The mismatch is invisible to a linker (the prototype
 * comes from the same file) and only showed up as a wrong number on a real
 * boot, which is why bas_boot_check.py asserts the byte count rather than
 * merely that the call succeeded. */
int bas_write_file(const char *path, const char *data);

/* Directory entries, newline separated, owned until bas_release.  Returns NULL
 * for an unreadable directory.  Two calls (count, then fill) rather than a
 * caller-sized buffer, for the same reason. */
const char *bas_list_dir(const char *path, long long *out_len);

/* Free a buffer from bas_read_file / bas_list_dir.  The only free a bas
 * program can reach, since it can obtain no other buffer. */
void bas_release(const char *s);

int bas_mkdir(const char *path);
int bas_unlink(const char *path);

/* ── Time ── */
long long bas_time(void);

/* ── Integer math (implemented in bas_math.c) ── */
unsigned int       bas_isqrt32(unsigned int v);
unsigned long long bas_gcd64(unsigned long long a, unsigned long long b);
unsigned int       bas_rand(void);
void               bas_srand(unsigned int seed);

#endif /* BAS_OS_H */
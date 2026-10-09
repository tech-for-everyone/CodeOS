/* CodeOS POSIX Stubs - Main implementation file
 * Provides non-inline implementations for POSIX functions Qt6 needs */

#include <stdint.h>
#include <stdarg.h>
#include "codeos_types.h"

/* Forward declarations for CodeOS syscalls */
extern int sys_write(const void *buf, int count);
extern int sys_read(int fd, void *buf, int count);
extern int sys_open(const char *path, int flags);
extern int sys_close(int fd);
extern void sys_exit(int status);
extern void *sys_brk(void *addr);
extern int sys_fork(void);
extern int sys_execve(const char *path, char **argv, int argc);
extern int sys_wait(int pid, int *status);
extern int sys_getpid(void);
extern int sys_pipe(int fd[2]);
extern int sys_dup(int oldfd);
extern int sys_dup2(int oldfd, int newfd);
extern int sys_lseek(int fd, int offset, int whence);
extern void *sys_mmap(void *addr, int len, int prot);
extern void sys_yield(void);

/* Memory management - implementations provided in cppstubs.cpp */
extern void *malloc(int size);
extern void free(void *p);
extern void *realloc(void *ptr, int size);

/* String functions - provided by userspace/lib/string.c or stubs below */
extern int strlen(const char *s);
extern int strcmp(const char *a, const char *b);
extern int strncmp(const char *a, const char *b, int n);
extern char *strcpy(char *dst, const char *src);
extern char *strncpy(char *dst, const char *src, int n);
extern char *strcat(char *dst, const char *src);
extern char *strchr(const char *s, int c);
extern char *strrchr(const char *s, int c);
extern int memcmp(const void *a, const void *b, int n);
extern void *memcpy(void *dst, const void *src, int n);
extern void *memmove(void *dst, const void *src, int n);
extern void *memset(void *s, int c, int n);

/* ── File Operations ── */

int open(const char *path, int flags, ...) {
    return sys_open(path, flags);
}

int read(int fd, void *buf, int count) {
    return sys_read(fd, buf, count);
}

int write(int fd, const void *buf, int count) {
    if (fd <= 2) {
        return sys_write(buf, count);
    }
    return sys_write(buf, count);
}

int close(int fd) {
    return sys_close(fd);
}

off_t lseek(int fd, off_t offset, int whence) {
    return sys_lseek(fd, (int)offset, whence);
}

int dup(int oldfd) {
    return sys_dup(oldfd);
}

int dup2(int oldfd, int newfd) {
    return sys_dup2(oldfd, newfd);
}

int pipe(int pipefd[2]) {
    return sys_pipe(pipefd);
}

int pipe2(int pipefd[2], int flags) {
    (void)flags;
    return sys_pipe(pipefd);
}

/* ── Process Operations ── */

int fork(void) {
    return sys_fork();
}

int execve(const char *path, char *const argv[], char *const envp[]) {
    (void)envp;
    int argc = 0;
    if (argv) {
        while (argv[argc]) argc++;
    }
    return sys_execve(path, (char **)argv, argc);
}

int waitpid(int pid, int *status, int options) {
    (void)options;
    return sys_wait(pid, status);
}

int getpid(void) {
    return sys_getpid();
}

int getppid(void) {
    return 0;  /* Stub */
}

int getuid(void) {
    return 0;  /* Root */
}

int geteuid(void) {
    return 0;
}

int getgid(void) {
    return 0;
}

int getegid(void) {
    return 0;
}

int setuid(int uid) {
    (void)uid;
    return 0;
}

int setgid(int gid) {
    (void)gid;
    return 0;
}

void _exit(int status) {
    sys_exit(status);
    while (1);
}

/* ── File Operations ── */

int access(const char *path, int mode) {
    (void)mode;
    int fd = sys_open(path, 0);
    if (fd >= 0) {
        sys_close(fd);
        return 0;
    }
    return -1;
}

int faccessat(int dirfd, const char *path, int mode, int flags) {
    (void)dirfd; (void)flags;
    return access(path, mode);
}

int unlink(const char *path) {
    (void)path;
    return 0;  /* Stub */
}

int rmdir(const char *path) {
    (void)path;
    return 0;
}

int rename(const char *oldpath, const char *newpath) {
    (void)oldpath; (void)newpath;
    return 0;
}

int readlink(const char *path, char *buf, int bufsiz) {
    (void)path; (void)bufsiz;
    if (buf) buf[0] = 0;
    return 0;
}

int readlinkat(int dirfd, const char *path, char *buf, int bufsiz) {
    (void)dirfd;
    return readlink(path, buf, bufsiz);
}

int mkdir(const char *path, int mode) {
    (void)path; (void)mode;
    return 0;
}

int chmod(const char *path, int mode) {
    (void)path; (void)mode;
    return 0;
}

int chown(const char *path, int owner, int group) {
    (void)path; (void)owner; (void)group;
    return 0;
}

/* ── Directory Operations ── */

struct dirent {
    uint64_t d_ino;
    int64_t  d_off;
    unsigned short d_reclen;
    unsigned char  d_type;
    char d_name[256];
};

struct DIR {
    int fd;
    int pos;
};

typedef struct DIR DIR;

DIR *opendir(const char *name) {
    (void)name;
    static DIR dir;
    dir.fd = -1;
    dir.pos = 0;
    return &dir;
}

struct dirent *readdir(DIR *dir) {
    (void)dir;
    return 0;
}

int closedir(DIR *dir) {
    (void)dir;
    return 0;
}

/* ── Time Functions ── */

int gettimeofday(struct timeval *tv, void *tz) {
    (void)tz;
    if (!tv) return -1;

    extern uint64_t timer_get_milliseconds(void);
    uint64_t ms = timer_get_milliseconds();
    tv->tv_sec = ms / 1000;
    tv->tv_usec = (ms % 1000) * 1000;
    return 0;
}

int nanosleep(const struct timespec *req, struct timespec *rem) {
    if (!req) return -1;
    extern void sys_sleep(int ms);
    int ms = req->tv_sec * 1000 + req->tv_nsec / 1000000;
    if (ms < 1) ms = 1;
    sys_sleep(ms);
    if (rem) {
        rem->tv_sec = 0;
        rem->tv_nsec = 0;
    }
    return 0;
}

int clock_gettime(int clk_id, struct timespec *tp) {
    (void)clk_id;
    if (!tp) return -1;

    extern uint64_t timer_get_milliseconds(void);
    uint64_t ms = timer_get_milliseconds();
    tp->tv_sec = ms / 1000;
    tp->tv_nsec = (ms % 1000) * 1000000;
    return 0;
}

int sched_yield(void) {
    sys_yield();
    return 0;
}

/* ── Signal Handling (stubs) ── */

typedef void (*sig_t)(int);
typedef void (*sighandler_t)(int);

#define SIG_DFL ((sighandler_t)0)
#define SIG_IGN ((sighandler_t)1)

sighandler_t signal(int signum, sighandler_t handler) {
    (void)signum; (void)handler;
    return SIG_DFL;
}

int sigaction(int signum, const void *act, void *oldact) {
    (void)signum; (void)act; (void)oldact;
    return 0;
}

int kill(int pid, int sig) {
    (void)pid; (void)sig;
    return 0;
}

int raise(int sig) {
    (void)sig;
    return 0;
}

/* ── Environment ── */

static char *environ_storage[64];
char **environ = environ_storage;

int setenv(const char *name, const char *value, int overwrite) {
    (void)name; (void)value; (void)overwrite;
    return 0;
}

int unsetenv(const char *name) {
    (void)name;
    return 0;
}

char *getenv(const char *name) {
    (void)name;
    return 0;
}

/* ── printf family (implementations in userspace/lib/stdio.c or stubs) ── */

int printf(const char *fmt, ...);
int sprintf(char *str, const char *fmt, ...);
int snprintf(char *str, int size, const char *fmt, ...);
int vsnprintf(char *str, int size, const char *fmt, va_list ap);
int sscanf(const char *str, const char *fmt, ...);

/* ── Sorting ── */

static void qsort_swap(char *a, char *b, int size) {
    for (int i = 0; i < size; i++) {
        char tmp = a[i];
        a[i] = b[i];
        b[i] = tmp;
    }
}

void qsort(void *base, int nmemb, int size,
           int (*compar)(const void *, const void *)) {
    if (!base || nmemb <= 1 || size <= 0) return;

    for (int i = 0; i < nmemb - 1; i++) {
        int min = i;
        for (int j = i + 1; j < nmemb; j++) {
            if (compar((char *)base + j * size, (char *)base + min * size) < 0) {
                min = j;
            }
        }
        if (min != i) {
            qsort_swap((char *)base + i * size, (char *)base + min * size, size);
        }
    }
}

void *bsearch(const void *key, const void *base, int nmemb, int size,
              int (*compar)(const void *, const void *)) {
    if (!base || nmemb <= 0 || size <= 0) return 0;

    int lo = 0, hi = nmemb - 1;
    while (lo <= hi) {
        int mid = (lo + hi) / 2;
        int cmp = compar(key, (char *)base + mid * size);
        if (cmp == 0) return (char *)base + mid * size;
        else if (cmp < 0) hi = mid - 1;
        else lo = mid + 1;
    }
    return 0;
}

/* ── Locale (stub) ── */

struct lconv {
    char *decimal_point;
    char *thousands_sep;
    char *grouping;
    char *int_curr_symbol;
    char *currency_symbol;
    char *mon_decimal_point;
    char *mon_thousands_sep;
    char *mon_grouping;
    char *positive_sign;
    char *negative_sign;
    char int_frac_digits;
    char frac_digits;
    char p_cs_precedes;
    char p_sep_by_space;
    char n_cs_precedes;
    char n_sep_by_space;
    char p_sign_posn;
    char n_sign_posn;
};

static struct lconv default_locale = {
    ".", ",", "", "", "", ".", "", "", "", "",
    2, 2, 1, 0, 1, 0, 1, 1
};

struct lconv *localeconv(void) {
    return &default_locale;
}

char *setlocale(int category, const char *locale) {
    (void)category; (void)locale;
    return "C";
}

/* ── Dynamic Loading (stubs) ── */

#define RTLD_LAZY    1
#define RTLD_NOW     2
#define RTLD_GLOBAL  256
#define RTLD_LOCAL   0

void *dlopen(const char *filename, int flag) {
    (void)filename; (void)flag;
    return 0;
}

void *dlsym(void *handle, const char *symbol) {
    (void)handle; (void)symbol;
    return 0;
}

int dlclose(void *handle) {
    (void)handle;
    return 0;
}

char *dlerror(void) {
    return "dlopen not supported on CodeOS";
}

/* ── System Configuration ── */

#define _SC_PAGESIZE         30
#define _SC_OPEN_MAX         5
#define _SC_CLK_TCK          2
#define _SC_NPROCESSORS_ONLN 84
#define _SC_GETPW_R_SIZE_MAX 70
#define _SC_HOST_NAME_MAX    120

long sysconf(int name) {
    switch (name) {
    case _SC_PAGESIZE:         return 4096;
    case _SC_OPEN_MAX:         return 256;
    case _SC_CLK_TCK:          return 100;
    case _SC_NPROCESSORS_ONLN: return 4;
    case _SC_GETPW_R_SIZE_MAX: return 1024;
    case _SC_HOST_NAME_MAX:    return 256;
    default:                   return -1;
    }
}

/* ── System Info ── */

struct utsname {
    char sysname[65];
    char nodename[65];
    char release[65];
    char version[65];
    char machine[65];
    char domainname[65];
};

int uname(struct utsname *buf) {
    if (!buf) return -1;
    for (int i = 0; i < 65; i++) {
        buf->sysname[i] = 0;
        buf->nodename[i] = 0;
        buf->release[i] = 0;
        buf->version[i] = 0;
        buf->machine[i] = 0;
        buf->domainname[i] = 0;
    }
    for (int i = 0; i < 6 && i < 64; i++) buf->sysname[i] = "CodeOS"[i];
    for (int i = 0; i < 5 && i < 64; i++) buf->release[i] = "1.4.0"[i];
    for (int i = 0; i < 5 && i < 64; i++) buf->machine[i] = "x86_64"[i];
    return 0;
}

int gethostname(char *name, int len) {
    if (!name || len <= 0) return -1;
    for (int i = 0; i < 6 && i < len - 1; i++)
        name[i] = "codeos"[i];
    name[6] = 0;
    return 0;
}

/* ── User/Group Info (stubs) ── */

struct passwd {
    char *pw_name;
    char *pw_passwd;
    int pw_uid;
    int pw_gid;
    char *pw_gecos;
    char *pw_dir;
    char *pw_shell;
};

struct group {
    char *gr_name;
    char *gr_passwd;
    int gr_gid;
    char **gr_mem;
};

static struct passwd default_pw = { "root", "x", 0, 0, "root", "/root", "/bin/sh" };
static struct group default_gr = { "root", "x", 0, 0 };

struct passwd *getpwnam(const char *name) {
    (void)name;
    return &default_pw;
}

struct passwd *getpwuid(int uid) {
    (void)uid;
    return &default_pw;
}

struct group *getgrnam(const char *name) {
    (void)name;
    return &default_gr;
}

struct group *getgrgid(int gid) {
    (void)gid;
    return &default_gr;
}

int getpwnam_r(const char *name, struct passwd *pwd, char *buf, int buflen,
               struct passwd **result) {
    (void)name; (void)buf; (void)buflen;
    if (pwd) *pwd = default_pw;
    if (result) *result = pwd;
    return 0;
}

int getpwuid_r(int uid, struct passwd *pwd, char *buf, int buflen,
               struct passwd **result) {
    (void)uid; (void)buf; (void)buflen;
    if (pwd) *pwd = default_pw;
    if (result) *result = pwd;
    return 0;
}

/* ── Syslog (stub) ── */

#define LOG_EMERG   0
#define LOG_ALERT   1
#define LOG_CRIT    2
#define LOG_ERR     3
#define LOG_WARNING 4
#define LOG_NOTICE  5
#define LOG_INFO    6
#define LOG_DEBUG   7

#define LOG_PID    0x01
#define LOG_CONS   0x02
#define LOG_ODELAY 0x04
#define LOG_NDELAY 0x08

#define LOG_LOCAL0 16
#define LOG_LOCAL1 17
#define LOG_DAEMON 3

void openlog(const char *ident, int option, int facility) {
    (void)ident; (void)option; (void)facility;
}

void closelog(void) {
}

void syslog(int priority, const char *format, ...) {
    (void)priority; (void)format;
}

/* ── Pattern Matching (stubs) ── */

#define FNM_PATHNAME    1
#define FNM_PERIOD      2
#define FNM_NOESCAPE    4
#define FNM_CASEMATCH   8

int fnmatch(const char *pattern, const char *string, int flags) {
    (void)pattern; (void)string; (void)flags;
    return 1;
}

#define GLOB_ERR        1
#define GLOB_MARK       2
#define GLOB_NOSORT     4
#define GLOB_DOOFFS     8
#define GLOB_NOCHECK    16
#define GLOB_APPEND     32
#define GLOB_NOESCAPE   64

typedef struct {
    int gl_pathc;
    char **gl_pathv;
    int gl_offs;
} glob_t;

int glob(const char *pattern, int flags, int (*errfunc)(const char *, int),
         glob_t *pglob) {
    (void)pattern; (void)flags; (void)errfunc;
    if (pglob) {
        pglob->gl_pathc = 0;
        pglob->gl_pathv = 0;
    }
    return 0;
}

void globfree(glob_t *pglob) {
    (void)pglob;
}

/* ── Resource Limits ── */

struct rlimit {
    unsigned long rlim_cur;
    unsigned long rlim_max;
};

int getrlimit(int resource, struct rlimit *rlim) {
    (void)resource;
    if (!rlim) return -1;
    rlim->rlim_cur = 65536;
    rlim->rlim_max = 65536;
    return 0;
}

int setrlimit(int resource, const struct rlimit *rlim) {
    (void)resource; (void)rlim;
    return 0;
}

/* ── Timer ── */

struct itimerval {
    struct timeval it_interval;
    struct timeval it_value;
};

#define ITIMER_REAL    0
#define ITIMER_VIRTUAL 1
#define ITIMER_PROF    2

int getitimer(int which, struct itimerval *curr_value) {
    (void)which; (void)curr_value;
    return 0;
}

int setitimer(int which, const struct itimerval *new_value,
              struct itimerval *old_value) {
    (void)which; (void)new_value; (void)old_value;
    return 0;
}

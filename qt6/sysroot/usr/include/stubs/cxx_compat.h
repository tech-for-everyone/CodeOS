#ifndef CXX_COMPAT_H
#define CXX_COMPAT_H

#include <stddef.h>
#include <stdarg.h>
#ifndef __DEFINED_wint_t
#define __DEFINED_wint_t
typedef __WINT_TYPE__ wint_t;
#endif
#ifndef __DEFINED_ssize_t
#define __DEFINED_ssize_t
typedef __INT64_TYPE__ ssize_t;
#endif

/* ── Types not provided by GCC built-ins ── */
#ifndef __DEFINED_mode_t
#define __DEFINED_mode_t
typedef unsigned int mode_t;
#endif
#ifndef __DEFINED_off_t
#define __DEFINED_off_t
typedef long off_t;
#endif
#ifndef __DEFINED_pid_t
#define __DEFINED_pid_t
typedef int pid_t;
#endif

#ifndef __DEFINED_FILE
#define __DEFINED_FILE
typedef struct _FILE FILE;
#endif
#ifndef __DEFINED_fpos_t
#define __DEFINED_fpos_t
typedef long fpos_t;
#endif
#ifndef __DEFINED_va_list
#define __DEFINED_va_list
typedef __builtin_va_list va_list;
#endif

/* pthread types and functions - the real CodeOS pthread.h (sysroot) provides
 * the actual inline implementations.  Pull it in here so code that only
 * includes cxx_compat.h still gets working pthreads. */
#include <pthread.h>

/* struct timeval (but don't conflict if already defined) */
#ifndef __DEFINED_struct_timeval
#define __DEFINED_struct_timeval
/* struct timeval defined via sys/time.h or bits/types/struct_timeval.h */
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* ── string.h ── */
void *memcpy(void *, const void *, size_t);
void *memmove(void *, const void *, size_t);
void *memset(void *, int, size_t);
int memcmp(const void *, const void *, size_t);
void *memchr(const void *, int, size_t);
char *strcat(char *, const char *);
char *strchr(const char *, int);
int strcmp(const char *, const char *);
int strcoll(const char *, const char *);
char *strcpy(char *, const char *);
size_t strcspn(const char *, const char *);
char *strerror(int);
size_t strlen(const char *);
char *strncat(char *, const char *, size_t);
int strncmp(const char *, const char *, size_t);
char *strncpy(char *, const char *, size_t);
char *strpbrk(const char *, const char *);
char *strrchr(const char *, int);
size_t strspn(const char *, const char *);
char *strstr(const char *, const char *);
char *strtok(char *, const char *);
size_t strxfrm(char *, const char *, size_t);

/* ── stdio.h ── */
void clearerr(FILE *);
FILE *fopen(const char *, const char *);
FILE *freopen(const char *, const char *, FILE *);
int fclose(FILE *);
int feof(FILE *);
int ferror(FILE *);
int fflush(FILE *);
int fgetc(FILE *);
int fgetpos(FILE *, fpos_t *);
char *fgets(char *, int, FILE *);
int fputc(int, FILE *);
int fputs(const char *, FILE *);
size_t fread(void *, size_t, size_t, FILE *);
int fscanf(FILE *, const char *, ...) __attribute__((__format__(__scanf__, 2, 3)));
int fseek(FILE *, long, int);
int fsetpos(FILE *, const fpos_t *);
long ftell(FILE *);
size_t fwrite(const void *, size_t, size_t, FILE *);
int getc(FILE *);
int getchar(void);
int ungetc(int, FILE *);
void perror(const char *);
int printf(const char *, ...) __attribute__((__format__(__printf__, 1, 2)));
int fprintf(FILE *, const char *, ...) __attribute__((__format__(__printf__, 2, 3)));
int sprintf(char *, const char *, ...) __attribute__((__format__(__printf__, 2, 3)));
int snprintf(char *, size_t, const char *, ...) __attribute__((__format__(__printf__, 3, 4)));
int putc(int, FILE *);
int putchar(int);
int puts(const char *);
int remove(const char *);
int rename(const char *, const char *);
void rewind(FILE *);
int scanf(const char *, ...) __attribute__((__format__(__scanf__, 1, 2)));
int sscanf(const char *, const char *, ...) __attribute__((__format__(__scanf__, 2, 3)));
void setbuf(FILE *, char *);
int setvbuf(FILE *, char *, int, size_t);
FILE *tmpfile(void);
char *tmpnam(char *);
int vfprintf(FILE *, const char *, __builtin_va_list);
int vprintf(const char *, __builtin_va_list);
int vsprintf(char *, const char *, __builtin_va_list);
int vsnprintf(char *, size_t, const char *, __builtin_va_list);
int vfscanf(FILE *, const char *, __builtin_va_list);
int vscanf(const char *, __builtin_va_list);
int vsscanf(const char *, const char *, __builtin_va_list);

/* ── stdlib.h ── */
typedef struct { int quot; int rem; } div_t;
typedef struct { long quot; long rem; } ldiv_t;
typedef struct { long long quot; long long rem; } lldiv_t;
void abort(void) __attribute__((__noreturn__));
int abs(int);
int atexit(void (*)(void));
double atof(const char *);
int atoi(const char *);
long atol(const char *);
long long atoll(const char *);
void *bsearch(const void *, const void *, size_t, size_t, int (*)(const void *, const void *));
void *calloc(size_t, size_t);
div_t div(int, int);
void exit(int) __attribute__((__noreturn__));
void _Exit(int) __attribute__((__noreturn__));
void free(void *);
char *getenv(const char *);
long labs(long);
ldiv_t ldiv(long, long);
long long llabs(long long);
lldiv_t lldiv(long long, long long);
void *malloc(size_t);
int mblen(const char *, size_t);
size_t mbstowcs(wchar_t *, const char *, size_t);
int mbtowc(wchar_t *, const char *, size_t);
void qsort(void *, size_t, size_t, int (*)(const void *, const void *));
int rand(void);
void *realloc(void *, size_t);
void srand(unsigned int);
double strtod(const char *, char **);
float strtof(const char *, char **);
long double strtold(const char *, char **);
long strtol(const char *, char **, int);
long long strtoll(const char *, char **, int);
unsigned long strtoul(const char *, char **, int);
unsigned long long strtoull(const char *, char **, int);
int system(const char *);
size_t wcstombs(char *, const wchar_t *, size_t);
int wctomb(char *, wchar_t);
void *aligned_alloc(size_t, size_t);
void at_quick_exit(void (*)(void));
void quick_exit(int) __attribute__((__noreturn__));
#define RAND_MAX 2147483647
#define EXIT_SUCCESS 0
#define EXIT_FAILURE 1

/* ── ctype.h ── */
int isalnum(int);
int isalpha(int);
int isblank(int);
int iscntrl(int);
int isdigit(int);
int isgraph(int);
int islower(int);
int isprint(int);
int ispunct(int);
int isspace(int);
int isupper(int);
int isxdigit(int);
int tolower(int);
int toupper(int);

/* ── time.h ── */
typedef long time_t;
typedef long clock_t;
#ifndef __DEFINED_clockid_t
#define __DEFINED_clockid_t
typedef long clockid_t;
#endif
#define CLOCK_MONOTONIC 1
struct tm {
    int tm_sec; int tm_min; int tm_hour; int tm_mday; int tm_mon; int tm_year;
    int tm_wday; int tm_yday; int tm_isdst;
};
struct timespec { time_t tv_sec; long tv_nsec; };
clock_t clock(void);
double difftime(time_t, time_t);
time_t mktime(struct tm *);
time_t time(time_t *);
char *asctime(const struct tm *);
char *ctime(const time_t *);
struct tm *gmtime(const time_t *);
struct tm *localtime(const time_t *);
size_t strftime(char *, size_t, const char *, const struct tm *);
int timespec_get(struct timespec *, int);
#define TIME_UTC 1

/* ── math.h ── */
typedef double double_t;
typedef float float_t;
#define HUGE_VAL __builtin_huge_val()
#define HUGE_VALF __builtin_huge_valf()
#define HUGE_VALL __builtin_huge_vall()
#define INFINITY __builtin_inff()
#define NAN __builtin_nanf("")
#define M_PI 3.14159265358979323846
#define FP_NAN 0
#define FP_INFINITE 1
#define FP_ZERO 2
#define FP_SUBNORMAL 3
#define FP_NORMAL 4
double acos(double); double asin(double); double atan(double); double atan2(double,double);
double cos(double); double sin(double); double tan(double);
double cosh(double); double sinh(double); double tanh(double);
double exp(double); double frexp(double,int*); double ldexp(double,int);
double log(double); double log10(double); double modf(double,double*);
double pow(double,double); double sqrt(double);
double ceil(double); double fabs(double); double floor(double); double fmod(double,double);
double acosh(double); double asinh(double); double atanh(double);
double cbrt(double); double copysign(double,double);
double erf(double); double erfc(double);
double exp2(double); double expm1(double);
double fdim(double,double); double fma(double,double,double);
double fmax(double,double); double fmin(double,double);
double hypot(double,double); double ilogb(double);
double lgamma(double);
double llrint(double); double llround(double);
double log1p(double); double log2(double); double logb(double);
double lrint(double); double lround(double);
double nearbyint(double); double nextafter(double,double); double nexttoward(double,long double);
double remainder(double,double); double remquo(double,double,int*);
double rint(double); double round(double);
double scalbln(double,long); double scalbn(double,int);
double tgamma(double); double trunc(double);
float acosf(float); float asinf(float); float atanf(float); float atan2f(float,float);
float cosf(float); float sinf(float); float tanf(float);
float acoshf(float); float asinhf(float); float atanhf(float);
float cbrtf(float); float ceilf(float); float copysignf(float,float);
float coshf(float); float erff(float); float erfcf(float);
float expf(float); float exp2f(float); float expm1f(float);
float fabsf(float); float fdimf(float,float); float floorf(float);
float fmaf(float,float,float); float fmaxf(float,float); float fminf(float,float);
float fmodf(float,float); float frexpf(float,int*);
float hypotf(float,float); float ilogbf(float);
float ldexpf(float,int); float lgammaf(float);
float llrintf(float); float llroundf(float);
float logf(float); float log10f(float); float log1pf(float); float log2f(float); float logbf(float);
float lrintf(float); float lroundf(float);
float modff(float,float*); float nearbyintf(float);
float nextafterf(float,float); float nexttowardf(float,long double);
float powf(float,float); float remainderf(float,float); float remquof(float,float,int*);
float rintf(float); float roundf(float);
float scalblnf(float,long); float scalbnf(float,int);
float sinhf(float); float sqrtf(float); float tanhf(float);
float tgammaf(float); float truncf(float);
long double acosl(long double); long double asinl(long double); long double atanl(long double);
long double atan2l(long double,long double);
long double ceill(long double); long double cosl(long double); long double coshl(long double);
long double expl(long double); long double fabsl(long double);
long double floorl(long double); long double fmodl(long double,long double);
long double frexpl(long double,int*); long double ldexpl(long double,int);
long double logl(long double); long double log10l(long double);
long double modfl(long double,long double*);
long double powl(long double,long double);
long double sinl(long double); long double sinhl(long double);
long double sqrtl(long double); long double tanl(long double); long double tanhl(long double);
long double acoshl(long double); long double asinhl(long double); long double atanhl(long double);
long double cbrtl(long double); long double copysignl(long double,long double);
long double erfl(long double); long double erfcl(long double);
long double exp2l(long double); long double expm1l(long double);
long double fdiml(long double,long double); long double fmal(long double,long double,long double);
long double fmaxl(long double,long double); long double fminl(long double,long double);
long double hypotl(long double,long double); long double ilogbl(long double);
long double lgammal(long double);
long double llrintl(long double); long double llroundl(long double);
long double log1pl(long double); long double log2l(long double); long double logbl(long double);
long double lrintl(long double); long double lroundl(long double);
long double nearbyintl(long double);
long double nextafterl(long double,long double); long double nexttowardl(long double,long double);
long double remainderl(long double,long double); long double remquol(long double,long double,int*);
long double rintl(long double); long double roundl(long double);
long double scalblnl(long double,long); long double scalbnl(long double,int);
long double tgammal(long double); long double truncl(long double);
double nan(const char*); float nanf(const char*); long double nanl(const char*);

/* ── wchar.h ── */
typedef struct { int count; wchar_t *ptr; int state; } mbstate_t;
#define WEOF ((wint_t)-1)
wchar_t *wcscat(wchar_t *, const wchar_t *);
wchar_t *wcschr(const wchar_t *, wchar_t);
int wcscmp(const wchar_t *, const wchar_t *);
int wcscoll(const wchar_t *, const wchar_t *);
wchar_t *wcscpy(wchar_t *, const wchar_t *);
size_t wcscspn(const wchar_t *, const wchar_t *);
size_t wcslen(const wchar_t *);
wchar_t *wcsncat(wchar_t *, const wchar_t *, size_t);
int wcsncmp(const wchar_t *, const wchar_t *, size_t);
wchar_t *wcsncpy(wchar_t *, const wchar_t *, size_t);
wchar_t *wcspbrk(const wchar_t *, const wchar_t *);
wchar_t *wcsrchr(const wchar_t *, wchar_t);
size_t wcsspn(const wchar_t *, const wchar_t *);
wchar_t *wcsstr(const wchar_t *, const wchar_t *);
double wcstod(const wchar_t *, wchar_t **);
float wcstof(const wchar_t *, wchar_t **);
long double wcstold(const wchar_t *, wchar_t **);
wchar_t *wcstok(wchar_t *, const wchar_t *, wchar_t **);
long wcstol(const wchar_t *, wchar_t **, int);
long long wcstoll(const wchar_t *, wchar_t **, int);
unsigned long wcstoul(const wchar_t *, wchar_t **, int);
unsigned long long wcstoull(const wchar_t *, wchar_t **, int);
size_t wcsxfrm(wchar_t *, const wchar_t *, size_t);
int wmemcmp(const wchar_t *, const wchar_t *, size_t);
wchar_t *wmemcpy(wchar_t *, const wchar_t *, size_t);
wchar_t *wmemmove(wchar_t *, const wchar_t *, size_t);
wchar_t *wmemset(wchar_t *, wchar_t, size_t);
wchar_t *wmemchr(const wchar_t *, wchar_t, size_t);
wint_t btowc(int);
int wctob(wint_t);
int fwide(FILE *, int);
int fwprintf(FILE *, const wchar_t *, ...);
int fwscanf(FILE *, const wchar_t *, ...);
int swprintf(wchar_t *, size_t, const wchar_t *, ...);
int swscanf(const wchar_t *, const wchar_t *, ...);
int vfwprintf(FILE *, const wchar_t *, __builtin_va_list);
int vswprintf(wchar_t *, size_t, const wchar_t *, __builtin_va_list);
int vfwscanf(FILE *, const wchar_t *, __builtin_va_list);
int vswscanf(const wchar_t *, const wchar_t *, __builtin_va_list);
int vwprintf(const wchar_t *, __builtin_va_list);
int vwscanf(const wchar_t *, __builtin_va_list);
int wprintf(const wchar_t *, ...);
int wscanf(const wchar_t *, ...);
int getwc(FILE *);
int getwchar(void);
int putwc(wchar_t, FILE *);
int putwchar(wchar_t);
int ungetwc(wint_t, FILE *);
size_t mbrlen(const char *, size_t, mbstate_t *);
size_t mbrtowc(wchar_t *, const char *, size_t, mbstate_t *);
int mbsinit(const mbstate_t *);
size_t mbsrtowcs(wchar_t *, const char **, size_t, mbstate_t *);
size_t wcrtomb(char *, wchar_t, mbstate_t *);
size_t wcsrtombs(char *, const wchar_t **, size_t, mbstate_t *);
size_t wcsftime(wchar_t *, size_t, const wchar_t *, const struct tm *);

/* ── signal.h ── */
typedef int sig_atomic_t;
#define SIG_DFL ((void(*)(int))0)
#define SIG_IGN ((void(*)(int))1)
#define SIG_ERR ((void(*)(int))-1)
void (*signal(int, void (*)(int)))(int);
int raise(int);

/* ── setjmp.h ── */
typedef long jmp_buf[16];
int setjmp(jmp_buf);
void longjmp(jmp_buf, int);

/* ── locale.h ── */
typedef struct __locale_struct *__locale_t;
typedef __locale_t __c_locale;
typedef __locale_t locale_t;
locale_t newlocale(int, const char *, locale_t);
locale_t duplocale(locale_t);
void freelocale(locale_t);
locale_t uselocale(locale_t);
struct lconv {
    char *decimal_point; char *thousands_sep;
    char *grouping; char *mon_decimal_point;
    char *mon_thousands_sep; char *mon_grouping;
    char *positive_sign; char *negative_sign;
    char *int_frac_digits; char *frac_digits;
    char *p_cs_precedes; char *p_sep_by_space;
    char *n_cs_precedes; char *n_sep_by_space;
    char *p_sign_posn; char *n_sign_posn;
    char *int_curr_symbol; char *currency_symbol;
    char *int_n_cs_precedes; char *int_n_sep_by_space;
    char *int_n_sign_posn; char *int_p_cs_precedes;
    char *int_p_sep_by_space; char *int_p_sign_posn;
};
#define LC_ALL 0
#define LC_COLLATE 1
#define LC_CTYPE 2
#define LC_MONETARY 3
#define LC_NUMERIC 4
#define LC_TIME 5
#define LC_MESSAGES 6
char *setlocale(int, const char *);
struct lconv *localeconv(void);

/* ── uchar.h ── */
/* char16_t and char32_t are built-in in C++17 */

/* ── unistd.h ── */
unsigned int sleep(unsigned int);
int close(int);
ssize_t read(int, void *, size_t);
ssize_t write(int, const void *, size_t);
off_t lseek(int, off_t, int);

/* ── errno.h ── */
extern int errno;
#define EDOM 33
#define ERANGE 34
#define EINVAL 22
#define EAGAIN 11
#define EINTR 4
#define EBADF 9
#define EACCES 13
#define ENOENT 2
#define ENOMEM 12
#define EEXIST 17
#define EIO 5
#define ENOSPC 28
#define E2BIG 7
#define EAFNOSUPPORT 97
#define EADDRINUSE 98
#define EADDRNOTAVAIL 99
#define EISCONN 106
#define EFAULT 14
#define EPIPE 32
#define ECONNABORTED 103
#define EALREADY 114
#define ECONNREFUSED 111
#define ECONNRESET 104
#define EXDEV 18
#define EDESTADDRREQ 89
#define EBUSY 16
#define ENOTEMPTY 39
#define ENOEXEC 8
#define EFBIG 27
#define ENAMETOOLONG 36
#define ENOSYS 38
#define EHOSTUNREACH 113
#define EIDRM 43
#define EILSEQ 84
#define ENOTTY 25
#define ESPIPE 29
#define EISDIR 21
#define EMLINK 31
#define EMSGSIZE 90
#define ENETDOWN 100
#define ENETRESET 102
#define ENETUNREACH 101
#define ENOBUFS 105
#define ENODATA 120
#define ENODEV 19
#define ENOLCK 37
#define ENOMSG 42
#define ENOPROTOOPT 92
#define ENOSR 63
#define ENOSTR 60
#define ENOTCONN 107
#define ENOTRECOVERABLE 131
#define ENOTSOCK 88
#define ENOTSUP 95
#define EOPNOTSUPP 95
#define EOVERFLOW 75
#define EOWNERDEAD 130
#define EPERM 1
#define EPROTO 71
#define EPROTONOSUPPORT 93
#define EPROTOTYPE 91
#define EROFS 30
#define ETIME 62
#define ETIMEDOUT 110
#define ETXTBSY 26
#define EWOULDBLOCK 11
#define ECHILD 10
#define EDEADLK 35
#define EINPROGRESS 115
#define ELOOP 40
#define EMFILE 24
#define ENFILE 23
#define ENOLINK 67
#define ENOTDIR 20
#define ENXIO 6
#define ESRCH 3
#define ECANCELED 125

/* ── sched.h ── */
typedef unsigned long cpu_set_t;
#define CPU_SET(cpu, cpusetp) ((void)0)
#define CPU_ZERO(cpusetp) ((void)0)

#ifdef __cplusplus
}
#endif

#endif /* CXX_COMPAT_H */

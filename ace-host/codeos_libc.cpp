// The plain-C half of the freestanding runtime for the CodeOS build.
//
// This file deliberately includes NO standard headers. It has to *define* the
// libc symbols the static archives reach for (`strchr`, `fputc`, `stderr`,
// pthread stubs, the stack-canary hook), and the hosted headers declare several
// of those with signatures that a freestanding definition cannot match --
// `strchr` in particular is const-correct in C++ and `stderr` is a `FILE*`
// macro. Keeping this TU header-free is what lets it define them at all; see
// codeos_runtime.cpp for the C++ half (operator new over the arena below, the
// C++ ABI hooks, and the TLS-free std::string members).
//
// The allocator is a static bump arena: nothing here is ever freed (the process
// lays out one scene and exits), which is why `free` is a no-op and `realloc`
// copies rather than resizes. `operator new` in codeos_runtime.cpp forwards to
// `malloc`, so the whole payload lives in this one 8 MiB block.

using size = __SIZE_TYPE__;

// --------------------------------------------------------------------------
// Bump arena, wrapped as malloc/free so the libstdc++ objects that reach for
// malloc directly still work.
// --------------------------------------------------------------------------
static unsigned char g_heap[8 << 20];
static size g_used;

static size align_up(size n)
{
    return (n + 15u) & ~size(15);
}

extern "C" void* malloc(size n)
{
    void* p = g_heap + g_used;
    g_used += align_up(n);
    return p;
}

extern "C" void* calloc(size n, size m)
{
    void* p = malloc(n * m);
    unsigned char* q = static_cast<unsigned char*>(p);
    for (size i = 0; i < n * m; ++i) {
        q[i] = 0;
    }
    return p;
}

extern "C" void* realloc(void* p, size n)
{
    void* q = malloc(n);
    if (p != nullptr) {
        unsigned char* a = static_cast<unsigned char*>(p);
        unsigned char* b = static_cast<unsigned char*>(q);
        for (size i = 0; i < n; ++i) {
            b[i] = a[i];
        }
    }
    return q;
}

extern "C" void free(void*) {}

// --------------------------------------------------------------------------
// The C string/memory functions the engine and libstdc++ archive call.
// --------------------------------------------------------------------------
extern "C" void* memcpy(void* d, const void* s, size n)
{
    char* dp = static_cast<char*>(d);
    const char* sp = static_cast<const char*>(s);
    for (size i = 0; i < n; ++i) {
        dp[i] = sp[i];
    }
    return d;
}

extern "C" void* memmove(void* d, const void* s, size n)
{
    char* dp = static_cast<char*>(d);
    const char* sp = static_cast<const char*>(s);
    if (dp < sp) {
        for (size i = 0; i < n; ++i) {
            dp[i] = sp[i];
        }
    } else {
        for (size i = n; i > 0; --i) {
            dp[i - 1] = sp[i - 1];
        }
    }
    return d;
}

extern "C" void* memset(void* d, int c, size n)
{
    unsigned char* dp = static_cast<unsigned char*>(d);
    for (size i = 0; i < n; ++i) {
        dp[i] = static_cast<unsigned char>(c);
    }
    return d;
}

extern "C" int memcmp(const void* a, const void* b, size n)
{
    const unsigned char* x = static_cast<const unsigned char*>(a);
    const unsigned char* y = static_cast<const unsigned char*>(b);
    for (size i = 0; i < n; ++i) {
        if (x[i] != y[i]) {
            return x[i] < y[i] ? -1 : 1;
        }
    }
    return 0;
}

extern "C" size strlen(const char* s)
{
    size n = 0;
    while (s[n] != 0) {
        ++n;
    }
    return n;
}

extern "C" int strcmp(const char* a, const char* b)
{
    while (*a != 0 && *a == *b) {
        ++a;
        ++b;
    }
    return static_cast<unsigned char>(*a) - static_cast<unsigned char>(*b);
}

extern "C" int strncmp(const char* a, const char* b, size n)
{
    for (size i = 0; i < n; ++i) {
        if (a[i] != b[i]) {
            return static_cast<unsigned char>(a[i]) - static_cast<unsigned char>(b[i]);
        }
        if (a[i] == 0) {
            break;
        }
    }
    return 0;
}

extern "C" char* strchr(const char* s, int c)
{
    for (;; ++s) {
        if (*s == static_cast<char>(c)) {
            return const_cast<char*>(s);
        }
        if (*s == 0) {
            return nullptr;
        }
    }
}

extern "C" unsigned long __isoc23_strtoul(const char* s, char** e, int base)
{
    (void)e;
    (void)base;
    unsigned long v = 0;
    while (*s >= '0' && *s <= '9') {
        v = v * 10 + static_cast<unsigned long>(*s - '0');
        ++s;
    }
    return v;
}

// --------------------------------------------------------------------------
// stdio / gettext / pthread / loader hooks pulled in by the static archives.
// None of these are on the layout happy path; they exist so the link resolves.
// --------------------------------------------------------------------------
extern "C" int __sprintf_chk(char*, int, size, const char*, ...)
{
    return 0;
}
extern "C" char* secure_getenv(const char*)
{
    return nullptr;
}
extern "C" void __stack_chk_fail(void)
{
    // A `%fs:0x28` read cannot happen in a canary-free, TLS-free build; if one
    // ever reaches here the process has no way to report it, so park it.
    for (;;) {
    }
}
extern "C" int fputc(int, void*)
{
    return 0;
}
extern "C" int fputs(const char*, void*)
{
    return 0;
}
extern "C" size fwrite(const void*, size, size, void*)
{
    return 0;
}
extern "C" void* stderr = nullptr;
extern "C" void* stdout = nullptr;
extern "C" void* stdin = nullptr;
extern "C" int _dl_find_object(void*, void*)
{
    return -1;
}
extern "C" char* gettext(const char*)
{
    return const_cast<char*>("");
}
extern "C" char __libc_single_threaded = 1;
extern "C" int pthread_mutex_lock(void*)
{
    return 0;
}
extern "C" int pthread_mutex_unlock(void*)
{
    return 0;
}
extern "C" int pthread_cond_wait(void*, void*)
{
    return 0;
}
extern "C" int pthread_cond_broadcast(void*)
{
    return 0;
}
extern "C" int pthread_once(void*, void (*)(void))
{
    return 0;
}

// --------------------------------------------------------------------------
// The rest of what libstdc++'s always-pulled support objects reference. Every
// one is a stub or a trivial correct implementation: they are off the layout
// happy path, which is exactly why they can be inert.
// --------------------------------------------------------------------------
extern "C" void* memchr(const void* s, int c, size n)
{
    const unsigned char* p = static_cast<const unsigned char*>(s);
    for (size i = 0; i < n; ++i) {
        if (p[i] == static_cast<unsigned char>(c)) {
            return const_cast<unsigned char*>(p + i);
        }
    }
    return nullptr;
}

extern "C" char* strerror_r(int, char* buf, size n)
{
    if (n > 0) {
        buf[0] = 0;
    }
    return buf;
}

extern "C" int* __errno_location(void)
{
    static int err;
    return &err;
}

extern "C" void* __uselocale(void*)
{
    return nullptr;
}

extern "C" int vsnprintf(char*, size, const char*, __builtin_va_list)
{
    return 0;
}

extern "C" int __fprintf_chk(void*, int, const char*, ...)
{
    return 0;
}

// Wide-character helpers the locale facets call. wchar_t is 32-bit on Linux,
// so `int` is the correct element type here.
extern "C" int* wmemcpy(int* d, const int* s, size n)
{
    for (size i = 0; i < n; ++i) {
        d[i] = s[i];
    }
    return d;
}

extern "C" int* __wmemcpy_chk(int* d, const int* s, size n, size)
{
    return wmemcpy(d, s, n);
}

extern "C" int* wmemmove(int* d, const int* s, size n)
{
    if (d < s) {
        for (size i = 0; i < n; ++i) {
            d[i] = s[i];
        }
    } else {
        for (size i = n; i > 0; --i) {
            d[i - 1] = s[i - 1];
        }
    }
    return d;
}

extern "C" int* wmemset(int* d, int c, size n)
{
    for (size i = 0; i < n; ++i) {
        d[i] = c;
    }
    return d;
}

extern "C" int* __wmemset_chk(int* d, int c, size n, size)
{
    return wmemset(d, c, n);
}

extern "C" int* wmemchr(const int* s, int c, size n)
{
    for (size i = 0; i < n; ++i) {
        if (s[i] == c) {
            return const_cast<int*>(s + i);
        }
    }
    return nullptr;
}

extern "C" size wcslen(const int* s)
{
    size n = 0;
    while (s[n] != 0) {
        ++n;
    }
    return n;
}

// --------------------------------------------------------------------------
// The glibc surface libstdc++'s iostream/locale objects reference. All of it is
// the `std::stringstream`/facet machinery the engine's logging/serialisation
// code links but the layout path never runs, so the stubs may be inert. The few
// that could sit under layout (`floorf`, `round`) are implemented, not faked.
// --------------------------------------------------------------------------
extern "C" void* __dso_handle = nullptr;
extern "C" int atexit(void (*)(void))
{
    return 0;
}
extern "C" int __cxa_thread_atexit_impl(void (*)(void*), void*, void*)
{
    return 0;
}

extern "C" char* strdup(const char* s)
{
    size n = strlen(s) + 1;
    char* p = static_cast<char*>(malloc(n));
    for (size i = 0; i < n; ++i) {
        p[i] = s[i];
    }
    return p;
}

extern "C" char* strrchr(const char* s, int c)
{
    const char* last = nullptr;
    for (;; ++s) {
        if (*s == static_cast<char>(c)) {
            last = s;
        }
        if (*s == 0) {
            break;
        }
    }
    return const_cast<char*>(last);
}

extern "C" double strtod(const char*, char** end)
{
    if (end) {
        *end = nullptr;
    }
    return 0.0;
}

extern "C" float floorf(float x)
{
    long long i = static_cast<long long>(x);
    if (static_cast<float>(i) > x) {
        --i;
    }
    return static_cast<float>(i);
}

extern "C" double round(double x)
{
    return x >= 0.0 ? static_cast<double>(static_cast<long long>(x + 0.5))
                    : static_cast<double>(static_cast<long long>(x - 0.5));
}

extern "C" void* __memcpy_chk(void* d, const void* s, size n, size)
{
    return memcpy(d, s, n);
}

extern "C" void* __newlocale(int, const char*, void*)
{
    static int dummy;
    return &dummy;
}
extern "C" void __freelocale(void*) {}
extern "C" void* __duplocale(void*)
{
    static int dummy;
    return &dummy;
}
extern "C" const char* __nl_langinfo_l(int, void*)
{
    return "";
}
extern "C" char* bind_textdomain_codeset(const char*, const char*)
{
    return nullptr;
}
extern "C" char* dgettext(const char*, const char* msg)
{
    return const_cast<char*>(msg);
}
extern "C" int __ctype_get_mb_cur_max(void)
{
    return 1;
}

extern "C" int btowc(int c)
{
    return c == -1 ? -1 : static_cast<unsigned char>(c);
}
extern "C" int wctob(int c)
{
    return (c >= 0 && c < 256) ? c : -1;
}
extern "C" size mbrtowc(int*, const char*, size, void*)
{
    return static_cast<size>(-1);
}
extern "C" size wcrtomb(char*, int, void*)
{
    return static_cast<size>(-1);
}
extern "C" size mbsnrtowcs(int*, const char**, size, size, void*)
{
    return static_cast<size>(-1);
}
extern "C" size wcsnrtombs(char*, const int**, size, size, void*)
{
    return static_cast<size>(-1);
}
extern "C" size __mbsrtowcs_chk(int*, const char**, size, void*, size)
{
    return static_cast<size>(-1);
}

extern "C" int __iswctype_l(int, int, void*)
{
    return 0;
}
extern "C" int __wctype_l(const char*, void*)
{
    return 0;
}
extern "C" int __towlower_l(int c, void*)
{
    return c;
}
extern "C" int __towupper_l(int c, void*)
{
    return c;
}
extern "C" int __strcoll_l(const char*, const char*, void*)
{
    return 0;
}
extern "C" size __strxfrm_l(char* d, const char* s, size n, void*)
{
    const size l = strlen(s);
    if (d && n > 0) {
        const size m = l < n - 1 ? l : n - 1;
        for (size i = 0; i < m; ++i) {
            d[i] = s[i];
        }
        d[m] = 0;
    }
    return l;
}
extern "C" size __strftime_l(char*, size, const char*, const void*, void*)
{
    return 0;
}
extern "C" int __wcscoll_l(const int*, const int*, void*)
{
    return 0;
}
extern "C" size __wcsxfrm_l(int*, const int*, size, void*)
{
    return 0;
}
extern "C" size __wcsftime_l(int*, size, const int*, const void*, void*)
{
    return 0;
}

extern "C" void* iconv_open(const char*, const char*)
{
    static int dummy;
    return &dummy;
}
extern "C" size iconv(void*, char**, size*, char**, size*)
{
    return static_cast<size>(-1);
}
extern "C" int iconv_close(void*)
{
    return 0;
}

// FILE-based stdio the C++ stream buffers reach for. A raw-syscall process has
// no FILE objects at all, so every one of these is inert.
extern "C" int fflush(void*)
{
    return 0;
}
extern "C" size fread(void*, size, size, void*)
{
    return 0;
}
extern "C" long long fseeko64(void*, long long, int)
{
    return -1;
}
extern "C" long long ftello64(void*)
{
    return -1;
}
extern "C" int getc(void*)
{
    return -1;
}
extern "C" int getwc(void*)
{
    return -1;
}
extern "C" int putc(int, void*)
{
    return 0;
}
extern "C" int putwc(int, void*)
{
    return 0;
}
extern "C" int ungetc(int, void*)
{
    return -1;
}
extern "C" int ungetwc(int, void*)
{
    return -1;
}
extern "C" double __strtod_l(const char*, char**, void*)
{
    return 0.0;
}
extern "C" float __strtof_l(const char*, char**, void*)
{
    return 0.0f;
}
extern "C" long double strtold_l(const char*, char**, void*)
{
    return 0.0L;
}
extern "C" int wcscmp(const int* a, const int* b)
{
    while (*a != 0 && *a == *b) {
        ++a;
        ++b;
    }
    return (*a > *b) - (*a < *b);
}

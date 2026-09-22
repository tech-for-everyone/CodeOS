extern "C" {
#include "sched.h"
#include "string.h"
}
extern "C" double floor(double x);
extern "C" double fmod(double x, double y);

extern "C" void kprintf(const char *fmt, ...);
extern "C" unsigned long pmm_count_free(void);
extern "C" void codeos_dump_heap_corrupt(void);

extern "C" void __cxa_pure_virtual(void) {
    while (1) __builtin_trap();
}

void *__dso_handle = (void*)&__dso_handle;

/* ── C++ allocation ── */
extern "C" void free(void *p);
void operator delete(void *p) { free(p); }
void operator delete[](void *p) { free(p); }
void operator delete(void *p, unsigned long) { free(p); }
void operator delete[](void *, unsigned long) {}

extern "C" void *malloc(unsigned long);

void *operator new(unsigned long sz) {
    void *p = malloc(sz);
    if (!p) {
        kprintf("!! operator new(%lu) FAILED caller=%p pmm_free_pages=%lu\n",
                sz, __builtin_return_address(0), pmm_count_free());
        codeos_dump_heap_corrupt();
        while (1) __builtin_trap();
    }
    return p;
}
void *operator new[](unsigned long sz) { return operator new(sz); }
void *operator new(unsigned long sz, void *p) { (void)sz; return p; }

/* ── C++ ABI: guards (thread-safe statics) ── */
/* Single-threaded: just track pending/complete in the guard word */
extern "C" int __cxa_guard_acquire(long long *g) {
    if (*g & 1) return 0;
    *g |= 2;
    return 1;
}
extern "C" void __cxa_guard_release(long long *g) { *g |= 1; }

/* ── C++ ABI: exceptions (never thrown, Qt6 libs reference them) ── */
extern "C" void __cxa_allocate_exception(void) {}
extern "C" void __cxa_throw(void) { while (1) __builtin_trap(); }
extern "C" void __cxa_begin_catch(void) {}
extern "C" void __cxa_end_catch(void) {}
extern "C" void __cxa_rethrow(void) { while (1) __builtin_trap(); }
extern "C" void __cxa_current_exception_type(void) {}
extern "C" void __cxa_throw_bad_array_new_length(void) { while (1) __builtin_trap(); }
extern "C" void __cxa_thread_atexit(void) {}
extern "C" void __cxa_call_terminate(void *unwind) { (void)unwind; while (1) __builtin_trap(); }

/* ── C++ std::exception stubs ── */
namespace std {
struct exception {
    virtual const char *what() const noexcept;
    virtual ~exception() = default;
};
const char *exception::what() const noexcept { return "std::exception"; }
void __throw_bad_function_call(void) { while (1) __builtin_trap(); }
void __glibcxx_assert_fail(char const *, int, char const *, char const *) {
    while (1) __builtin_trap();
}
void __throw_length_error(char const *) { while (1) __builtin_trap(); }
void __throw_bad_array_new_length(void) { while (1) __builtin_trap(); }
void __throw_out_of_range_fmt(char const *, ...) { while (1) __builtin_trap(); }
void __throw_logic_error(char const *) { while (1) __builtin_trap(); }
void __throw_out_of_range(char const *) { while (1) __builtin_trap(); }
void __throw_bad_cast(void) { while (1) __builtin_trap(); }
void __throw_bad_typeid(void) { while (1) __builtin_trap(); }
void __throw_invalid_argument(char const *) { while (1) __builtin_trap(); }
void __throw_system_error(int) { while (1) __builtin_trap(); }

/* pmr */
namespace pmr {
struct memory_resource {
    virtual ~memory_resource() = default;
    virtual void *do_allocate(unsigned long, unsigned long) = 0;
    virtual void do_deallocate(void *, unsigned long, unsigned long) = 0;
    virtual bool do_is_equal(const memory_resource &) const noexcept = 0;
};
struct monotonic_buffer_resource : memory_resource {
    monotonic_buffer_resource() : next_buffer(0), current_buffer(0), current_pos(0), remaining(0) {}
    ~monotonic_buffer_resource() {}
    void *_M_new_buffer(unsigned long sz, unsigned long align);
    void _M_release_buffers();
    void *do_allocate(unsigned long sz, unsigned long align) override;
    void do_deallocate(void *, unsigned long, unsigned long) override {}
    bool do_is_equal(const memory_resource &o) const noexcept override { return this == &o; }
    memory_resource *next_buffer;
    void *current_buffer;
    unsigned long current_pos;
    unsigned long remaining;
};
memory_resource *get_default_resource() {
    static monotonic_buffer_resource default_res;
    return &default_res;
}
void *monotonic_buffer_resource::_M_new_buffer(unsigned long sz, unsigned long al) {
    (void)sz; (void)al; return 0;
}
void monotonic_buffer_resource::_M_release_buffers() {}
void *monotonic_buffer_resource::do_allocate(unsigned long sz, unsigned long al) {
    (void)sz; (void)al; return 0;
}
} /* std::pmr */
/* hashtable / rb-tree detail */
namespace __detail {
struct _Prime_rehash_policy {
    unsigned long _M_next_bkt(unsigned long n) const;
    struct _need_rehash_ret { bool first; unsigned long second; };
    _need_rehash_ret _M_need_rehash(unsigned long, unsigned long, unsigned long) const;
};
unsigned long _Prime_rehash_policy::_M_next_bkt(unsigned long n) const {
    return n < 2 ? 2 : n * 2 + 1;
}
_Prime_rehash_policy::_need_rehash_ret _Prime_rehash_policy::_M_need_rehash(unsigned long, unsigned long, unsigned long) const {
    return _need_rehash_ret{false, 0};
}
}
struct _Rb_tree_node_base { _Rb_tree_node_base *parent, *left, *right; unsigned color; };
_Rb_tree_node_base *_Rb_tree_decrement(_Rb_tree_node_base *x) {
    if (x->left) { x = x->left; while (x->right) x = x->right; return x; }
    while (x->parent && x == x->parent->left) x = x->parent;
    return x->parent;
}
_Rb_tree_node_base *_Rb_tree_increment(_Rb_tree_node_base *x) {
    if (x->right) { x = x->right; while (x->left) x = x->left; return x; }
    while (x->parent && x == x->parent->right) x = x->parent;
    return x->parent;
}
void _Rb_tree_insert_and_rebalance(bool ins, _Rb_tree_node_base *x, _Rb_tree_node_base *p, _Rb_tree_node_base &h) {
    (void)ins; (void)x; (void)p; (void)h;
}
void _Rb_tree_rebalance_for_erase(_Rb_tree_node_base *z, _Rb_tree_node_base &h) {
    (void)z; (void)h;
}
} /* std */

/* operator new(std::nothrow_t) */
namespace std {
struct nothrow_t {};
}
void *operator new(unsigned long sz, const std::nothrow_t &) noexcept { return malloc(sz); }

/* C++17 aligned-new variants */
void *operator new(unsigned long sz, std::align_val_t) { return operator new(sz); }
void *operator new[](unsigned long sz, std::align_val_t) { return operator new(sz); }
void *operator new(unsigned long sz, std::align_val_t, const std::nothrow_t &) noexcept { return malloc(sz); }
void operator delete(void *p, std::align_val_t) noexcept { free(p); }
void operator delete[](void *p, std::align_val_t) noexcept { free(p); }
void operator delete(void *p, unsigned long, std::align_val_t) noexcept { free(p); }
void operator delete[](void *, unsigned long, std::align_val_t) noexcept {}

/* Qt-internal stubs for dropped unix filesystem engine */
extern "C" char _ZN17QFileSystemEngine12fillMetaDataERK16QFileSystemEntryR19QFileSystemMetaData6QFlagsINS3_12MetaDataFlagEE(void) { return 0; }
extern "C" void *_ZN17QFileSystemEngine15resolveUserNameEj(void) { return 0; }

/* Qt-internal stubs for dropped unix process engine */
extern "C" void _ZN15QProcessPrivate12startProcessEv(void) {}
extern "C" void _ZN15QProcessPrivate11killProcessEv(void) {}
extern "C" void _ZN15QProcessPrivate7cleanupEv(void) {}
extern "C" char _ZN15QProcessPrivate14waitForStartedERK14QDeadlineTimer(void) { return 1; }
extern "C" char _ZN15QProcessPrivate15waitForFinishedERK14QDeadlineTimer(void) { return 1; }
extern "C" char _ZN15QProcessPrivate19waitForBytesWrittenERK14QDeadlineTimer(void) { return 1; }
extern "C" char _ZN15QProcessPrivate16waitForReadyReadERK14QDeadlineTimer(void) { return 1; }
extern "C" void _ZN15QProcessPrivate11openChannelERNS_7ChannelE(void) {}
extern "C" void _ZN15QProcessPrivate12closeChannelEPNS_7ChannelE(void) {}
extern "C" char _ZN15QProcessPrivate13startDetachedEPx(void) { return 0; }
extern "C" void _ZN15QProcessPrivate14processStartedEP7QString(void) {}
extern "C" void *_ZN15QProcessPrivate15readFromChannelEPKNS_7ChannelEPcx(void) { return 0; }
extern "C" void _ZN15QProcessPrivate16terminateProcessEv(void) {}
extern "C" char _ZN15QProcessPrivate16waitForDeadChildEv(void) { return 1; }
extern "C" void *_ZNK15QProcessPrivate23bytesAvailableInChannelEPKNS_7ChannelE(void) { return 0; }
extern "C" void *_ZN19QProcessEnvironment17systemEnvironmentEv(void) { return 0; }
extern "C" void *_ZN8QProcess9writeDataEPKcx(void) { return 0; }

/* ── exception_ptr stubs ── */
extern "C" void _ZNSt15__exception_ptr13exception_ptr9_M_addrefEv(void) {}
extern "C" void _ZNSt15__exception_ptr13exception_ptr10_M_releaseEv(void) {}
extern "C" void _ZSt17current_exceptionv(void) {}
extern "C" void _ZSt17rethrow_exceptionNSt15__exception_ptr13exception_ptrE(void) {}

/* ── std::locale classic C-locale (matches libstdc++ GCC-16 x86_64 layout
      so Qt's bundled double_conversion can run use_facet<ctype<char>>) ── */
namespace codeos_locale {
struct locale_id {          /* std::locale::id: 8 bytes */
    unsigned long index;    /* _M_index (mutable) */
};
struct ctype_obj {          /* std::ctype<char>: 576 bytes */
    void *vptr;                     /* 0  */
    unsigned int refcount;          /* 8  */
    void *c_locale;                 /* 16 */
    unsigned char del;              /* 24 */
    const int *toupper_tab;         /* 32 */
    const int *tolower_tab;         /* 40 */
    const unsigned short *mask_tab; /* 48 */
    unsigned char widen_ok;         /* 56 */
    char widen[256];                /* 57 */
    char narrow[256];               /* 313 */
    unsigned char narrow_ok;        /* 569 */
};
struct locale_impl {        /* std::locale::_Impl: 40 bytes */
    unsigned int refcount;
    const void **facets;
    unsigned long facets_size;
    const void **caches;
    char **names;
};
struct locale_obj {         /* std::locale: 8 bytes */
    locale_impl *impl;      /* _M_impl */
};

static void trap(void) { while (1) __builtin_trap(); }

static void *const ctype_vtbl[18] = {
    (void *)0, (void *)0,
    (void *)trap, (void *)trap, (void *)trap, (void *)trap, (void *)trap,
    (void *)trap, (void *)trap, (void *)trap, (void *)trap, (void *)trap,
    (void *)trap, (void *)trap, (void *)trap, (void *)trap, (void *)trap,
    (void *)trap,
};

static unsigned short classic_mask[256];
static int classic_toupper_tab[256];
static int classic_tolower_tab[256];
static ctype_obj classic_ct;
static const void *classic_facets[256];
static locale_impl classic_impl;
static char classic_locale_mem[sizeof(locale_obj)];
static int classic_ready;

/* glibc x86_64 ctype_base::mask bits (see ctype_base.h) */
enum {
    M_BLANK  = 1,      M_CNTRL = 2,     M_PUNCT = 4,
    M_UPPER  = 256,    M_LOWER = 512,   M_ALPHA = 1024,
    M_DIGIT  = 2048,   M_XDIGIT = 4096, M_SPACE = 8192,
    M_PRINT  = 16384,
    M_ALNUM  = 3072,   M_GRAPH = 3076,
};

static void build_classic_locale(void) {
    if (classic_ready) return;
    for (int i = 0; i < 256; i++) {
        unsigned short m = 0;
        int t = i;
        if (i >= '0' && i <= '9') {
            m = M_DIGIT | M_XDIGIT | M_ALNUM | M_GRAPH | M_PRINT;
        } else if (i >= 'a' && i <= 'z') {
            m = M_ALPHA | M_LOWER | M_ALNUM | M_GRAPH | M_PRINT;
            t = i - 32;
        } else if (i >= 'A' && i <= 'Z') {
            m = M_ALPHA | M_UPPER | M_ALNUM | M_GRAPH | M_PRINT;
            t = i + 32;
        } else if (i == ' ') {
            m = M_SPACE | M_BLANK | M_PRINT;
        } else if (i == '\t') {
            m = M_SPACE | M_BLANK | M_CNTRL;
        } else if (i >= '\n' && i <= '\r') {
            m = M_SPACE | M_CNTRL;
        } else if ((i < 0x20) || i == 0x7f) {
            m = M_CNTRL;
        } else if (i >= 0x80) {
            m = 0;
        } else {
            m = M_PRINT | M_GRAPH | M_PUNCT;
        }
        classic_mask[i] = m;
        classic_toupper_tab[i] = t;
        classic_tolower_tab[i] = (i >= 'A' && i <= 'Z') ? i + 32 : i;
    }
    classic_ct.vptr = (void *)ctype_vtbl;
    classic_ct.refcount = 0x7fffffff;
    classic_ct.c_locale = 0;
    classic_ct.del = 0;
    classic_ct.toupper_tab = classic_toupper_tab;
    classic_ct.tolower_tab = classic_tolower_tab;
    classic_ct.mask_tab = classic_mask;
    classic_ct.widen_ok = 0;
    classic_ct.narrow_ok = 0;
    for (int i = 0; i < 256; i++) classic_facets[i] = &classic_ct;
    classic_impl.refcount = 0x7fffffff;
    classic_impl.facets = classic_facets;
    classic_impl.facets_size = 256;
    classic_impl.caches = 0;
    classic_impl.names = 0;
    ((locale_obj *)classic_locale_mem)->impl = &classic_impl;
    classic_ready = 1;
}
} /* namespace codeos_locale */

extern "C" codeos_locale::locale_obj *_ZNSt6locale7classicEv(void) {
    codeos_locale::build_classic_locale();
    return (codeos_locale::locale_obj *)codeos_locale::classic_locale_mem;
}
extern "C" unsigned long _ZNKSt6locale2id5_M_idEv(codeos_locale::locale_id *self) {
    if (!self->index) {
        static unsigned long counter = 0;
        self->index = ++counter;
    }
    return self->index;
}
extern "C" int abs(int j) { return j < 0 ? -j : j; }

/* ── libstdc++ locale/ios machinery (iostream vtables come from
      stdcxx_support.cpp; these are the out-of-line non-template bits) ── */

/* std::locale::locale() default ctor: point at the immortal classic impl */
extern "C" void _ZNSt6localeC1Ev(codeos_locale::locale_obj *self) {
    codeos_locale::build_classic_locale();
    self->impl = &codeos_locale::classic_impl;
}
extern "C" void _ZNSt6localeC1ERKS_(codeos_locale::locale_obj *self, const codeos_locale::locale_obj *o) {
    self->impl = o->impl;
}
extern "C" void _ZNSt6localeD1Ev(codeos_locale::locale_obj *) {}
extern "C" void _ZNSt6localeaSERKS_(codeos_locale::locale_obj *self, const codeos_locale::locale_obj *o) {
    self->impl = o->impl;
}

extern "C" void _ZNSt6locale5facetD2Ev(void *) {}
extern "C" void *_ZNSt6locale5facet15_S_get_c_localeEv(void) { return 0; }
extern "C" void _ZNSt6locale5_Impl16_M_install_cacheEPKNS_5facetEm(void *, const void *, unsigned long) {}

extern "C" void _ZNKSt5ctypeIcE13_M_widen_initEv(codeos_locale::ctype_obj *self) {
    if (self->widen_ok) return;
    for (int i = 0; i < 256; i++) {
        self->widen[i] = (char)i;
        self->narrow[i] = (char)i;
    }
    self->widen_ok = 1;
    self->narrow_ok = 1;
}

extern "C" { unsigned char _ZNSt7__cxx118numpunctIcE2idE[8] = {0}; }

extern "C" {
    char _ZNSt10__num_base11_S_atoms_inE[63] =
        "0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ";
    char _ZNSt10__num_base12_S_atoms_outE[63] =
        "0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ";
}
extern "C" const char *_ZNSt10__num_base15_S_format_floatERKSt8ios_basePcc(void *, char, char) {
    static const char f[] = "%g";
    return f;
}
extern "C" void _ZSt14__convert_to_vIfEvPKcRT_RSt12_Ios_IostateRKP15__locale_struct(
    const char *, void *v, void *, const void *) {
    *(float *)v = 0;
}
extern "C" void _ZSt14__convert_to_vIdEvPKcRT_RSt12_Ios_IostateRKP15__locale_struct(
    const char *, void *v, void *, const void *) {
    *(double *)v = 0;
}
extern "C" void _ZSt14__convert_to_vIeEvPKcRT_RSt12_Ios_IostateRKP15__locale_struct(
    const char *, void *v, void *, const void *) {
    *(long double *)v = 0;
}
extern "C" char _ZSt17__verify_groupingPKcmRKNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEEE(
    const char *, unsigned long, const void *) { return 1; }
extern "C" int _ZSt18uncaught_exceptionv(void) { return 0; }
extern "C" void _ZSt19__throw_ios_failurePKc(char const *) { while (1) __builtin_trap(); }
extern "C" long _ZSt21__copy_streambufs_eofIcSt11char_traitsIcEElPSt15basic_streambufIT_T0_ES6_Rb(
    void *, void *, void *) { return 0; }

extern "C" void *_ZNSi7getlineEPclc(void *self, char *, long, char) { return self; }

extern "C" void _ZNSt8ios_baseC2Ev(void *) {}
extern "C" void _ZNSt8ios_base7_M_initEv(void *) {}
extern "C" void _ZNSt8ios_base17_M_call_callbacksENS_5eventE(void *, int) {}
extern "C" void _ZNSt8ios_base20_M_dispose_callbacksEv(void *) {}
extern "C" void _ZNSt8ios_base7_M_moveERS_(void *, void *) {}
extern "C" void _ZNSt8ios_base7_M_swapERS_(void *, void *) {}
extern "C" void _ZNSt8ios_base5imbueERKSt6locale(void *sret, void *, const void *) {
    ((char *)sret)[0] = 0;
    *(void **)sret = 0;
}

/* ── std::__cxx11::basic_string<char> out-of-line members ──
   Layout (libstdc++ ABI, 32 bytes):
     offset  0: char * _M_dataplus._M_p      (Allocator hider base is empty, EBO)
     offset  8: size_t _M_string_length
     offset 16: union { char _M_local_buf[16]; size_t _M_allocated_capacity; }
   _M_is_local(): _M_p == (char*)this + 16;  capacity(): local ? 15 : cap. */
struct codeos_string {
    char *_M_p;
    size_t _M_len;
    union {
        char _M_local[16];
        size_t _M_cap;
    };
};

#define CODEOS_STRING_LOCAL(cs) ((cs)->_M_p == (char *)(cs) + 16)
#define CODEOS_STRING_CAP(cs) (CODEOS_STRING_LOCAL(cs) ? 15u : (cs)->_M_cap)
#define CODEOS_STRING_SETLEN(cs, n)      \
    do {                                 \
        (cs)->_M_p[n] = 0;               \
        (cs)->_M_len = n;                \
    } while (0)
#define CODEOS_STRING_MAX ((size_t)-1 / 4)
#define CODEOS_STRING_NPOS ((size_t)-1)

/* Internal helpers */
static inline int codeos_string_is_local(const struct codeos_string *cs) {
    return CODEOS_STRING_LOCAL(cs);
}
static inline size_t codeos_string_cap(const struct codeos_string *cs) {
    return CODEOS_STRING_CAP(cs);
}
static inline void codeos_string_setlen(struct codeos_string *cs, size_t n) {
    cs->_M_p[n] = 0;
    cs->_M_len = n;
}

/* Bounds check helper - returns 1 if pos is valid, 0 otherwise */
static inline int codeos_string_check_pos(const struct codeos_string *cs, size_t pos) {
    return pos <= cs->_M_len;
}

/* Core allocation with growth policy matching libstdc++ */
static char *codeos_string_create(struct codeos_string *s, size_t *cap, size_t old_cap) {
    (void)s;
    if (*cap > CODEOS_STRING_MAX) *cap = CODEOS_STRING_MAX;
    if (*cap > old_cap && *cap < 2 * old_cap) {
        *cap = 2 * old_cap;
        if (*cap > CODEOS_STRING_MAX) *cap = CODEOS_STRING_MAX;
    }
    return (char *)operator new(*cap + 1);
}

/* Validation helper - traps on invalid pos */
static inline void codeos_string_validate_pos(const struct codeos_string *cs, size_t pos) {
    if (__builtin_expect(pos > cs->_M_len, 0)) {
        while (1) __builtin_trap(); /* out_of_range */
    }
}

extern "C" void _ZNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEE10_M_disposeEv(
    struct codeos_string *v) {
    if (!CODEOS_STRING_LOCAL(v)) operator delete(v->_M_p);
}

extern "C" void _ZNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEED1Ev(
    struct codeos_string *v) {
    if (!CODEOS_STRING_LOCAL(v)) operator delete(v->_M_p);
}

extern "C" char *_ZNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEE9_M_createERmm(
    struct codeos_string *v, size_t *cap, size_t old_cap) {
    return codeos_string_create(v, cap, old_cap);
}

extern "C" void _ZNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEE12_M_constructEmc(
    struct codeos_string *v, size_t n, char c) {
    struct codeos_string *s = v;
    if (n > 15) {
        size_t cap = n;
        s->_M_p = codeos_string_create(s, &cap, 0);
        s->_M_cap = cap;
    } else {
        s->_M_p = (char *)s + 16;
    }
    if (n) memset(s->_M_p, c, n);
    codeos_string_setlen(s, n);
}

extern "C" void _ZNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEE9_M_assignERKS4_(
    struct codeos_string *v, const struct codeos_string *ov) {
    struct codeos_string *s = v;
    const struct codeos_string *o = ov;
    if (s == o) return;
    size_t rsize = o->_M_len;
    size_t cap = CODEOS_STRING_CAP(s);
    if (rsize > cap) {
        size_t new_cap = rsize;
        char *tmp = codeos_string_create(s, &new_cap, cap);
        operator delete(s->_M_p);
        s->_M_p = tmp;
        s->_M_cap = new_cap;
    }
    if (rsize) memcpy(s->_M_p, o->_M_p, rsize);
    codeos_string_setlen(s, rsize);
}

extern "C" void _ZNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEE9_M_mutateEmmPKcm(
    struct codeos_string *v, size_t pos, size_t len1, const char *ss, size_t len2) {
    struct codeos_string *s = v;
    codeos_string_validate_pos(s, pos);
    size_t how_much = s->_M_len - pos - len1;
    size_t new_cap = s->_M_len + len2 - len1;
    size_t grown = new_cap;
    char *r = codeos_string_create(s, &grown, CODEOS_STRING_CAP(s));
    if (pos) memcpy(r, s->_M_p, pos);
    if (ss && len2) memcpy(r + pos, ss, len2);
    if (how_much) memcpy(r + pos + len2, s->_M_p + pos + len1, how_much);
    operator delete(s->_M_p);
    s->_M_p = r;
    s->_M_cap = grown;
}

extern "C" void *_ZNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEE14_M_replace_auxEmmmc(
    struct codeos_string *v, size_t pos, size_t n1, size_t n2, char c) {
    struct codeos_string *s = v;
    codeos_string_validate_pos(s, pos);
    size_t old_size = s->_M_len;
    size_t new_size = old_size + n2 - n1;
    if (new_size <= CODEOS_STRING_CAP(s)) {
        char *p = s->_M_p + pos;
        size_t how_much = old_size - pos - n1;
        if (how_much && n1 != n2) memmove(p + n2, p + n1, how_much);
    } else {
        _ZNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEE9_M_mutateEmmPKcm(
            s, pos, n1, 0, n2);
    }
    if (n2) memset(s->_M_p + pos, c, n2);
    codeos_string_setlen(s, new_size);
    return v;
}

extern "C" void *_ZNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEE10_M_replaceEmmPKcm(
    struct codeos_string *v, size_t pos, size_t len1, const char *ss, size_t len2) {
    struct codeos_string *s = v;
    codeos_string_validate_pos(s, pos);
    size_t old_size = s->_M_len;
    size_t new_size = old_size + len2 - len1;
    if (new_size <= CODEOS_STRING_CAP(s)) {
        char *p = s->_M_p + pos;
        size_t how_much = old_size - pos - len1;
        if (ss + len2 <= s->_M_p || ss >= s->_M_p + s->_M_len) {
            if (how_much && len1 != len2) memmove(p + len2, p + len1, how_much);
            if (len2) memcpy(p, ss, len2);
        } else {
            if (len2 && len2 <= len1) memmove(p, ss, len2);
            if (how_much && len1 != len2) memmove(p + len2, p + len1, how_much);
            if (len2 > len1) {
                if (ss + len2 <= p + len1)
                    memmove(p, ss, len2);
                else if (ss >= p + len1) {
                    size_t poff = (ss - p) + (len2 - len1);
                    memcpy(p, p + poff, len2);
                } else {
                    size_t nleft = (p + len1) - ss;
                    memmove(p, ss, nleft);
                    memcpy(p + nleft, p + len2, len2 - nleft);
                }
            }
        }
    } else {
        _ZNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEE9_M_mutateEmmPKcm(
            s, pos, len1, ss, len2);
    }
    codeos_string_setlen(s, new_size);
    return v;
}

extern "C" void _ZNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEE8_M_eraseEmm(
    struct codeos_string *v, size_t pos, size_t n) {
    struct codeos_string *s = v;
    codeos_string_validate_pos(s, pos);
    if (n == 0) return;
    size_t how_much = s->_M_len - pos - n;
    if (how_much && n) memmove(s->_M_p + pos, s->_M_p + pos + n, how_much);
    codeos_string_setlen(s, s->_M_len - n);
}

extern "C" void *_ZNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEE9_M_appendEPKcm(
    struct codeos_string *v, const char *ss, size_t n) {
    struct codeos_string *s = v;
    size_t len = n + s->_M_len;
    if (len <= CODEOS_STRING_CAP(s)) {
        if (n) memcpy(s->_M_p + s->_M_len, ss, n);
    } else {
        _ZNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEE9_M_mutateEmmPKcm(
            s, s->_M_len, 0, ss, n);
    }
    codeos_string_setlen(s, len);
    return v;
}

extern "C" void *_ZNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEE6appendEPKcm(
    struct codeos_string *v, const char *ss, size_t n) {
    return _ZNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEE9_M_appendEPKcm(v, ss, n);
}

extern "C" void _ZNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEE7reserveEm(
    struct codeos_string *v, size_t res) {
    struct codeos_string *s = v;
    size_t cap = CODEOS_STRING_CAP(s);
    if (res <= cap) return;
    size_t grown = res;
    char *tmp = codeos_string_create(s, &grown, cap);
    memcpy(tmp, s->_M_p, s->_M_len + 1);
    operator delete(s->_M_p);
    s->_M_p = tmp;
    s->_M_cap = grown;
}

extern "C" void _ZNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEE6resizeEmc(
    struct codeos_string *v, size_t n, char c) {
    struct codeos_string *s = v;
    size_t sz = s->_M_len;
    if (sz < n) {
        _ZNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEE14_M_replace_auxEmmmc(
            s, sz, 0, n - sz, c);
    } else if (n < sz) {
        s->_M_p[n] = 0;
        s->_M_len = n;
    }
}

extern "C" void _ZNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEE9push_backEc(
    struct codeos_string *v, char c) {
    struct codeos_string *s = v;
    size_t sz = s->_M_len;
    if (sz == CODEOS_STRING_CAP(s))
        _ZNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEE9_M_mutateEmmPKcm(
            s, sz, 0, 0, 1);
    s->_M_p[sz] = c;
    codeos_string_setlen(s, sz + 1);
}

extern "C" unsigned long _ZNKSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEE4copyEPcmm(
    const struct codeos_string *v, char *dst, size_t n, size_t pos) {
    const struct codeos_string *s = v;
    if (!codeos_string_check_pos(s, pos)) return CODEOS_STRING_NPOS;
    size_t rlen = s->_M_len - pos;
    if (n < rlen) rlen = n;
    if (rlen) memcpy(dst, s->_M_p + pos, rlen);
    return rlen;
}

extern "C" void _ZNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEE4swapERS4_(
    struct codeos_string *v, struct codeos_string *ov) {
    struct codeos_string *s = v, *o = ov;
    if (s == o) return;
    int s_local = CODEOS_STRING_LOCAL(s);
    int o_local = CODEOS_STRING_LOCAL(o);
    if (s_local) {
        if (o_local) {
            if (s->_M_len && o->_M_len) {
                char tmp[16];
                memcpy(tmp, o->_M_p, o->_M_len + 1);
                memcpy(o->_M_p, s->_M_p, s->_M_len + 1);
                memcpy(s->_M_p, tmp, o->_M_len + 1);
            } else if (o->_M_len) {
                s->_M_p = (char *)s + 16;
                memcpy(s->_M_p, o->_M_p, o->_M_len + 1);
                s->_M_len = o->_M_len;
                o->_M_p[0] = 0;
                o->_M_len = 0;
                return;
            } else if (s->_M_len) {
                o->_M_p = (char *)o + 16;
                memcpy(o->_M_p, s->_M_p, s->_M_len + 1);
                o->_M_len = s->_M_len;
                s->_M_p[0] = 0;
                s->_M_len = 0;
                return;
            }
        } else {
            size_t tmp_cap = o->_M_cap;
            char *o_heap = o->_M_p;
            memcpy((char *)o + 16, s->_M_p, s->_M_len + 1);
            s->_M_p = o_heap;
            o->_M_p = (char *)o + 16;
            s->_M_cap = tmp_cap;
        }
    } else {
        size_t tmp_cap = s->_M_cap;
        char *s_heap = s->_M_p;
        if (o_local) {
            memcpy((char *)s + 16, o->_M_p, o->_M_len + 1);
            o->_M_p = s_heap;
            s->_M_p = (char *)s + 16;
        } else {
            s->_M_p = o->_M_p;
            o->_M_p = s_heap;
            s->_M_cap = o->_M_cap;
        }
        o->_M_cap = tmp_cap;
    }
    size_t tl = s->_M_len;
    s->_M_len = o->_M_len;
    o->_M_len = tl;
}

extern "C" unsigned long _ZNKSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEE4findEcm(
    const struct codeos_string *v, char c, size_t pos) {
    const struct codeos_string *s = v;
    size_t size = s->_M_len;
    if (pos >= size) return CODEOS_STRING_NPOS;
    const char *data = s->_M_p;
    const char *p = (const char *)memchr(data + pos, c, size - pos);
    return p ? (size_t)(p - data) : CODEOS_STRING_NPOS;
}

extern "C" unsigned long _ZNKSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEE4findEPKcmm(
    const struct codeos_string *v, const char *ss, size_t pos, size_t n) {
    const struct codeos_string *s = v;
    size_t size = s->_M_len;
    if (n == 0) return pos <= size ? pos : CODEOS_STRING_NPOS;
    if (pos >= size) return CODEOS_STRING_NPOS;
    const char *data = s->_M_p;
    const char *first = data + pos;
    const char *last = data + size;
    size_t len = size - pos;
    while (len >= n) {
        first = (const char *)memchr(first, ss[0], len - n + 1);
        if (!first) return CODEOS_STRING_NPOS;
        if (memcmp(first, ss, n) == 0) return (size_t)(first - data);
        len = last - ++first;
    }
    return CODEOS_STRING_NPOS;
}

extern "C" unsigned long _ZNKSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEE5rfindEcm(
    const struct codeos_string *v, char c, size_t pos) {
    const struct codeos_string *s = v;
    size_t size = s->_M_len;
    const char *data = s->_M_p;
    if (size) {
        if (--size > pos) size = pos;
        for (++size; size-- > 0;)
            if (data[size] == c) return size;
    }
    return CODEOS_STRING_NPOS;
}

extern "C" unsigned long _ZNKSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEE13find_first_ofEPKcmm(
    const struct codeos_string *v, const char *ss, size_t pos, size_t n) {
    const struct codeos_string *s = v;
    for (; n && pos < s->_M_len; ++pos)
        if (memchr(ss, s->_M_p[pos], n)) return pos;
    return CODEOS_STRING_NPOS;
}

extern "C" unsigned long _ZNKSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEE17find_first_not_ofEPKcmm(
    const struct codeos_string *v, const char *ss, size_t pos, size_t n) {
    const struct codeos_string *s = v;
    for (; pos < s->_M_len; ++pos)
        if (!memchr(ss, s->_M_p[pos], n)) return pos;
    return CODEOS_STRING_NPOS;
}

extern "C" unsigned long _ZNKSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEE16find_last_not_ofEPKcmm(
    const struct codeos_string *v, const char *ss, size_t pos, size_t n) {
    const struct codeos_string *s = v;
    size_t size = s->_M_len;
    const char *data = s->_M_p;
    if (size) {
        if (--size > pos) size = pos;
        do {
            if (!memchr(ss, data[size], n)) return size;
        } while (size--);
    }
    return CODEOS_STRING_NPOS;
}

extern "C" void *__uselocale(void *) { return 0; }

static void codeos_emit(char **out, unsigned long *room, char c) {
    if (!*out) return;
    if (*room) {
        *(*out)++ = c;
        (*room)--;
    }
}

/* Helper: write string to output with padding/precision */

extern "C" int vsnprintf(char *s, unsigned long n, const char *fmt, va_list ap) {
    char *out = s;
    unsigned long room = n;
    int written = 0;

    while (*fmt) {
        if (*fmt != '%') {
            codeos_emit(&out, &room, *fmt++);
            written++;
            continue;
        }
        fmt++;
        int left = 0;
        while (*fmt == '-') { left = 1; fmt++; }
        int width = 0;
        while (*fmt >= '0' && *fmt <= '9') width = width * 10 + (*fmt++ - '0');
        int prec = -1;
        if (*fmt == '.') {
            fmt++;
            prec = 0;
            if (*fmt == '*') { prec = va_arg(ap, int); fmt++; }
            else while (*fmt >= '0' && *fmt <= '9') prec = prec * 10 + (*fmt++ - '0');
        }
        int lmod = 0;
        while (*fmt == 'l' || *fmt == 'z' || *fmt == 'h' || *fmt == 'j' || *fmt == 't') {
            if (*fmt == 'l') lmod++;
            else if (*fmt == 'j' || *fmt == 'z' || *fmt == 't') lmod = 2;
            fmt++;
        }
        char conv = *fmt++;

        switch (conv) {
        case 's': {
            const char *str = va_arg(ap, const char *);
            if (!str) str = "(null)";
            size_t len = strlen(str);
            if (prec >= 0 && (size_t)prec < len) len = prec;
            int sp = width > (int)len ? width - (int)len : 0;
            if (!left) while (sp--) { codeos_emit(&out, &room, ' '); written++; }
            for (size_t i = 0; i < len; i++) { codeos_emit(&out, &room, str[i]); written++; }
            if (left) while (sp--) { codeos_emit(&out, &room, ' '); written++; }
            break;
        }
        case 'c': {
            char c = (char)va_arg(ap, int);
            int sp = width > 1 ? width - 1 : 0;
            if (!left) while (sp--) { codeos_emit(&out, &room, ' '); written++; }
            codeos_emit(&out, &room, c); written++;
            if (left) while (sp--) { codeos_emit(&out, &room, ' '); written++; }
            break;
        }
        case 'd':
        case 'i': {
            long v = (lmod >= 2) ? va_arg(ap, long long) : va_arg(ap, long);
            if (lmod == 0) v = (int)v;
            int sign = 0;
            if (v < 0) { sign = 1; v = -v; }
            unsigned long u = (unsigned long)v;
            char buf[64];
            size_t len = 0;
            do { buf[len++] = '0' + (char)(u % 10); u /= 10; } while (u);
            // Pad with zeros for precision
            if (prec >= 0) {
                while ((int)len < prec) buf[len++] = '0';
            }
            int total = (int)len + sign;
            int sp = width > total ? width - total : 0;
            if (!left) while (sp--) { codeos_emit(&out, &room, ' '); written++; }
            if (sign) { codeos_emit(&out, &room, '-'); written++; }
            while (len--) codeos_emit(&out, &room, buf[len]);
            if (left) while (sp--) { codeos_emit(&out, &room, ' '); written++; }
            break;
        }
        case 'u': {
            unsigned long u = (lmod >= 2) ? va_arg(ap, unsigned long long)
                                          : va_arg(ap, unsigned long);
            if (lmod == 0) u = (unsigned int)u;
            char buf[64];
            size_t len = 0;
            do { buf[len++] = '0' + (char)(u % 10); u /= 10; } while (u);
            if (prec >= 0) {
                while ((int)len < prec) buf[len++] = '0';
            }
            int total = (int)len;
            int sp = width > total ? width - total : 0;
            if (!left) while (sp--) { codeos_emit(&out, &room, ' '); written++; }
            while (len--) codeos_emit(&out, &room, buf[len]);
            if (left) while (sp--) { codeos_emit(&out, &room, ' '); written++; }
            break;
        }
        case 'o':
        case 'x':
        case 'X': {
            unsigned long u = (lmod >= 2) ? va_arg(ap, unsigned long long)
                                          : va_arg(ap, unsigned long);
            if (lmod == 0) u = (unsigned int)u;
            unsigned base = (conv == 'o') ? 8 : 16;
            char buf[64];
            size_t len = 0;
            do {
                unsigned d = (unsigned)(u % base);
                buf[len++] = (d < 10) ? '0' + (char)d
                                      : (conv == 'X' ? 'A' : 'a') + (char)(d - 10);
                u /= base;
            } while (u);
            if (prec >= 0) {
                while ((int)len < prec) buf[len++] = '0';
            }
            int total = (int)len;
            int sp = width > total ? width - total : 0;
            if (!left) while (sp--) { codeos_emit(&out, &room, ' '); written++; }
            while (len--) codeos_emit(&out, &room, buf[len]);
            if (left) while (sp--) { codeos_emit(&out, &room, ' '); written++; }
            break;
        }
        case 'p': {
            unsigned long u = (unsigned long)va_arg(ap, void *);
            char buf[64];
            size_t len = 0;
            do { unsigned d = (unsigned)(u % 16);
                 buf[len++] = (d < 10) ? '0' + (char)d : 'a' + (char)(d - 10);
                 u /= 16; } while (u);
            int total = (int)len + 2;
            int sp = width > total ? width - total : 0;
            if (!left) while (sp--) { codeos_emit(&out, &room, ' '); written++; }
            codeos_emit(&out, &room, '0'); codeos_emit(&out, &room, 'x'); written += 2;
            while (len--) codeos_emit(&out, &room, buf[len]);
            written += len;
            if (left) while (sp--) { codeos_emit(&out, &room, ' '); written++; }
            break;
        }
        case '%':
            codeos_emit(&out, &room, '%'); written++;
            break;
        case 'n':
            break;
        default:
            codeos_emit(&out, &room, '%'); codeos_emit(&out, &room, conv); written += 2;
            break;
        }
    }
    codeos_emit(&out, &room, 0);
    return written;
}

extern "C" int printf(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    char buf[1024];
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    kprintf("%s", buf);
    return (int)strlen(buf);
}

extern "C" int __cxa_atexit(void (*)(void *), void *, void *) { return 0; }
extern "C" { int errno = 0; }

/* glibc single-thread flag (libstdc++ ext/atomicity.h reads it) */
extern "C" {
    char __libc_single_threaded = 1;
}

/* make_shared deleter tag compare (never matches → get_deleter fails) */
extern "C" char _ZNSt19_Sp_make_shared_tag5_S_eqERKSt9type_info(void) { return 0; }

/* ── C library stubs ── */
extern "C" void abort(void) { while (1) __builtin_trap(); }
extern "C" int atexit(void (*)(void)) { return 0; }
extern "C" double fabs(double x) { return x < 0 ? -x : x; }

extern "C" int isalnum(int c) {
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}
extern "C" int isspace(int c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\v' || c == '\f' || c == '\r';
}
extern "C" int tolower(int c) {
    return (c >= 'A' && c <= 'Z') ? c + 32 : c;
}

extern "C" int atoi(const char *s) {
    while (isspace((unsigned char)*s)) s++;
    int neg = 0;
    if (*s == '-') { neg = 1; s++; } else if (*s == '+') s++;
    long v = 0;
    while (*s >= '0' && *s <= '9') v = v * 10 + (*s++ - '0');
    return neg ? (int)-v : (int)v;
}

extern "C" long strtol(const char *nptr, char **endptr, int base) {
    while (isspace((unsigned char)*nptr)) nptr++;
    int neg = 0;
    if (*nptr == '-') { neg = 1; nptr++; } else if (*nptr == '+') nptr++;
    if ((base == 0 || base == 16) && nptr[0] == '0' && (nptr[1] == 'x' || nptr[1] == 'X')) {
        base = 16;
        nptr += 2;
    } else if (base == 0 && nptr[0] == '0') {
        base = 8;
        nptr++;
    } else if (base == 0) {
        base = 10;
    }
    const char *start = nptr;
    unsigned long acc = 0;
    int overflow = 0;
    while (1) {
        int c = (unsigned char)*nptr;
        int d;
        if (c >= '0' && c <= '9') d = c - '0';
        else if (c >= 'a' && c <= 'z') d = c - 'a' + 10;
        else if (c >= 'A' && c <= 'Z') d = c - 'A' + 10;
        else break;
        if (d >= base) break;
        unsigned long nacc = acc * (unsigned long)base + (unsigned long)d;
        if (nacc < acc) { overflow = 1; acc = (unsigned long)-1; break; }
        acc = nacc;
        nptr++;
    }
    if (endptr) *endptr = (char *)(nptr == start ? nptr : nptr);
    if (overflow) errno = 34;
    return neg ? -(long)acc : (long)acc;
}

extern "C" double nearbyint(double x) {
    double f = floor(x);
    double diff = x - f;
    if (diff < 0.5) return f;
    if (diff > 0.5) return f + 1.0;
    double h = fmod(f, 2.0);
    return (h == 0.0) ? f : f + 1.0;
}

/* extra string fns the kernel libc lacks */
extern "C" char *strncpy(char *d, const char *s, unsigned long n) {
    unsigned long i;
    for (i = 0; i < n && s[i]; i++) d[i] = s[i];
    for (; i < n; i++) d[i] = 0;
    return d;
}
extern "C" char *strrchr(const char *s, int c) {
    const char *r = 0;
    do { if (*s == (char)c) r = s; } while (*s++);
    return (char *)r;
}

/* fortified libc (glibc -D_FORTIFY_SOURCE) */
extern "C" void *__memcpy_chk(void *d, const void *s, unsigned long n, unsigned long dn) {
    (void)dn; return __builtin_memcpy(d, s, n);
}
extern "C" void *__memmove_chk(void *d, const void *s, unsigned long n, unsigned long dn) {
    (void)dn; return __builtin_memmove(d, s, n);
}
extern "C" void *__memset_chk(void *d, int c, unsigned long n, unsigned long dn) {
    (void)dn; return __builtin_memset(d, c, n);
}
extern "C" int __snprintf_chk(char *, unsigned long, int, unsigned long, const char *, ...) { return 0; }
extern "C" int __vsnprintf_chk(char *, unsigned long, int, unsigned long, const char *, void *) { return 0; }
extern "C" int __fprintf_chk(void *, int, const char *, ...) { return 0; }

/* C++ exception personality / unwind (never actually unwound) */
extern "C" void __gxx_personality_v0(void) {}
extern "C" void _Unwind_Resume(void *) { while (1) __builtin_trap(); }

/* Qt timezone (tzfile engine not built into this sysroot); base ctor is
   only present when Qt6 libs are linked, so reference it weakly */
extern "C" void _ZN16QTimeZonePrivateC2Ev(void *this_) __attribute__((weak));
extern "C" void _ZN18QTzTimeZonePrivateC1Ev(void *this_) {
    if (_ZN16QTimeZonePrivateC2Ev) _ZN16QTimeZonePrivateC2Ev(this_);
}
extern "C" void _ZN18QTzTimeZonePrivateC1ERK10QByteArray(void *this_, void *) {
    if (_ZN16QTimeZonePrivateC2Ev) _ZN16QTimeZonePrivateC2Ev(this_);
}

/* Qt collator (ICU/glibc collation backend absent; identity ordering) */
extern "C" void _ZN16QCollatorPrivate4initEv(void *) {}
extern "C" void _ZN16QCollatorPrivate7cleanupEv(void *) {}
extern "C" unsigned long _ZNK9QCollator7sortKeyERK7QString(void *, void *) { return 0; }

/* Qt system-locale engine; qlocale.cpp.obj is compiled without the
   platform locale engine (qlocale_unix/qlocale_wasm not built), so
   QSystemLocale::{query,fallbackLocale} have no real implementation.

   updateSystemPrivate() calls query() with this calling convention
   (verified in the qlocale.cpp.obj disassembly):
       rcx = hidden QVariant sret, rsi = this, edx = QueryType,
       rdi = QVariant&& in-arg
   and consumes the result only through QVariant::isValid()/dtor, so a
   32-byte zeroed sret (a null-but-valid QVariant) satisfies it.

   fallbackLocale() uses (rdi = hidden QLocale sret, rsi = this). The
   caller either detaches (when refcount != 1, so it never frees our
   static) or steals+deletes (only when refcount == 1). Returning a
   static QLocalePrivate with refcount == INT_MAX forces the detach
   path: it heap-copies the 32 bytes it cares about (qword@0, m_index,
   dword@0x18) and leaves our static untouched. m_index == 0 is the C
   locale, matching QLocalePrivate c_locale(locale_data, 0, ...). */
typedef struct { uint64_t q0; uint32_t ref; uint32_t pad; uint64_t m_index; uint32_t d18; } codeos_locale_private_t;
static codeos_locale_private_t codeos_c_locale = { 0, 0x7fffffff, 0, 0, 0 };

extern "C" void _ZNK13QSystemLocale5queryENS_9QueryTypeEO8QVariant(void *ref, void *self, int type, void *sret) {
    (void)self; (void)type;
    /* updateSystemPrivate() pre-fills both the QVariant sret and the
       in-argument with 0xfefefefefefefefe (the compiled-in QVariant
       constant is pure poison in this stripped build) and destroys both
       afterwards. Zeroing both makes the resulting dtors no-ops. */
    __builtin_memset(ref, 0, 32);
    __builtin_memset(sret, 0, 32);
}
extern "C" void _ZNK13QSystemLocale14fallbackLocaleEv(void *sret, void *self) {
    (void)self;
    *(uint64_t *)sret = (uint64_t)&codeos_c_locale;
}

/* std::chrono::system_clock (kernel clock not wired to C++ chrono yet) */
extern "C" long long _ZNSt6chrono3_V212system_clock3nowEv(void) { return 0; }
extern "C" long long _ZNSt6chrono3_V212steady_clock3nowEv(void) { return 0; }

/* QFSFileEngine methods missing from sysroot (unix engine not built) */
extern "C" unsigned long _ZN13QFSFileEngine7cloneToEP19QAbstractFileEngine(void *, void *) { return 0; }
extern "C" int _ZNK13QFSFileEngine8fileNameEN19QAbstractFileEngine8FileNameE(void *sret, void *, int) {
    for (int i = 0; i < 24; i++) ((char *)sret)[i] = 0;
    return 0;
}
extern "C" int _ZN13QFSFileEngine4linkERK7QString(void *, void *) { return 0; }
extern "C" int _ZN13QFSFileEngine7setSizeEx(void *, long long) { return 0; }
extern "C" int _ZNK13QFSFileEngine14isRelativePathEv(void *) { return 0; }
extern "C" unsigned int _ZNK13QFSFileEngine9fileFlagsE6QFlagsIN19QAbstractFileEngine8FileFlagEE(void *, unsigned int) { return 0; }
extern "C" int _ZN13QFSFileEngine14setPermissionsEj(void *, unsigned int) { return 0; }
extern "C" int _ZNK13QFSFileEngine2idEv(void *sret, void *) {
    for (int i = 0; i < 24; i++) ((char *)sret)[i] = 0;
    return 0;
}
extern "C" unsigned int _ZNK13QFSFileEngine7ownerIdEN19QAbstractFileEngine9FileOwnerE(void *, int) { return 0; }
extern "C" int _ZNK13QFSFileEngine5ownerEN19QAbstractFileEngine9FileOwnerE(void *sret, void *, int) {
    for (int i = 0; i < 24; i++) ((char *)sret)[i] = 0;
    return 0;
}
extern "C" int _ZN13QFSFileEngine11setFileTimeERK9QDateTimeN11QFileDevice8FileTimeE(void *, void *, int) { return 0; }

/* QLockFile + QFileSystemIterator unix-engine methods not in sysroot */
extern "C" int _ZN9QLockFile6unlockEv(void *) { return 0; }
extern "C" int _ZN16QLockFilePrivate11tryLock_sysEv(void *) { return 0; }
extern "C" int _ZN16QLockFilePrivate15removeStaleLockEv(void *) { return 0; }
extern "C" int _ZN16QLockFilePrivate16isProcessRunningExRK7QString(void *, long long, void *) { return 0; }
extern "C" int _ZN16QLockFilePrivate16processNameByPidEx(void *sret, void *, long long) {
    for (int i = 0; i < 24; i++) ((char *)sret)[i] = 0;
    return 0;
}
extern "C" int _ZN16QLockFilePrivate21openNewFileDescriptorERK7QString(void *, void *) { return -1; }
extern "C" void _ZN7QThread5sleepENSt6chrono8durationIlSt5ratioILl1ELl1000000000EEEE(long long) {}
extern "C" void _ZN19QFileSystemIteratorC1ERK16QFileSystemEntry6QFlagsIN11QDirListing12IteratorFlagEE(void *, void *, unsigned int) {}
extern "C" void _ZN19QFileSystemIteratorC1ERK16QFileSystemEntry6QFlagsIN4QDir6FilterEE(void *, void *, unsigned int) {}
extern "C" void _ZN19QFileSystemIteratorD1Ev(void *) {}
extern "C" void _ZN19QFileSystemIteratorD2Ev(void *) {}
extern "C" int _ZN19QFileSystemIterator7advanceER16QFileSystemEntryR19QFileSystemMetaData(void *, void *, void *) { return 0; }

/* QFileSystemEngine + QFSFileEnginePrivate unix-native methods */
extern "C" int _ZN17QFileSystemEngine8copyFileERK16QFileSystemEntryS2_R12QSystemError(void *, void *, void *, void *) { return 0; }
extern "C" int _ZN17QFileSystemEngine10removeFileERK16QFileSystemEntryR12QSystemError(void *, void *, void *) { return 0; }
extern "C" int _ZN17QFileSystemEngine10renameFileERK16QFileSystemEntryS2_R12QSystemError(void *, void *, void *, void *) { return 0; }
extern "C" int _ZN17QFileSystemEngine19renameOverwriteFileERK16QFileSystemEntryS2_R12QSystemError(void *, void *, void *, void *) { return 0; }
extern "C" int _ZN17QFileSystemEngine5mkdirERK16QFileSystemEntrySt8optionalI6QFlagsIN11QFileDevice10PermissionEEE(void *, void *, void *) { return 0; }
extern "C" int _ZN17QFileSystemEngine5rmdirERK16QFileSystemEntry(void *, void *) { return 0; }
extern "C" int _ZN17QFileSystemEngine6mkpathERK16QFileSystemEntrySt8optionalI6QFlagsIN11QFileDevice10PermissionEEE(void *, void *, void *) { return 0; }
extern "C" int _ZN17QFileSystemEngine6rmpathERK16QFileSystemEntry(void *, void *) { return 0; }
extern "C" int _ZN17QFileSystemEngine14setCurrentPathERK16QFileSystemEntry(void *, void *) { return 0; }
extern "C" int _ZN20QFSFileEnginePrivate10nativeOpenE6QFlagsIN13QIODeviceBase12OpenModeFlagEESt8optionalIS0_IN11QFileDevice10PermissionEEE(void *, unsigned int, void *) { return 0; }
extern "C" int _ZN20QFSFileEnginePrivate10nativeReadEPcx(void *, char *, long long) { return -1; }
extern "C" int _ZN20QFSFileEnginePrivate10nativeSeekEx(void *, long long) { return 0; }
extern "C" int _ZN20QFSFileEnginePrivate11nativeCloseEv(void *) { return 0; }
extern "C" int _ZN20QFSFileEnginePrivate11nativeFlushEv(void *) { return 0; }
extern "C" long _ZN20QFSFileEnginePrivate11nativeWriteEPKcx(void *, const char *, long long) { return -1; }
extern "C" long _ZN20QFSFileEnginePrivate14nativeReadLineEPcx(void *, char *, long long) { return -1; }
extern "C" int _ZN20QFSFileEnginePrivate16nativeSyncToDiskEv(void *) { return 0; }
extern "C" long _ZNK20QFSFileEnginePrivate10nativeSizeEv(void *) { return 0; }
extern "C" long _ZNK20QFSFileEnginePrivate12nativeHandleEv(void *) { return -1; }
extern "C" int _ZNK20QFSFileEnginePrivate18nativeIsSequentialEv(void *) { return 0; }
extern "C" int _ZNK20QFSFileEnginePrivate6doStatE6QFlagsIN19QFileSystemMetaData12MetaDataFlagEE(void *, unsigned int) { return 0; }
extern "C" long _ZNK20QFSFileEnginePrivate9nativePosEv(void *) { return 0; }
extern "C" unsigned char *_ZN20QFSFileEnginePrivate3mapExx6QFlagsIN11QFileDevice13MemoryMapFlagEE(void *, long long, long long, unsigned int) { return 0; }
extern "C" int _ZN20QFSFileEnginePrivate5unmapEPh(void *, unsigned char *) { return 0; }

/* QFileSystemEngine unix helpers (QString=24B, QByteArray=24B, QFileSystemEntry=56B via sret) */
extern "C" int _ZN17QFileSystemEngine11currentPathEv(void *sret) {
    /* returns QFileSystemEntry (56B): m_filePath QString @0 + m_nativeFilePath
       QByteArray @24 + qint16 fields @48. Zeroing only 24B leaves the
       QByteArray d-pointer as 0xfefefefefefefefe poison from the Qt
       sysroot's -ftrivial-auto-var-init=pattern build, and ~QFileSystemEntry
       derefs it -> GP. */
    for (int i = 0; i < 56; i++) ((char *)sret)[i] = 0;
    return 0;
}
extern "C" int _ZN17QFileSystemEngine8homePathEv(void *sret) {
    for (int i = 0; i < 24; i++) ((char *)sret)[i] = 0;
    return 0;
}
extern "C" int _ZN17QFileSystemEngine8rootPathEv(void *sret) {
    for (int i = 0; i < 24; i++) ((char *)sret)[i] = 0;
    return 0;
}
extern "C" int _ZN17QFileSystemEngine8tempPathEv(void *sret) {
    for (int i = 0; i < 24; i++) ((char *)sret)[i] = 0;
    return 0;
}
extern "C" int _ZN17QFileSystemEngine16resolveGroupNameEj(void *sret, unsigned int) {
    for (int i = 0; i < 24; i++) ((char *)sret)[i] = 0;
    return 0;
}
extern "C" int _ZN17QFileSystemEngine2idERK16QFileSystemEntry(void *sret, void *) {
    for (int i = 0; i < 24; i++) ((char *)sret)[i] = 0;
    return 0;
}
extern "C" int _ZN17QFileSystemEngine12absoluteNameERK16QFileSystemEntry(void *sret, void *) {
    for (int i = 0; i < 56; i++) ((char *)sret)[i] = 0;
    return 0;
}
extern "C" int _ZN17QFileSystemEngine13canonicalNameERK16QFileSystemEntryR19QFileSystemMetaData(void *sret, void *, void *) {
    for (int i = 0; i < 56; i++) ((char *)sret)[i] = 0;
    return 0;
}
extern "C" int _ZN17QFileSystemEngine13getLinkTargetERK16QFileSystemEntryR19QFileSystemMetaData(void *sret, void *, void *) {
    for (int i = 0; i < 56; i++) ((char *)sret)[i] = 0;
    return 0;
}
extern "C" int _ZN17QFileSystemEngine14getRawLinkPathERK16QFileSystemEntryR19QFileSystemMetaData(void *sret, void *, void *) {
    for (int i = 0; i < 56; i++) ((char *)sret)[i] = 0;
    return 0;
}
extern "C" int _ZN17QFileSystemEngine15isCaseSensitiveERK16QFileSystemEntryR19QFileSystemMetaData(void *, void *, void *) { return 1; }
extern "C" int _ZN17QFileSystemEngine15moveFileToTrashERK16QFileSystemEntryRS0_R12QSystemError(void *, void *, void *, void *) { return 0; }
extern "C" int _ZN17QFileSystemEngine23supportsMoveFileToTrashEv(void *) { return 0; }

/* QFSFileEngine::drives -> empty QList<QFileInfo> (24 bytes sret) */
extern "C" void _ZN13QFSFileEngine6drivesEv(void *sret) {
    for (int i = 0; i < 24; i++) ((char *)sret)[i] = 0;
}

/* pthread / environment / time libc (single-threaded kernel) */
extern "C" { void *stderr = 0; char *tzname[2] = { 0, 0 }; }
extern "C" unsigned long pthread_self(void) { return 1; }
extern "C" int pthread_getname_np(unsigned long, char *name, unsigned long len) {
    if (len) name[0] = 0;
    return 0;
}
extern "C" long syscall(long, ...) { return -1; }
extern "C" int setenv(const char *, const char *, int) { return 0; }
extern "C" int unsetenv(const char *) { return 0; }
extern "C" void tzset(void) {}
extern "C" long mktime(struct tm_glibc *t) { (void)t; return 0; }
extern "C" struct tm_glibc *localtime(const long *t) { (void)t; return 0; }
extern "C" long pathconf(const char *, int) { return -1; }
extern "C" long sysconf(int) { return -1; }
extern "C" int getpwnam_r(const char *, void *, char *, unsigned long, void **) { return -1; }
extern "C" void exit(int) { while (1) __builtin_trap(); }
extern "C" int __isoc23_sscanf(const char *, const char *, ...) { return 0; }

/* qt_readlink returns QByteArray (24 bytes) via sret */
extern "C" void _Z11qt_readlinkPKc(void *sret, const char *) {
    for (int i = 0; i < 24; i++) ((char *)sret)[i] = 0;
}

/* QCollator::compare -> identity ordering */
extern "C" int _ZNK9QCollator7compareE11QStringViewS0_(void *, void *, void *) { return 0; }

/* Unix event dispatcher (kernel has no poll/select loop yet) */
extern "C" unsigned long _ZN14QThreadPrivate21createEventDispatcherEP11QThreadData(void *) { return 0; }

/* ── libm (x87-based, FPU is fninit'd by kernel) ── */
static double ldexp_impl(double m, long e) {
    union { double d; long long i; } u;
    u.d = m;
    long long exp = (u.i >> 52) & 0x7ff;
    exp += e;
    u.i = (u.i & 0x800fffffffffffffLL) | (exp << 52);
    return u.d;
}
extern "C" double floor(double x) {
    long long t = (long long)x;
    if ((double)t > x) return (double)(t - 1);
    return (double)t;
}
extern "C" double ceil(double x) {
    long long t = (long long)x;
    if ((double)t < x) return (double)(t + 1);
    return (double)t;
}
extern "C" double trunc(double x) { return (double)(long long)x; }
extern "C" double round(double x) { return x >= 0 ? floor(x + 0.5) : ceil(x - 0.5); }
extern "C" double sqrt(double x) {
    double r;
    __asm__ volatile("fsqrt" : "=t"(r) : "0"(x));
    return r;
}
extern "C" double fmod(double x, double y) {
    if (y == 0) return x;
    double q = (long long)(x / y);
    return x - q * y;
}
extern "C" double sin(double x) {
    double r;
    __asm__ volatile("fsin" : "=t"(r) : "0"(x));
    return r;
}
extern "C" double cos(double x) {
    double r;
    __asm__ volatile("fcos" : "=t"(r) : "0"(x));
    return r;
}
extern "C" double tan(double x) {
    double r;
    __asm__ volatile("fptan; fstp %%st(0)" : "=t"(r) : "0"(x) : "st(1)");
    return r;
}
extern "C" double atan2(double y, double x) {
    double r;
    __asm__ volatile("fpatan" : "=t"(r) : "0"(x), "u"(y));
    return r;
}
extern "C" double atan(double x) { return atan2(x, 1.0); }
extern "C" double asin(double x) {
    double s = sqrt(1.0 - x * x);
    return atan2(x, s);
}
extern "C" double acos(double x) { return 1.57079632679489661923 - asin(x); }
extern "C" double log2(double x) {
    double r;
    __asm__ volatile("fyl2x" : "=t"(r) : "0"(x), "u"(1.0));
    return r;
}
extern "C" double log(double x) { return log2(x) * 0.693147180559945309417232121458176568; }
extern "C" double log10(double x) { return log2(x) * 0.301029995663981195213738894724493027; }
extern "C" double exp(double x) {
    double f = x * 1.4426950408889634074;
    double i = floor(f);
    double frac = f - i;
    double p2;
    __asm__ volatile("f2xm1" : "=t"(p2) : "0"(frac));
    return (p2 + 1.0) * ldexp_impl(1.0, (long)i);
}
extern "C" double pow(double x, double y) {
    if (x <= 0.0) return 1.0;
    double f = y * log2(x);
    double i = floor(f);
    double frac = f - i;
    double p2;
    __asm__ volatile("f2xm1" : "=t"(p2) : "0"(frac));
    return (p2 + 1.0) * ldexp_impl(1.0, (long)i);
}
extern "C" double fmin(double a, double b) { return a < b ? a : b; }
extern "C" double fmax(double a, double b) { return a > b ? a : b; }
extern "C" double hypot(double x, double y) { return sqrt(x * x + y * y); }
extern "C" double modf(double x, double *i) {
    *i = trunc(x);
    return x - *i;
}
extern "C" float sqrtf(float x) { return (float)sqrt((double)x); }
extern "C" float sinf(float x) { return (float)sin((double)x); }
extern "C" float cosf(float x) { return (float)cos((double)x); }
extern "C" float tanf(float x) { return (float)tan((double)x); }
extern "C" float asinf(float x) { return (float)asin((double)x); }
extern "C" float acosf(float x) { return (float)acos((double)x); }
extern "C" float atanf(float x) { return (float)atan((double)x); }
extern "C" float atan2f(float y, float x) { return (float)atan2((double)y, (double)x); }
extern "C" float expf(float x) { return (float)exp((double)x); }
extern "C" float logf(float x) { return (float)log((double)x); }
extern "C" float log10f(float x) { return (float)log10((double)x); }
extern "C" float log2f(float x) { return (float)log2((double)x); }
extern "C" float powf(float x, float y) { return (float)pow((double)x, (double)y); }
extern "C" float fabsf(float x) { return x < 0 ? -x : x; }
extern "C" float floorf(float x) { return (float)floor((double)x); }
extern "C" float ceilf(float x) { return (float)ceil((double)x); }
extern "C" float truncf(float x) { return (float)trunc((double)x); }
extern "C" float roundf(float x) { return (float)round((double)x); }
extern "C" float fmodf(float x, float y) { return (float)fmod((double)x, (double)y); }
extern "C" float fminf(float a, float b) { return a < b ? a : b; }
extern "C" float fmaxf(float a, float b) { return a > b ? a : b; }
extern "C" float hypotf(float x, float y) { return (float)hypot((double)x, (double)y); }
extern "C" float modff(float x, float *i) { return (float)modf((double)x, (double *)i); }
extern "C" float copysignf(float x, float y) {
    union { float f; unsigned u; } a = { x }, b = { y };
    a.u = (a.u & 0x7fffffffu) | (b.u & 0x80000000u);
    return a.f;
}
extern "C" double copysign(double x, double y) {
    union { double f; unsigned long long u; } a = { x }, b = { y };
    a.u = (a.u & 0x7fffffffffffffffull) | (b.u & 0x8000000000000000ull);
    return a.f;
}

/* ── POSIX syscall stubs ── */
extern "C" int *__errno_location(void) { return &errno; }
extern "C" unsigned long wcslen(const unsigned int *s) {
    unsigned long n = 0;
    while (s[n]) n++;
    return n;
}
extern "C" int strcoll(const char *a, const char *b) {
    while (*a && *a == *b) { a++; b++; }
    return (unsigned char)*a - (unsigned char)*b;
}
extern "C" char *setlocale(int, const char *) { static char c[] = "C"; return c; }
extern "C" char *nl_langinfo(unsigned long) { static char c[] = "UTF-8"; return c; }
extern "C" char *strerror(int) {
    static char s[] = "Unknown error";
    return s;
}
extern "C" char *strncat(char *d, const char *s, unsigned long n) {
    char *r = d;
    while (*d) d++;
    unsigned long i;
    for (i = 0; i < n && s[i]; i++) d[i] = s[i];
    d[i] = 0;
    return r;
}
extern "C" void *memrchr(const void *s, int c, unsigned long n) {
    const unsigned char *p = (const unsigned char *)s + n;
    while (n--) if (*--p == (unsigned char)c) return (void *)p;
    return 0;
}
extern "C" void *memmem(const void *hay, unsigned long hl, const void *nee, unsigned long nl) {
    const unsigned char *h = (const unsigned char *)hay;
    const unsigned char *n = (const unsigned char *)nee;
    if (nl > hl) return 0;
    if (!nl) return (void *)h;
    for (unsigned long i = 0; i + nl <= hl; i++) {
        unsigned long j = 0;
        while (j < nl && h[i + j] == n[j]) j++;
        if (j == nl) return (void *)(h + i);
    }
    return 0;
}
extern "C" long time(long *t) {
    if (t) *t = 0;
    return 0;
}
struct tm_glibc {
    int tm_sec, tm_min, tm_hour, tm_mday, tm_mon, tm_year;
    int tm_wday, tm_yday, tm_isdst;
    long tm_gmtoff;
    const char *tm_zone;
};
extern "C" struct tm_glibc *gmtime_r(const long *t, struct tm_glibc *r) {
    (void)t;
    for (int i = 0; i < 9; i++) ((int *)r)[i] = 0;
    r->tm_gmtoff = 0;
    r->tm_zone = 0;
    return r;
}
extern "C" int open(const char *, int, ...) { return -1; }
extern "C" void *fopen(const char *, const char *) { return 0; }
extern "C" int fclose(void *) { return -1; }
extern "C" char *fgets(char *, int, void *) { return 0; }
extern "C" int fseeko(void *, long, int) { return 0; }
extern "C" long ftello(void *) { return 0; }
extern "C" int feof(void *) { return 1; }
extern "C" int fflush(void *) { return 0; }
extern "C" unsigned long fread(void *, unsigned long, unsigned long, void *) { return 0; }
extern "C" unsigned long fwrite(const void *, unsigned long, unsigned long, void *) { return 0; }
extern "C" long write(int, const void *, unsigned long) { return -1; }
extern "C" void *mremap(void *, unsigned long, unsigned long, int, ...) { return (void *)-1; }
extern "C" int fstat(int, void *) { return -1; }
extern "C" long read(int, void *, unsigned long) { return -1; }
extern "C" int close(int) { return -1; }
extern "C" int uname(void *p) {
    /* fake utsname with plausible values */
    struct { char sys[65]; char node[65]; char rel[65]; char ver[65]; char mach[65]; } *u = (typeof(u))p;
    if (!u) return -1;
    __builtin_memset(u, 0, sizeof(*u));
    __builtin_memcpy(u->sys, "CodeOS", 6);
    __builtin_memcpy(u->node, "kernel", 6);
    __builtin_memcpy(u->rel, "1.0.0", 5);
    __builtin_memcpy(u->ver, "1", 1);
    __builtin_memcpy(u->mach, "x86_64", 6);
    return 0;
}
extern "C" void __cxa_guard_abort(long long *g) { (void)g; }
extern "C" int stat(const char *, void *buf) { (void)buf; return -1; }
extern "C" int lstat(const char *, void *buf) { (void)buf; return -1; }
extern "C" long lseek(int, long, int) { return -1; }
extern "C" void *mmap(void *, unsigned long, int, int, int, long) { return (void*)-1; }
extern "C" int munmap(void *, unsigned long) { return -1; }
extern "C" int mprotect(void *, unsigned long, int) { return -1; }
extern "C" int getpid(void) { return 1; }
extern "C" int getuid(void) { return 0; }
extern "C" int geteuid(void) { return 0; }
extern "C" int getpagesize(void) { return 4096; }
extern "C" int getrlimit(int, void *) { return 0; }
extern "C" int mkdir(const char *, int) { return -1; }
/* ── Time: must report real advancing time or Qt's QDateTime freezes.
   (Empty stubs here used to shadow posixstubs' working versions.) */
extern "C" uint64_t timer_get_milliseconds(void);
struct codeos_timeval { long tv_sec; long tv_usec; };
struct codeos_timespec { long tv_sec; long tv_nsec; };
extern "C" int gettimeofday(struct codeos_timeval *tv, void *) {
    if (!tv) return -1;
    uint64_t ms = timer_get_milliseconds();
    tv->tv_sec = (long)(ms / 1000);
    tv->tv_usec = (long)((ms % 1000) * 1000);
    return 0;
}
extern "C" int clock_gettime(int, struct codeos_timespec *tp) {
    if (!tp) return -1;
    uint64_t ms = timer_get_milliseconds();
    tp->tv_sec = (long)(ms / 1000);
    tp->tv_nsec = (long)((ms % 1000) * 1000000);
    return 0;
}
extern "C" int nanosleep(const void *, void *) { return 0; }
extern "C" char *getenv(const char *) { return 0; }
extern "C" int isatty(int) { return 0; }
extern "C" int tcgetattr(int, void *) { return -1; }
extern "C" int tcsetattr(int, int, const void *) { return -1; }

/* ── C++ ABI RTTI vtables ── */
extern "C" void __cxxabiv1_typeinfo_impl(void) { while (1) __builtin_trap(); }

#define CXXABI_VTABLE(name) \
    asm(".globl " name "\n\t" \
        ".type " name ", @object\n\t" \
        ".data\n\t" \
        name ":\n\t" \
        ".quad 0\n\t.quad 0\n\t" \
        ".quad __cxxabiv1_typeinfo_impl\n\t" \
        ".quad __cxxabiv1_typeinfo_impl\n\t" \
        ".quad __cxxabiv1_typeinfo_impl\n\t" \
        ".quad __cxxabiv1_typeinfo_impl\n\t" \
        ".quad __cxxabiv1_typeinfo_impl\n\t" \
        ".size " name ", . - " name);

CXXABI_VTABLE("_ZTVN10__cxxabiv117__class_type_infoE");
CXXABI_VTABLE("_ZTVN10__cxxabiv120__si_class_type_infoE");
CXXABI_VTABLE("_ZTVN10__cxxabiv121__vmi_class_type_infoE");

/* ── RTTI typeinfo objects ──
   Qt static libs are built with -fno-rtti, so Qt classes emit no typeinfo;
   the panels are built with -frtti and reference the typeinfo of their
   Qt bases.  Provide them here.  __si_class_type_info layout:
   { vptr; const char* name; const __class_type_info* __base_type; } */
#define RTTI_CLASS(name, tname) \
    asm(".globl _ZTI" name "\n\t" \
        ".type _ZTI" name ", @object\n\t" \
        ".data\n\t" \
        "_ZTI" name ":\n\t" \
        ".quad _ZTVN10__cxxabiv117__class_type_infoE + 16\n\t" \
        ".quad _ZTS" name "\n\t" \
        ".size _ZTI" name ", . - _ZTI" name "\n\t" \
        ".section .rodata\n\t" \
        ".globl _ZTS" name "\n\t" \
        ".type _ZTS" name ", @object\n\t" \
        "_ZTS" name ":\n\t" \
        ".string \"" tname "\"\n\t" \
        ".size _ZTS" name ", . - _ZTS" name "\n\t" \
        ".text\n")

#define RTTI_SI(name, base, tname) \
    asm(".globl _ZTI" name "\n\t" \
        ".type _ZTI" name ", @object\n\t" \
        ".data\n\t" \
        "_ZTI" name ":\n\t" \
        ".quad _ZTVN10__cxxabiv120__si_class_type_infoE + 16\n\t" \
        ".quad _ZTS" name "\n\t" \
        ".quad _ZTI" base "\n\t" \
        ".size _ZTI" name ", . - _ZTI" name "\n\t" \
        ".section .rodata\n\t" \
        ".globl _ZTS" name "\n\t" \
        ".type _ZTS" name ", @object\n\t" \
        "_ZTS" name ":\n\t" \
        ".string \"" tname "\"\n\t" \
        ".size _ZTS" name ", . - _ZTS" name "\n\t" \
        ".text\n")

RTTI_CLASS("7QObject", "7QObject");
RTTI_SI("7QWidget", "7QObject", "7QWidget");
RTTI_SI("15QPlatformScreen", "7QObject", "15QPlatformScreen");
RTTI_SI("15QPlatformWindow", "7QObject", "15QPlatformWindow");
RTTI_SI("21QPlatformBackingStore", "7QObject", "21QPlatformBackingStore");
RTTI_SI("18QPlatformClipboard", "7QObject", "18QPlatformClipboard");
RTTI_SI("20QPlatformIntegration", "7QObject", "20QPlatformIntegration");
RTTI_SI("24QAbstractEventDispatcher", "7QObject", "24QAbstractEventDispatcher");

RTTI_CLASS("St8ios_base", "St8ios_base");
RTTI_CLASS("NSt6locale5facetE", "NSt6locale5facetE");

#undef RTTI_CLASS
#undef RTTI_SI

/* ── std::bad_alloc / terminate / list internals (Qt pulls in stl headers) ── */
namespace std {
struct bad_alloc : exception {
    bad_alloc() noexcept {}
    virtual ~bad_alloc() noexcept;
};
bad_alloc::~bad_alloc() noexcept {}
void terminate() { while (1) __builtin_trap(); }
void __throw_bad_alloc() { while (1) __builtin_trap(); }
namespace __detail {
struct _List_node_base {
    _List_node_base *_M_next;
    _List_node_base *_M_prev;
    void _M_hook(_List_node_base *const pos);
    void _M_unhook();
    void _M_transfer(_List_node_base *const first, _List_node_base *const last);
    void _M_reverse();
    static void swap(_List_node_base &x, _List_node_base &y);
};
void _List_node_base::_M_hook(_List_node_base *const pos) {
    this->_M_next = pos;
    this->_M_prev = pos->_M_prev;
    pos->_M_prev->_M_next = this;
    pos->_M_prev = this;
}
void _List_node_base::_M_unhook() {
    _List_node_base *n = this->_M_next;
    _List_node_base *p = this->_M_prev;
    n->_M_prev = p;
    p->_M_next = n;
}
void _List_node_base::_M_transfer(_List_node_base *const first, _List_node_base *const last) {
    if (this == last) return;
    _List_node_base *const n = this->_M_next;
    _List_node_base *const p = first->_M_prev;
    n->_M_prev = p;
    p->_M_next = n;
    this->_M_next = last;
    last->_M_prev = this;
    _List_node_base *const t = last->_M_prev;
    _List_node_base *const u = first;
    u->_M_prev = this;
    this->_M_next = u;
    t->_M_next = first;
    first->_M_prev = t;
}
void _List_node_base::_M_reverse() {
    _List_node_base *tmp = this;
    do {
        _List_node_base *n = tmp->_M_next;
        tmp->_M_next = tmp->_M_prev;
        tmp->_M_prev = n;
        tmp = tmp->_M_prev;
    } while (tmp != this);
}
void _List_node_base::swap(_List_node_base &x, _List_node_base &y) {
    if (x._M_next != &x) {
        if (y._M_next != &y) {
            _List_node_base *n = x._M_next;
            x._M_next = y._M_next;
            y._M_next = n;
            n = x._M_prev;
            x._M_prev = y._M_prev;
            y._M_prev = n;
            x._M_next->_M_prev = x._M_prev->_M_next = &x;
            y._M_next->_M_prev = y._M_prev->_M_next = &y;
        } else {
            y._M_next = x._M_next;
            y._M_prev = x._M_prev;
            y._M_next->_M_prev = y._M_prev->_M_next = &y;
            x._M_next = x._M_prev = &x;
        }
    } else if (y._M_next != &y) {
        x._M_next = y._M_next;
        x._M_prev = y._M_prev;
        x._M_next->_M_prev = x._M_prev->_M_next = &x;
        y._M_next = y._M_prev = &y;
    }
}
}
} /* std */

/* iostream support (QWidget::flagsForDumping writes via ostream) */
namespace std {
struct ios_base { virtual ~ios_base(); };
ios_base::~ios_base() {}
}
extern "C" void *_ZSt16__ostream_insertIcSt11char_traitsIcEERSt13basic_ostreamIT_T0_ES6_PKS3_l(void *os, const char *, long) { return os; }
extern "C" void *_ZNSo3putEc(void *os, char) { return os; }

/* panels run in-kernel: sleep via scheduler */
extern "C" void sys_sleep(int ms) { sched_sleep_ms((uint64_t)ms); }
extern "C" void _Z9sys_sleepi(int ms) { sched_sleep_ms((uint64_t)ms); }

/* setjmp/longjmp (qgrayraster error recovery)
 * Layout must match sysroot <setjmp.h>: rbx,rbp,r12,r13,r14,r15,rsp,rip,mxcsr,fpsw.
 * Must save/restore ALL callee-saved registers - __builtin_setjmp does not,
 * which corrupted rasterizer state after longjmp (UD/PF crashes). */
extern "C" {
int _setjmp(void *buf);
int setjmp(void *buf);
void longjmp(void *buf, int val);
void __longjmp_chk(void *buf, int val);
}

asm(".text\n"
    ".globl _setjmp\n"
    ".type _setjmp,@function\n"
    "_setjmp:\n"
    "   movq %rbx, 0(%rdi)\n"
    "   movq %rbp, 8(%rdi)\n"
    "   movq %r12, 16(%rdi)\n"
    "   movq %r13, 24(%rdi)\n"
    "   movq %r14, 32(%rdi)\n"
    "   movq %r15, 40(%rdi)\n"
    "   leaq 8(%rsp), %rax\n"
    "   movq %rax, 48(%rdi)\n"
    "   movq (%rsp), %rax\n"
    "   movq %rax, 56(%rdi)\n"
    "   stmxcsr 64(%rdi)\n"
    "   fnstcw 72(%rdi)\n"
    "   xorl %eax, %eax\n"
    "   ret\n"
    ".size _setjmp, . - _setjmp\n"
    ".globl setjmp\n"
    "setjmp = _setjmp\n");

asm(".text\n"
    ".globl longjmp\n"
    ".type longjmp,@function\n"
    "longjmp:\n"
    "   movq 0(%rdi), %rbx\n"
    "   movq 8(%rdi), %rbp\n"
    "   movq 16(%rdi), %r12\n"
    "   movq 24(%rdi), %r13\n"
    "   movq 32(%rdi), %r14\n"
    "   movq 40(%rdi), %r15\n"
    "   ldmxcsr 64(%rdi)\n"
    "   fldcw 72(%rdi)\n"
    "   movl %esi, %eax\n"
    "   testl %eax, %eax\n"
    "   jnz 1f\n"
    "   movl $1, %eax\n"
    "1:\n"
    "   movq 48(%rdi), %rsp\n"
    "   jmp *56(%rdi)\n"
    ".size longjmp, . - longjmp\n"
    ".globl __longjmp_chk\n"
    "__longjmp_chk = longjmp\n");

/* remaining libc */
extern "C" unsigned long strcspn(const char *s, const char *reject) {
    unsigned long n = 0;
    while (s[n]) {
        const char *r = reject;
        while (*r) if (*r++ == s[n]) return n;
        n++;
    }
    return n;
}
extern "C" void qsort(void *base, unsigned long nmemb, unsigned long size,
                      int (*compar)(const void *, const void *)) {
    char *b = (char *)base;
    for (unsigned long i = 1; i < nmemb; i++)
        for (unsigned long j = i; j > 0; j--) {
            char *a = b + (j - 1) * size, *c = b + j * size;
            if (compar(a, c) <= 0) break;
            for (unsigned long k = 0; k < size; k++) {
                char t = a[k]; a[k] = c[k]; c[k] = t;
            }
        }
}
extern "C" int fputs(const char *, void *) { return 0; }

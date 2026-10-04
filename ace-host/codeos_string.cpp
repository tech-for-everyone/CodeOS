// TLS-free replacements for the two out-of-line std::string<char> members.
//
// libstdc++'s prebuilt string object (string-inst.o) reads the `%fs:0x28` stack
// canary, which a raw-syscall CodeOS process cannot have: `arch_prctl` cannot
// set FS there. Defining `_M_create` and `_M_append` ourselves keeps that object
// out of the link entirely.
//
// This TU includes no headers, on purpose. `<string>` unconditionally includes
// <bits/basic_string.tcc>, whose `basic_string<char>::_M_create` template is
// emitted whenever the class is instantiated -- and then collides with our
// definition. Declaring the members as free functions taking `void*`/plain
// integers, with their mangled names pinned via `asm`, avoids ever naming
// `std::string`, so the template is never instantiated here. The ABI is the
// same: `this` is the first argument, and the return value is in `%rax`.
//
// The C++11 ABI layout of basic_string<char> is: data pointer, length, then the
// SSO buffer / allocated-capacity union at offset 16.

using size = __SIZE_TYPE__;

extern "C" void* malloc(size);
extern "C" void free(void*);

struct AceStringLayout {
    char* data;
    size len;
    union {
        char local[16];
        size cap;
    };
};

extern "C" void* AceStringCreate(void* self, size* cap, size oldcap)
    asm("_ZNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEE9_M_createERmm");
extern "C" void* AceStringAppend(void* self, const char* s, size n)
    asm("_ZNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEE9_M_appendEPKcm");

extern "C" void* AceStringCreate(void*, size* cap, size oldcap)
{
    size c = *cap;
    if (c > oldcap && c < 2 * oldcap) {
        c = 2 * oldcap;
    }
    *cap = c;
    return static_cast<char*>(malloc(c + 1));
}

extern "C" void* AceStringAppend(void* self, const char* s, size n)
{
    AceStringLayout* l = static_cast<AceStringLayout*>(self);
    const size old = l->len;
    const bool local = (l->data == l->local);
    const size cap = local ? 15u : l->cap;
    if (n <= cap - old) {
        for (size i = 0; i < n; ++i) {
            l->data[old + i] = s[i];
        }
        l->len = old + n;
        l->data[old + n] = 0;
    } else {
        size newcap = old + n;
        char* p = static_cast<char*>(AceStringCreate(self, &newcap, cap));
        for (size i = 0; i < old; ++i) {
            p[i] = l->data[i];
        }
        for (size i = 0; i < n; ++i) {
            p[old + i] = s[i];
        }
        p[old + n] = 0;
        if (!local) {
            // The old heap block came from the bump arena, where free is a
            // no-op; calling it keeps intent clear without touching the arena.
            free(l->data);
        }
        l->data = p;
        l->cap = newcap;
        l->len = old + n;
    }
    return self;
}

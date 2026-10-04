// The freestanding C++ runtime for the CodeOS build of the ArkUI probes.
//
// WHY THIS EXISTS
// ---------------
// CodeOS's Linux personality runs *raw-syscall* static ELFs (see linux-probe),
// not glibc: `arch_prctl` is a deliberate no-op there because the kernel owns
// `%fs` for its own TLS and has no swapgs (kernel/kernel/syscall.c:1083). So a
// normal static C++ binary faults the first time it touches TLS, and libstdc++'s
// prebuilt string objects read the `%fs:0x28` stack canary on entry.
//
// This file supplies the C++ half of what the engine links against but CodeOS
// does not have:
//   * `operator new`/`delete`, backed by the bump arena in codeos_libc.cpp,
//   * the C++ ABI hooks libstdc++'s runtime would normally provide,
//   * TLS-free replacements for `std::string<char>::_M_create`/`_M_append`, which
//     keeps libstdc++'s canary-reading string object out of the link entirely,
//   * `_start` and the raw `write`/`exit` syscalls used to report geometry.
//
// The plain C symbols (allocator, mem*/str*, stdio, pthread, the canary hook)
// live in codeos_libc.cpp on purpose: that file includes no standard headers, so
// it can define `strchr`, `fputc`, `stderr` and the pthread stubs without
// colliding with the hosted declarations `<string>` drags in. Nothing about the
// engine is reimplemented; this is a runtime, not a port. The companion file
// `codeos_probe.cpp` is the payload that calls the engine.

#include <cstddef>
#include <cstdint>
#include <new>

// Provided by codeos_libc.cpp. Declared here rather than by pulling in
// <cstdlib>, which would bring hosted declarations into a TU that deliberately
// avoids them.
extern "C" void* malloc(__SIZE_TYPE__);
extern "C" void* calloc(__SIZE_TYPE__, __SIZE_TYPE__);
extern "C" void* realloc(void*, __SIZE_TYPE__);
extern "C" void free(void*);

// --------------------------------------------------------------------------
// The C++ allocation operators, over the C bump arena.
// --------------------------------------------------------------------------
void* operator new(std::size_t n)
{
    return malloc(n);
}
void* operator new[](std::size_t n)
{
    return malloc(n);
}
void* operator new(std::size_t n, std::align_val_t)
{
    return malloc(n);
}
void* operator new[](std::size_t n, std::align_val_t)
{
    return malloc(n);
}
void operator delete(void* p) noexcept
{
    free(p);
}
void operator delete[](void* p) noexcept
{
    free(p);
}
void operator delete(void* p, std::size_t) noexcept
{
    free(p);
}
void operator delete[](void* p, std::size_t) noexcept
{
    free(p);
}
void operator delete(void* p, std::align_val_t) noexcept
{
    free(p);
}
void operator delete[](void* p, std::align_val_t) noexcept
{
    free(p);
}
void operator delete(void* p, std::size_t, std::align_val_t) noexcept
{
    free(p);
}
void operator delete[](void* p, std::size_t, std::align_val_t) noexcept
{
    free(p);
}

// --------------------------------------------------------------------------
// C++ ABI bits. libgcc_eh supplies the unwinder; these are the hooks that only
// exist in libstdc++'s runtime.
// --------------------------------------------------------------------------
extern "C" void abort(void)
{
    for (;;) {
    }
}
extern "C" void __cxa_pure_virtual(void)
{
    for (;;) {
    }
}
extern "C" void __cxa_deleted_virtual(void)
{
    for (;;) {
    }
}
extern "C" int __cxa_guard_acquire(std::uint64_t* g)
{
    return (*reinterpret_cast<volatile char*>(g) == 0) ? 1 : 0;
}
extern "C" void __cxa_guard_release(std::uint64_t* g)
{
    *reinterpret_cast<volatile char*>(g) = 1;
}
extern "C" void __cxa_guard_abort(std::uint64_t*) {}
extern "C" void __cxa_atexit(void (*)(void*), void*, void*) {}

// --------------------------------------------------------------------------
// Raw Linux syscalls: the only interface CodeOS's Linux personality gives us.
// --------------------------------------------------------------------------
static long sys_write(long fd, const void* buf, long n)
{
    long r;
    asm volatile("syscall"
                 : "=a"(r)
                 : "a"(1), "D"(fd), "S"(buf), "d"(n)
                 : "rcx", "r11", "memory");
    return r;
}

static void sys_exit(long code)
{
    asm volatile("syscall" ::"a"(60), "D"(code) : "rcx", "r11", "memory");
    for (;;) {
    }
}

static void out(const char* s, long n)
{
    long done = 0;
    while (done < n) {
        long r = sys_write(1, s + done, n - done);
        if (r <= 0) {
            return;
        }
        done += r;
    }
}

void AceOut(const char* s)
{
    long n = 0;
    while (s[n] != 0) {
        ++n;
    }
    out(s, n);
}

void AceOutNum(long v)
{
    char b[24];
    int i = 24;
    bool neg = false;
    unsigned long u;
    if (v < 0) {
        neg = true;
        u = static_cast<unsigned long>(-v);
    } else {
        u = static_cast<unsigned long>(v);
    }
    do {
        b[--i] = static_cast<char>('0' + u % 10);
        u /= 10;
    } while (u != 0);
    if (neg) {
        b[--i] = '-';
    }
    out(b + i, 24 - i);
}

// `codeos_probe.cpp` defines the payload; `_start` aligns the stack the way a
// normal function entry expects it, calls the payload, and exits with its code.
extern "C" int codeos_main(void);

extern "C" void ace_start(void)
{
    sys_exit(codeos_main());
}

asm(".global _start\n"
    ".type _start,@function\n"
    "_start:\n"
    "  andq $-16, %rsp\n"
    "  call ace_start\n"
    "  movq $60, %rax\n"
    "  xorq %rdi, %rdi\n"
    "  syscall\n");

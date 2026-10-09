/* CodeOS C++ Standard Library Stubs - Implementation */

#include <stdint.h>
#include <stddef.h>

extern "C" {
    void *malloc(size_t size);
    void free(void *p);
}

/* C++ operators - must use size_t per C++ standard */
void *operator new(size_t size) {
    return malloc(size);
}

void *operator new[](size_t size) {
    return malloc(size);
}

void operator delete(void *ptr) noexcept {
    free(ptr);
}

void operator delete[](void *ptr) noexcept {
    free(ptr);
}

void operator delete(void *ptr, size_t size) noexcept {
    (void)size;
    free(ptr);
}

void operator delete[](void *ptr, size_t size) noexcept {
    (void)size;
    free(ptr);
}

/* C++ exception handling stubs */
extern "C" {
    void __cxa_pure_virtual(void) {
        while(1);
    }

    void __cxa_atexit(void (*func)(void*), void *arg, void *dso_handle) {
        (void)func; (void)arg; (void)dso_handle;
    }

    void __cxa_finalize(void *dso_handle) {
        (void)dso_handle;
    }
}

#ifndef WINE_STUBS_H
#define WINE_STUBS_H

#include "types.h"

/* Stub function type */
typedef uint64_t (*win32_stub_fn)(void);

/* Stub registry entry */
typedef struct {
    const char *dll;
    const char *name;
    uint16_t ordinal;
    win32_stub_fn fn;
} win32_stub_entry_t;

/* Lookup a stub by DLL name + function name. Returns fn pointer or 0. */
uint64_t wine_resolve_import(const char *dll, const char *name);

/* Lookup a stub by DLL name + ordinal. Returns fn pointer or 0. */
uint64_t wine_resolve_import_ordinal(const char *dll, uint16_t ordinal);

#endif

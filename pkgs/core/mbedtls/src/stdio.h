/* Minimal freestanding stdio.h shim.  The configured TLS path does not use
 * stdio (DEBUG_C and FS_IO are disabled); this only satisfies headers that
 * include <stdio.h> for type definitions. */
#ifndef MBEDTLS_CODEOS_STDIO_H
#define MBEDTLS_CODEOS_STDIO_H
#include <stddef.h>
#endif

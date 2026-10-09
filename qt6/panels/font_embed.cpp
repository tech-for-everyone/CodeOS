#include <stdint.h>

extern "C" void kprintf(const char *fmt, ...);

extern "C" void font_embed_load(void) {
    kprintf("font_embed: using CodeOS bitmap font (no TTF needed)\n");
}

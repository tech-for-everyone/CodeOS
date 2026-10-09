#ifndef KPRINTF_H
#define KPRINTF_H

#include "types.h"

void kprintf(const char *fmt, ...);
void kputs(const char *s);
void kputchar(char c);

void kprintf_set_output(void (*putchar_fn)(char), void (*clear_fn)(void));
void kprintf_set_serial(void (*putchar_fn)(char));
void kprintf_disable_output(void);

void kprintf_capture_begin(void);
int  kprintf_capture_end(void);
const char *kprintf_capture_get(void);

#endif

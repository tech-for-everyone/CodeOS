#include "unistd.h"

void exit(int status) {
    sys_exit(status);
    while (1);
}

int atoi(const char *s) {
    int val = 0, neg = 0;
    while (*s == ' ' || *s == '\t') s++;
    if (*s == '-') { neg = 1; s++; }
    else if (*s == '+') s++;
    while (*s >= '0' && *s <= '9') {
        val = val * 10 + (*s - '0');
        s++;
    }
    return neg ? -val : val;
}

static char *heap_cur;
static char *heap_end;

void *malloc(int size) {
    if (size <= 0) size = 1;
    if (size < 16) size = 16;
    size = (size + 15) & ~15;

    if (!heap_cur) {
        heap_cur = (char *)sys_brk(0);
        if (!heap_cur || heap_cur == (void *)-1) return 0;
        heap_end = heap_cur;
    }

    if (heap_cur + size > heap_end) {
        char *new_brk = (char *)sys_brk(heap_end + (size > 4096 ? size : 4096));
        if (!new_brk || new_brk == (void *)-1) return 0;
        heap_end = new_brk;
    }

    void *ptr = heap_cur;
    heap_cur += size;
    return ptr;
}

void free(void *p) {
    (void)p;
}

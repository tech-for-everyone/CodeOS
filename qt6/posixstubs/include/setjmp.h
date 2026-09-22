/* CodeOS setjmp/longjmp - Required by Qt6 (even without exceptions,
 * Qt6's event loop and signal handling uses these) */

#ifndef SETJMP_H
#define SETJMP_H

#include <stdint.h>

typedef struct {
    uint64_t rbx, rbp, r12, r13, r14, r15;
    uint64_t rsp, rip;
    uint64_t mxcsr;
    uint32_t fpsw;
} jmp_buf[1];

int setjmp(jmp_buf buf);
void longjmp(jmp_buf buf, int val);

#endif /* SETJMP_H */

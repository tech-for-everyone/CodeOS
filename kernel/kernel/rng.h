#ifndef RNG_H
#define RNG_H

#include "types.h"

void rng_seed(uint64_t s);
uint64_t rng_next(void);
int rng_secure_bytes(void *buf, size_t len);
void rng_init(void);

#endif

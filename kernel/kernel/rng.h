#ifndef RNG_H
#define RNG_H

#include "types.h"

void rng_seed(uint64_t s);
uint64_t rng_next(void);
void rng_init(void);

#endif

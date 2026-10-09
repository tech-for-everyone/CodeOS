#ifndef TIMER_H
#define TIMER_H

#include "types.h"

void timer_init(uint32_t frequency);
void timer_sleep(uint32_t ms);
void timer_sleep_ms(uint32_t ms);
void timer_wait_ticks(uint64_t ticks);
uint64_t timer_get_ticks(void);
uint32_t timer_get_frequency(void);
uint64_t timer_get_milliseconds(void);
void timer_tick(void);

#endif

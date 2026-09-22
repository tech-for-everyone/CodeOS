#ifndef RTC_H
#define RTC_H

#include "types.h"

void rtc_init(void);
uint64_t rtc_gettime(void);
int rtc_available(void);
uint32_t rtc_read_wallclock(void);

#endif

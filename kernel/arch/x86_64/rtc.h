#ifndef RTC_H
#define RTC_H

#include "types.h"

typedef struct {
    int second, minute, hour, day, month, year;
} rtc_time_t;

void rtc_init(void);
void rtc_read(rtc_time_t *t);
int  rtc_available(void);

#endif

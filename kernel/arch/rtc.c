#include "rtc.h"
#include "io.h"

#define CMOS_ADDR 0x70
#define CMOS_DATA 0x71

#define RTC_SECONDS  0x00
#define RTC_MINUTES  0x02
#define RTC_HOURS    0x04
#define RTC_DAY      0x07
#define RTC_MONTH    0x08
#define RTC_YEAR     0x09
#define RTC_CENTURY  0x32
#define RTC_STAT_B   0x0B

static int rtc_initialized;

static uint8_t cmos_read(uint8_t reg) {
    outb(CMOS_ADDR, reg | 0x80);
    return inb(CMOS_DATA);
}

static void rtc_wait_ready(void) {
    for (int i = 0; i < 1000000; i++) {
        if (!(cmos_read(0x0A) & 0x80))
            return;
        asm volatile("pause");
    }
}

static int is_bcd(void) {
    return !(cmos_read(RTC_STAT_B) & 0x04);
}

static int bcd_to_bin(int val) {
    return (val & 0x0F) + ((val / 16) * 10);
}

void rtc_init(void) {
    rtc_initialized = 1;
}

int rtc_available(void) {
    return rtc_initialized;
}

void rtc_read(rtc_time_t *t) {
    rtc_wait_ready();
    int bcd = is_bcd();
    int second = cmos_read(RTC_SECONDS);
    int minute = cmos_read(RTC_MINUTES);
    int hour   = cmos_read(RTC_HOURS);
    int day    = cmos_read(RTC_DAY);
    int month  = cmos_read(RTC_MONTH);
    int year   = cmos_read(RTC_YEAR);
    int century = cmos_read(RTC_CENTURY);

    if (bcd) {
        second = bcd_to_bin(second);
        minute = bcd_to_bin(minute);
        hour   = bcd_to_bin(hour);
        day    = bcd_to_bin(day);
        month  = bcd_to_bin(month);
        year   = bcd_to_bin(year);
        if (century) century = bcd_to_bin(century);
    }

    t->second = second;
    t->minute = minute;
    t->hour   = hour;
    t->day    = day;
    t->month  = month;
    if (century)
        t->year = century * 100 + year;
    else
        t->year = 2000 + year;
}

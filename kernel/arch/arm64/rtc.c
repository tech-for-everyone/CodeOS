#include "rtc.h"

/* ARM64 Generic Timer - system counter
 * Accessed via CNTPCT_EL0 (physical count register)
 * QEMU virt platform typically has a 62.5MHz timer (16ns period)
 *
 * For a simple time-of-day, we count ticks since boot.
 * A proper implementation would use PL031 RTC at 0x09010000.
 */

static uint64_t timer_freq;

void rtc_init(void) {
    /* Read the timer frequency from CNTFRQ_EL0 */
    __asm__ volatile("mrs %0, cntfrq_el0" : "=r"(timer_freq));
    if (timer_freq == 0)
        timer_freq = 62500000; /* QEMU default */
}

uint64_t rtc_gettime(void) {
    uint64_t ticks;
    __asm__ volatile("mrs %0, cntpct_el0" : "=r"(ticks));
    return ticks / timer_freq;
}

int rtc_available(void) {
    return timer_freq != 0;
}

/* PL031 real-time clock on the virt platform: 32-bit seconds since 1970. */
#define PL031_BASE    0x09010000UL
#define PL031_RTCDR   0x000       /* data register (seconds) */
#define PL031_RTCMR   0x004
#define PL031_RTCICR  0x00c       /* interrupt clear */
#define PL031_RTCSTAT 0x010       /* bit 0 = BUSY */

uint32_t rtc_read_wallclock(void) {
    volatile uint32_t *stat = (volatile uint32_t *)(PL031_BASE + PL031_RTCSTAT);
    volatile uint32_t *dr   = (volatile uint32_t *)(PL031_BASE + PL031_RTCDR);
    volatile uint32_t *icr  = (volatile uint32_t *)(PL031_BASE + PL031_RTCICR);
    uint32_t retries = 100000;
    while ((*stat & 1) && retries--) { __asm__ volatile("yield"); }
    *icr = 0x0;                 /* clear any pending interrupt */
    return *dr;
}

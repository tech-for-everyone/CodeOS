#include "timer.h"
#include "../kernel/idt.h"
#include "../kernel/sched.h"

#ifdef __aarch64__
/* ARM Generic Timer */
static volatile uint64_t tick_count = 0;
static uint32_t timer_frequency = 100;

void timer_init(uint32_t frequency) {
    if (frequency == 0) frequency = 100;
    /* Read the counter frequency from CNTFRQ_EL0 */
    uint64_t freq;
    __asm__ volatile("mrs %0, cntfrq_el0" : "=r"(freq));
    if (freq == 0) freq = 62500000; /* QEMU default */
    timer_frequency = frequency;
    tick_count = 0;
}

uint64_t timer_get_ticks(void) {
    return tick_count;
}

uint32_t timer_get_frequency(void) {
    return timer_frequency;
}

uint64_t timer_get_milliseconds(void) {
    if (timer_frequency == 0) return 0;
    return (timer_get_ticks() * 1000ULL) / timer_frequency;
}

void timer_tick(void) {
    tick_count++;
}

void timer_sleep(uint32_t ms) {
    uint64_t target = timer_get_ticks() + ((uint64_t)ms * timer_frequency) / 1000ULL;
    while (timer_get_ticks() < target) {
        __asm__ volatile("yield");
    }
}

void timer_sleep_ms(uint32_t ms) { timer_sleep(ms); }

void timer_wait_ticks(uint64_t ticks) {
    uint64_t target = timer_get_ticks() + ticks;
    while (timer_get_ticks() < target) {
        __asm__ volatile("yield");
    }
}

#else /* x86 PIT */

#include "../arch/x86_64/io.h"

#define PIT_CMD 0x43
#define PIT_DATA 0x40
#define PIT_INPUT_FREQ 1193182

static volatile uint64_t tick_count = 0;
static uint32_t timer_frequency = 100;

static void timer_irq(int_frame_t *frame) {
    (void)frame;
    tick_count++;
    sched_timer_tick(frame);
}

void timer_init(uint32_t frequency) {
    if (frequency == 0) frequency = 100;
    timer_frequency = frequency;

    uint32_t divisor = PIT_INPUT_FREQ / timer_frequency;
    if (divisor < 2) divisor = 2;
    if (divisor > 65535) divisor = 65535;
    outb(PIT_CMD, 0x36);
    outb(PIT_DATA, divisor & 0xFF);
    outb(PIT_DATA, (divisor >> 8) & 0xFF);

    tick_count = 0;
    irq_register(0, timer_irq);
}

void timer_sleep(uint32_t ms) {
    uint64_t target = timer_get_ticks() + ((uint64_t)ms * timer_frequency) / 1000ULL;
    int intr_enabled = idt_enabled();
    while (timer_get_ticks() < target) {
        if (intr_enabled)
            __asm__ volatile("hlt");
        else
            __asm__ volatile("pause");
    }
}

void timer_sleep_ms(uint32_t ms) {
    timer_sleep(ms);
}

void timer_wait_ticks(uint64_t ticks) {
    uint64_t target = timer_get_ticks() + ticks;
    while (timer_get_ticks() < target) {
        __asm__ volatile("pause");
    }
}

uint64_t timer_get_ticks(void) {
    return tick_count;
}

uint32_t timer_get_frequency(void) {
    return timer_frequency;
}

uint64_t timer_get_milliseconds(void) {
    if (timer_frequency == 0) return 0;
    return (timer_get_ticks() * 1000ULL) / timer_frequency;
}

void timer_tick(void) {
    tick_count++;
}
#endif

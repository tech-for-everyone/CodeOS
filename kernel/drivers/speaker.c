#include "speaker.h"
#include "../arch/x86_64/io.h"
#include "../drivers/timer.h"

#define PIT_CMD  0x43
#define PIT_DATA 0x42
#define SPEAKER_PORT 0x61

static int speaker_busy;
static uint64_t speaker_end_ms;

void speaker_init(void) {
    outb(SPEAKER_PORT, inb(SPEAKER_PORT) & ~0x03);
    speaker_busy = 0;
    speaker_end_ms = 0;
}

void speaker_on(int freq) {
    if (freq <= 0) return;
    uint32_t div = 1193182 / freq;
    outb(PIT_CMD, 0xB6);
    outb(PIT_DATA, div & 0xFF);
    outb(PIT_DATA, (div >> 8) & 0xFF);
    uint8_t tmp = inb(SPEAKER_PORT);
    if (tmp != (tmp | 0x03))
        outb(SPEAKER_PORT, tmp | 0x03);
}

void speaker_off(void) {
    outb(SPEAKER_PORT, inb(SPEAKER_PORT) & ~0x03);
}

void speaker_beep(int freq, int duration_ms) {
    speaker_on(freq);
    uint64_t start = timer_get_milliseconds();
    while ((int)(timer_get_milliseconds() - start) < duration_ms) {
        for (volatile int w = 0; w < 10000; w++) asm volatile("pause");
    }
    speaker_off();
}

void speaker_beep_async(int freq, int duration_ms) {
    speaker_on(freq);
    speaker_busy = 1;
    speaker_end_ms = timer_get_milliseconds() + duration_ms;
}

int speaker_tick(void) {
    if (!speaker_busy) return 0;
    if (timer_get_milliseconds() >= speaker_end_ms) {
        speaker_off();
        speaker_busy = 0;
        return 1;
    }
    return 0;
}

int speaker_is_busy(void) {
    return speaker_busy;
}

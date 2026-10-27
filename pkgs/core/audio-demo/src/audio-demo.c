/* audio-demo: a tone generator that plays through the audio callback.
 *
 * The point of the demo is the callback, not the sound.  The program
 * registers a callback, the pump calls it when the device wants samples,
 * and the samples go to the kernel's mixer, which is what the device
 * plays.
 *
 * It prints what it did, because a demo nobody can check is a demo
 * nobody believes: the boot check runs this and reads the numbers off
 * the serial log.
 */
#include "audio.h"
#include "stdio.h"
#include "unistd.h"
#include <stdint.h>

#define TONE_HZ      440
#define TONE_FRAMES  4800        /* a tenth of a second at 48 kHz */
#define PUMPS        200

/* The tone's state.  A triangle wave, because it is exact in integers:
 * the userspace has no floating point and a sine would need a table. */
static int32_t phase;            /* 0 to 2^32, one turn of the triangle */
static int32_t phase_step;       /* how far the phase moves a frame */
static int32_t frames_done;

static void fill(void *user, int16_t *frames, int count) {
    (void)user;
    for (int i = 0; i < count; i++) {
        /* A triangle: the phase's top half is the rise, the bottom half
         * the fall.  12000 is about a third of full scale, which leaves
         * room for the mixer to sum it with anything else. */
        int32_t v = (phase & 0x80000000) ? (phase >> 17) - 0x4000
                                         : 0x4000 - (phase >> 17);
        frames[i * 2] = (int16_t)v;
        frames[i * 2 + 1] = (int16_t)v;
        phase += phase_step;
        frames_done++;
    }
}

int main(void) {
    printf("audio-demo: opening a stream\n");

    int id = audio_open(48000, 2);
    if (id < 0) {
        printf("audio-demo: no stream (the mixer is full)\n");
        return 1;
    }
    printf("audio-demo: stream %d\n", id);

    /* The phase moves 2^32 / (rate / hz) per frame. */
    phase_step = (int32_t)((4294967296LL * TONE_HZ) / 48000);

    audio_set_callback(fill, 0);

    int filled = 0;
    for (int i = 0; i < PUMPS; i++) {
        int n = audio_pump();
        filled += n;
        if (n == 0) {
            /* The stream is full: the device is not draining it (there
             * may be no audio device at all), so there is nothing to do
             * but wait. */
            sys_sleep(10);
        }
    }

    printf("audio-demo: callback filled %d frames\n", filled);
    printf("audio-demo: tone %d Hz for %d frames\n", TONE_HZ, frames_done);
    printf("audio-demo: stream position %d bytes\n", audio_position());

    audio_close();
    return 0;
}

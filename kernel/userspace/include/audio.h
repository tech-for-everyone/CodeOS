#ifndef AUDIO_H
#define AUDIO_H

#include <stdint.h>

/* The userspace's audio API.
 *
 * A program that makes sound opens a stream, registers a callback, and
 * lets the callback be called when the device wants samples:
 *
 *     int id = audio_open(48000, 2);
 *     audio_set_callback(my_fill, &my_state);
 *     for (;;) {
 *         ... whatever the program's own loop does ...
 *         audio_pump();
 *     }
 *
 * The callback is called with a buffer to fill and the number of stereo
 * frames wanted (16 bits a sample).  It may fill fewer frames -- the
 * rest is silence -- but not more.  It runs in the calling thread, from
 * audio_pump(), so it must not sleep and must not call back into the
 * shell.
 *
 * The samples go to the kernel's mixer, and the mixer is what the device
 * plays: a stream that nobody writes to is silence, and a stream that is
 * written to faster than it is played grows until it is full, which
 * audio_free_bytes() reports.
 */

#define AUDIO_RATE_DEFAULT     48000
#define AUDIO_CHANNELS_DEFAULT 2
#define AUDIO_FRAMES_MAX       1024

/* The callback that fills the stream. */
typedef void (*audio_callback_t)(void *user, int16_t *frames, int count);

/* Open a stream.  Returns the stream id, or -1 when the mixer has no
 * room for another one. */
int audio_open(int rate, int channels);

/* The callback that fills the stream.  Returns 0. */
int audio_set_callback(audio_callback_t cb, void *user);

/* Call the callback when the device wants samples, and hand it what it
 * filled to the device.
 *
 * Returns the number of frames the callback filled, or 0 when it was not
 * called (no callback registered, or the stream is full). */
int audio_pump(void);

/* Write samples to the stream directly, with no callback involved.
 * Returns the number of bytes accepted. */
int audio_write(const int16_t *frames, int count);

/* How many bytes may still be written to the stream. */
int audio_free_bytes(void);

/* How many bytes have been played of the stream. */
int audio_position(void);

/* Start, pause and stop the stream. */
int audio_play(void);
int audio_pause(void);
int audio_stop(void);

/* The stream's volume, 0 to 100. */
int audio_set_volume(int volume);
int audio_get_volume(void);

int audio_close(void);

#endif

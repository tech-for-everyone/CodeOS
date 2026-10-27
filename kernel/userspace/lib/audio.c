/* The userspace's audio API: a stream, and a callback that fills it.
 *
 * The kernel's mixer holds the samples; the device plays them.  The
 * callback is the interesting part: a program registers one and the
 * stream asks for samples through it, so the program does not have to
 * know how much room the mixer has or when the device is ready.
 */
#include "audio.h"
#include "unistd.h"

/* The registered callback and its argument. */
static audio_callback_t fill_cb;
static void *fill_user;

/* The stream this process opened.  The mixer has a fixed number of
 * sources, so a program gets one of them and keeps it until it closes
 * it. */
static int stream_id = -1;

/* The buffer the callback fills.  It is static because the callback is
 * called with a pointer to it and the frame count has to survive the
 * call. */
static int16_t fill_buf[AUDIO_FRAMES_MAX * 2];

int audio_open(int rate, int channels) {
    if (rate <= 0) rate = AUDIO_RATE_DEFAULT;
    if (channels <= 0) channels = AUDIO_CHANNELS_DEFAULT;
    int id = sys_audio_open(rate, channels);
    if (id < 0) return -1;
    stream_id = id;
    /* A stream that is not playing is a stream nobody hears: the mixer
     * only mixes the sources that are playing. */
    sys_audio_set_state(stream_id, AUDIO_STATE_PLAY);
    return stream_id;
}

int audio_set_callback(audio_callback_t cb, void *user) {
    fill_cb = cb;
    fill_user = user;
    return 0;
}

int audio_pump(void) {
    if (!fill_cb || stream_id < 0) return 0;

    /* How much room is there?  The mixer knows; a frame is four bytes
     * (two channels of 16 bits). */
    int free_bytes = sys_audio_get_free(stream_id);
    if (free_bytes <= 0) return 0;

    int frames = free_bytes / 4;
    if (frames > AUDIO_FRAMES_MAX) frames = AUDIO_FRAMES_MAX;
    if (frames <= 0) return 0;

    /* Clear the buffer first: a callback that fills fewer frames than
     * were asked for leaves the rest as it was, and stale samples from
     * the last round would be played again. */
    for (int i = 0; i < frames * 2; i++) fill_buf[i] = 0;

    fill_cb(fill_user, fill_buf, frames);

    int written = sys_audio_write(stream_id, fill_buf, frames * 4);
    if (written < 0) return 0;
    return written / 4;
}

int audio_write(const int16_t *frames, int count) {
    if (!frames || count <= 0 || stream_id < 0) return 0;
    return sys_audio_write(stream_id, frames, count * 4);
}

int audio_free_bytes(void) {
    if (stream_id < 0) return 0;
    int free_bytes = sys_audio_get_free(stream_id);
    return free_bytes > 0 ? free_bytes : 0;
}

int audio_position(void) {
    if (stream_id < 0) return 0;
    return sys_audio_get_position(stream_id);
}

int audio_play(void) {
    if (stream_id < 0) return -1;
    return sys_audio_set_state(stream_id, AUDIO_STATE_PLAY);
}

int audio_pause(void) {
    if (stream_id < 0) return -1;
    return sys_audio_set_state(stream_id, AUDIO_STATE_PAUSE);
}

int audio_stop(void) {
    if (stream_id < 0) return -1;
    return sys_audio_set_state(stream_id, AUDIO_STATE_STOP);
}

int audio_set_volume(int volume) {
    if (stream_id < 0) return -1;
    return sys_audio_set_volume(stream_id, volume);
}

int audio_get_volume(void) {
    if (stream_id < 0) return -1;
    return sys_audio_get_volume(stream_id);
}

int audio_close(void) {
    if (stream_id < 0) return 0;
    int r = sys_audio_close(stream_id);
    stream_id = -1;
    fill_cb = 0;
    fill_user = 0;
    return r;
}

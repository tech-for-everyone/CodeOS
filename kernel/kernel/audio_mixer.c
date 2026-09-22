#include "audio_mixer.h"
#include "kprintf.h"

static audio_source_t sources[MIXER_MAX_SOURCES];
static int master_volume = 80;

void mixer_init(void) {
    for (int i = 0; i < MIXER_MAX_SOURCES; i++) {
        sources[i].id = -1;
        sources[i].state = AUDIO_STOPPED;
        sources[i].read_pos = 0;
        sources[i].write_pos = 0;
        sources[i].bytes_written = 0;
        sources[i].bytes_played = 0;
        sources[i].volume = 100;
        sources[i].sample_rate = 48000;
        sources[i].channels = 2;
    }
    master_volume = 80;
    kprintf("mixer: initialized (%d sources, master=%d)\n", MIXER_MAX_SOURCES, master_volume);
}

int mixer_open_source(int sample_rate, int channels) {
    for (int i = 0; i < MIXER_MAX_SOURCES; i++) {
        if (sources[i].id < 0) {
            sources[i].id = i;
            sources[i].state = AUDIO_STOPPED;
            sources[i].read_pos = 0;
            sources[i].write_pos = 0;
            sources[i].bytes_written = 0;
            sources[i].bytes_played = 0;
            sources[i].volume = 100;
            sources[i].sample_rate = sample_rate > 0 ? sample_rate : 48000;
            sources[i].channels = channels > 0 ? channels : 2;
            return i;
        }
    }
    return -1;
}

void mixer_close_source(int id) {
    if (id < 0 || id >= MIXER_MAX_SOURCES) return;
    sources[id].id = -1;
    sources[id].state = AUDIO_STOPPED;
    sources[id].read_pos = 0;
    sources[id].write_pos = 0;
    sources[id].bytes_written = 0;
    sources[id].bytes_played = 0;
}

static uint32_t ring_used(audio_source_t *s) {
    return s->bytes_written - s->bytes_played;
}

static uint32_t ring_free(audio_source_t *s) {
    return MIXER_RING_SIZE - ring_used(s);
}

int mixer_write(int id, const void *data, int bytes) {
    if (id < 0 || id >= MIXER_MAX_SOURCES || sources[id].id < 0) return -1;
    if (bytes <= 0) return 0;

    audio_source_t *s = &sources[id];
    uint32_t free = ring_free(s);
    if ((uint32_t)bytes > free) bytes = (int)free;
    if (bytes <= 0) return 0;

    const int16_t *src = (const int16_t *)data;
    int16_t *dst = s->ring;
    uint32_t pos = s->write_pos % MIXER_RING_SIZE;
    int written = 0;

    for (int i = 0; i < bytes / 2; i++) {
        dst[pos / 2] = src[i];
        pos += 2;
        if (pos >= MIXER_RING_SIZE) pos = 0;
        written += 2;
    }

    s->write_pos += written;
    s->bytes_written += written;
    return written;
}

int mixer_get_free(int id) {
    if (id < 0 || id >= MIXER_MAX_SOURCES || sources[id].id < 0) return 0;
    return (int)ring_free(&sources[id]);
}

void mixer_set_volume(int id, int vol) {
    if (id < 0 || id >= MIXER_MAX_SOURCES) return;
    if (vol < 0) vol = 0;
    if (vol > 100) vol = 100;
    sources[id].volume = vol;
}

int mixer_get_volume(int id) {
    if (id < 0 || id >= MIXER_MAX_SOURCES) return 0;
    return sources[id].volume;
}

void mixer_play(int id) {
    if (id < 0 || id >= MIXER_MAX_SOURCES) return;
    sources[id].state = AUDIO_PLAYING;
}

void mixer_pause(int id) {
    if (id < 0 || id >= MIXER_MAX_SOURCES) return;
    sources[id].state = AUDIO_PAUSED;
}

void mixer_stop(int id) {
    if (id < 0 || id >= MIXER_MAX_SOURCES) return;
    sources[id].state = AUDIO_STOPPED;
    sources[id].read_pos = 0;
    sources[id].write_pos = 0;
    sources[id].bytes_played = 0;
}

int mixer_get_position(int id) {
    if (id < 0 || id >= MIXER_MAX_SOURCES) return 0;
    return (int)sources[id].bytes_played;
}

int mixer_is_active(int id) {
    if (id < 0 || id >= MIXER_MAX_SOURCES) return 0;
    return sources[id].id >= 0;
}

void mixer_mix(int16_t *out, int samples) {
    for (int i = 0; i < samples; i++) {
        int32_t mixed = 0;

        for (int s = 0; s < MIXER_MAX_SOURCES; s++) {
            if (sources[s].id < 0 || sources[s].state != AUDIO_PLAYING)
                continue;
            if (ring_used(&sources[s]) < 2)
                continue;

            int16_t sample = sources[s].ring[sources[s].read_pos % (MIXER_RING_SIZE / 2)];
            int32_t scaled = ((int32_t)sample * sources[s].volume) / 100;
            mixed += scaled;

            sources[s].read_pos += 2;
            if (sources[s].read_pos >= MIXER_RING_SIZE)
                sources[s].read_pos = 0;
            sources[s].bytes_played += 2;
        }

        mixed = (mixed * master_volume) / 100;

        if (mixed > 32767) mixed = 32767;
        else if (mixed < -32768) mixed = -32768;
        out[i] = (int16_t)mixed;
    }
}

void mixer_set_master_volume(int vol) {
    if (vol < 0) vol = 0;
    if (vol > 100) vol = 100;
    master_volume = vol;
}

int mixer_get_master_volume(void) {
    return master_volume;
}

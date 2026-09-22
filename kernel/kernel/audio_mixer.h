#ifndef AUDIO_MIXER_H
#define AUDIO_MIXER_H

#include <stdint.h>

#define MIXER_MAX_SOURCES 8
#define MIXER_RING_SIZE   65536

typedef enum {
    AUDIO_STOPPED = 0,
    AUDIO_PLAYING,
    AUDIO_PAUSED
} audio_state_t;

typedef struct {
    int           id;
    audio_state_t state;
    int16_t       ring[MIXER_RING_SIZE / 2];
    uint32_t      read_pos;
    uint32_t      write_pos;
    uint32_t      bytes_written;
    uint32_t      bytes_played;
    int           volume;
    int           sample_rate;
    int           channels;
} audio_source_t;

void     mixer_init(void);
int      mixer_open_source(int sample_rate, int channels);
void     mixer_close_source(int id);
int      mixer_write(int id, const void *data, int bytes);
int      mixer_get_free(int id);
void     mixer_set_volume(int id, int vol);
int      mixer_get_volume(int id);
void     mixer_play(int id);
void     mixer_pause(int id);
void     mixer_stop(int id);
int      mixer_get_position(int id);
int      mixer_is_active(int id);
void     mixer_mix(int16_t *out, int samples);
void     mixer_set_master_volume(int vol);
int      mixer_get_master_volume(void);

#endif

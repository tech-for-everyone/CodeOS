#ifndef AUDIO_H
#define AUDIO_H

#include "types.h"

/* Audio subsystem: combines PC speaker and AC97 PCM output */

typedef enum {
    NOTE_C4 = 262,
    NOTE_D4 = 294,
    NOTE_E4 = 330,
    NOTE_F4 = 349,
    NOTE_G4 = 392,
    NOTE_A4 = 440,
    NOTE_B4 = 494,
    NOTE_C5 = 523,
    NOTE_D5 = 587,
    NOTE_E5 = 659,
    NOTE_F5 = 698,
    NOTE_G5 = 784,
    NOTE_A5 = 880,
    NOTE_B5 = 988,
} note_t;

void audio_init(void);
void audio_tick(void);                    /* call once per frame */
void audio_play_note(int freq, int duration_ms);
void audio_play_note_vol(int freq, int duration_ms, int volume);
void audio_play_melody(const int *notes, const int *durations, int count);

/* Predefined notification sounds */
void audio_notify_beep(void);
void audio_error_beep(void);
void audio_success_beep(void);
void audio_silence(void);

#endif

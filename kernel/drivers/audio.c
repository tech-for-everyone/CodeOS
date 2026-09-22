#include "audio.h"
#include "speaker.h"
#include "ac97.h"
#include "../kernel/kprintf.h"
#include "../kernel/audio_mixer.h"
#include "types.h"

void audio_init(void) {
    speaker_init();
    ac97_init();
    mixer_init();
    if (ac97.present) {
        ac97_stream_init();
        ac97_stream_start();
    }
    kprintf("audio: initialized (speaker + ac97 + mixer)\n");
}

void audio_tick(void) {
    speaker_tick();
}

void audio_play_note(int freq, int duration_ms) {
    audio_play_note_vol(freq, duration_ms, 100);
}

void audio_play_note_vol(int freq, int duration_ms, int volume) {
    if (volume < 0) volume = 0;
    if (volume > 100) volume = 100;
    int amp = (volume * 16000) / 100;
    if (ac97.present) {
        /* Generate a simple sine-like square wave PCM buffer */
        static int16_t buf[2048];
        int samples = (duration_ms * 48000) / 1000;
        if (samples > 2048) samples = 2048;
        if (samples < 64) samples = 64;

        for (int i = 0; i < samples; i++) {
            int16_t val = (i * freq / 48000) % 2 ? (int16_t)amp : (int16_t)-amp;
            buf[i] = val;
        }
        ac97_play_pcm(buf, samples);
    } else {
        speaker_beep_async(freq, duration_ms);
    }
}

void audio_play_melody(const int *notes, const int *durations, int count) {
    (void)notes;
    (void)durations;
    (void)count;
}

void audio_notify_beep(void) {
    audio_play_note(NOTE_G5, 80);
}

void audio_error_beep(void) {
    audio_play_note(NOTE_C4, 150);
    audio_play_note(NOTE_E4, 150);
}

void audio_success_beep(void) {
    audio_play_note(NOTE_C5, 60);
    audio_play_note(NOTE_E5, 60);
    audio_play_note(NOTE_G5, 100);
}

void audio_silence(void) {
    speaker_off();
    ac97_stop();
}

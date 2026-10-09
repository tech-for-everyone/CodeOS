#ifndef SPEAKER_H
#define SPEAKER_H

void speaker_init(void);
void speaker_on(int freq);
void speaker_off(void);
void speaker_beep(int freq, int duration_ms);
void speaker_beep_async(int freq, int duration_ms);
int  speaker_tick(void);
int  speaker_is_busy(void);

#endif

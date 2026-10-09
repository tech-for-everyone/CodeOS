#ifndef AC97_H
#define AC97_H

#include "types.h"

/* AC97 mixer registers (NAMM - BAR1) */
#define AC97_RESET        0x00
#define AC97_MASTER_VOL   0x02
#define AC97_PCM_VOL      0x18

/* AC97 bus master registers (NABM - BAR0) */
#define AC97_PO_CTRL      0x00
#define AC97_PO_STAT      0x02
#define AC97_PO_LVI       0x04
#define AC97_PO_PICB      0x06
#define AC97_PO_PI        0x08
#define AC97_PO_CR        0x0A
#define AC97_PO_BDBAR     0x10
#define AC97_PO_CIV       0x14

/* Control/Status bits */
#define AC97_CTRL_RUN     0x01
#define AC97_CTRL_RESET   0x02
#define AC97_CTRL_FILL    0x04
#define AC97_CTRL_IOCE    0x08

/* Buffer descriptor */
typedef struct {
    uint32_t pointer;
    uint16_t control;
    uint16_t length;
} __attribute__((packed)) ac97_bd_t;

/* AC97 state */
typedef struct {
    int      present;
    uint16_t nabm_base;
    uint16_t namm_base;
    uint8_t  irq;
    uint8_t  bd_idx;
    uint8_t  num_bds;
    ac97_bd_t *bds;
} ac97_t;

extern ac97_t ac97;

int  ac97_init(void);
int  ac97_play_pcm(const void *data, int samples);
void ac97_set_volume(int vol);
int  ac97_is_playing(void);
void ac97_stop(void);

/* Streaming API — uses mixer for continuous playback */
void ac97_stream_init(void);
void ac97_stream_start(void);
void ac97_stream_stop(void);
void ac97_stream_fill(int16_t *buf, int samples);

#endif

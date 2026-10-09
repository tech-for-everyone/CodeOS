#ifndef USB_AUDIO_H
#define USB_AUDIO_H

#include "types.h"

/* USB Audio Class (UAC1/UAC2) — Microphone + Speaker support */

#define AUDIO_MAX_STREAMS 4
#define AUDIO_RING_SIZE   8192

/* Audio class request codes */
#define AUDIO_SET_CUR     0x01
#define AUDIO_GET_CUR     0x81
#define AUDIO_SET_MIN     0x02
#define AUDIO_GET_MIN     0x82
#define AUDIO_SET_MAX     0x03
#define AUDIO_GET_MAX     0x83

/* Audio control interface selectors */
#define AUDIO_FU_MUTE     0x01
#define AUDIO_FU_VOLUME   0x02

/* Audio streaming format types */
#define PCM_FORMAT_TYPE_I 0x01

struct audio_stream {
    struct usb_dev *dev;
    int      ep_in;      /* microphone ISO IN */
    int      ep_out;     /* speaker ISO OUT */
    int      open;
    int      is_input;   /* 1=mic, 0=speaker */
    int      bit_depth;
    int      channels;
    int      sample_rate;
    int      frame_size; /* bytes per sample * channels */
    uint8_t  ring_buf[AUDIO_RING_SIZE];
    int      ring_head;
    int      ring_tail;
    int      ring_count;
    uint16_t max_packet;
    uint8_t  alt_setting;
};

int  usb_audio_init(void);
int  usb_audio_available(void);
void usb_audio_print_info(void);

int  usb_audio_get_stream_count(void);
int  usb_audio_open_output(int stream, int sample_rate, int channels, int bits);
int  usb_audio_open_input(int stream, int sample_rate, int channels, int bits);
int  usb_audio_close(int stream);

int  usb_audio_write(int stream, const void *data, int len);
int  usb_audio_read(int stream, void *buf, int max_len);

void usb_audio_set_volume(int stream, int volume);
void usb_audio_set_mute(int stream, int muted);
int  usb_audio_get_volume(int stream);

#endif

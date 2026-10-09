#include "usb_audio.h"
#include "usb_ehci.h"
#include "../kernel/kprintf.h"
#include "../kernel/string.h"
#include "../kernel/pmm.h"
#include "../kernel/vmm.h"

static struct audio_stream audio_streams[AUDIO_MAX_STREAMS];
static int audio_num_streams;
static int audio_ok;

static uint8_t audio_buf[4096] __attribute__((aligned(4096)));

static int audio_set_cur(struct usb_dev *dev, uint8_t iface, uint8_t ep,
                         uint8_t entity, uint8_t selector, uint16_t len,
                         const void *data) {
    return ehci_control_transfer(dev, USB_DIR_OUT, 0x21, AUDIO_SET_CUR,
                                 (selector << 8) | entity, (ep << 8) | iface,
                                 len, (void *)data);
}

static int audio_get_cur(struct usb_dev *dev, uint8_t iface, uint8_t ep,
                         uint8_t entity, uint8_t selector, uint16_t len,
                         void *data) {
    return ehci_control_transfer(dev, USB_DIR_IN, 0xA1, AUDIO_GET_CUR,
                                 (selector << 8) | entity, (ep << 8) | iface,
                                 len, data);
}

int usb_audio_init(void) {
    audio_num_streams = 0;
    audio_ok = 0;

    int nd = ehci_get_num_devs();
    for (int i = 0; i < nd; i++) {
        struct usb_dev *d = ehci_get_dev(i);
        if (!d) continue;
        /* Audio class: 0x01, streaming: 0x02 */
        if (d->class_code == 0x01 && d->subclass == 0x02) {
            if (audio_num_streams >= AUDIO_MAX_STREAMS) break;

            struct audio_stream *s = &audio_streams[audio_num_streams];
            memset(s, 0, sizeof(*s));
            s->dev = d;
            s->ep_in = d->ep_in_addr & 0x0F;
            s->ep_out = d->ep_out_addr & 0x0F;

            if (d->ep_in_addr & 0x80) {
                s->is_input = 1;  /* microphone */
            } else {
                s->is_input = 0;  /* speaker */
            }

            s->bit_depth = 16;
            s->channels = 1;
            s->sample_rate = 44100;
            s->frame_size = 2;
            s->max_packet = d->ep_in_maxp ? d->ep_in_maxp : d->ep_out_maxp;

            kprintf("usb_audio: stream %d at dev %d (%s, VID=%04x PID=%04x)\n",
                    audio_num_streams, i,
                    s->is_input ? "mic" : "speaker",
                    d->vendor_id, d->product_id);
            audio_num_streams++;
        }
    }

    if (audio_num_streams > 0) {
        audio_ok = 1;
        kprintf("usb_audio: %d stream(s) available\n", audio_num_streams);
    }
    return (audio_num_streams > 0) ? 0 : -1;
}

int usb_audio_available(void) { return audio_ok; }
int usb_audio_get_stream_count(void) { return audio_num_streams; }

int usb_audio_open_output(int stream, int sample_rate, int channels, int bits) {
    if (stream < 0 || stream >= audio_num_streams) return -1;
    struct audio_stream *s = &audio_streams[stream];
    if (s->is_input) return -1;

    s->sample_rate = sample_rate;
    s->channels = channels;
    s->bit_depth = bits;
    s->frame_size = (bits / 8) * channels;
    s->open = 1;
    s->ring_head = 0;
    s->ring_tail = 0;
    s->ring_count = 0;

    /* Set sample rate via USB control request */
    uint8_t rate_data[3];
    rate_data[0] = sample_rate & 0xFF;
    rate_data[1] = (sample_rate >> 8) & 0xFF;
    rate_data[2] = (sample_rate >> 16) & 0xFF;
    audio_set_cur(s->dev, 0, s->ep_out, 0, 0x01, 3, rate_data);

    kprintf("usb_audio: output stream %d opened (%dHz %dbit %dch)\n",
            stream, sample_rate, bits, channels);
    return 0;
}

int usb_audio_open_input(int stream, int sample_rate, int channels, int bits) {
    if (stream < 0 || stream >= audio_num_streams) return -1;
    struct audio_stream *s = &audio_streams[stream];
    if (!s->is_input) return -1;

    s->sample_rate = sample_rate;
    s->channels = channels;
    s->bit_depth = bits;
    s->frame_size = (bits / 8) * channels;
    s->open = 1;
    s->ring_head = 0;
    s->ring_tail = 0;
    s->ring_count = 0;

    uint8_t rate_data[3];
    rate_data[0] = sample_rate & 0xFF;
    rate_data[1] = (sample_rate >> 8) & 0xFF;
    rate_data[2] = (sample_rate >> 16) & 0xFF;
    audio_set_cur(s->dev, 0, s->ep_in, 0, 0x01, 3, rate_data);

    kprintf("usb_audio: input stream %d opened (%dHz %dbit %dch)\n",
            stream, sample_rate, bits, channels);
    return 0;
}

int usb_audio_close(int stream) {
    if (stream < 0 || stream >= audio_num_streams) return -1;
    audio_streams[stream].open = 0;
    return 0;
}

int usb_audio_write(int stream, const void *data, int len) {
    if (stream < 0 || stream >= audio_num_streams) return -1;
    struct audio_stream *s = &audio_streams[stream];
    if (!s->open || s->is_input) return -1;

    /* Copy into ring buffer */
    const uint8_t *src = (const uint8_t *)data;
    for (int i = 0; i < len; i++) {
        if (s->ring_count < AUDIO_RING_SIZE) {
            s->ring_buf[s->ring_head] = src[i];
            s->ring_head = (s->ring_head + 1) % AUDIO_RING_SIZE;
            s->ring_count++;
        }
    }

    /* Drain ring buffer to USB if enough data */
    while (s->ring_count >= s->max_packet) {
        int chunk = s->max_packet;
        uint8_t tmp[256];
        if (chunk > (int)sizeof(tmp)) chunk = sizeof(tmp);
        for (int i = 0; i < chunk; i++) {
            tmp[i] = s->ring_buf[s->ring_tail];
            s->ring_tail = (s->ring_tail + 1) % AUDIO_RING_SIZE;
            s->ring_count--;
        }
        ehci_bulk_transfer(s->dev, s->ep_out, USB_DIR_OUT, tmp, chunk);
    }

    return len;
}

int usb_audio_read(int stream, void *buf, int max_len) {
    if (stream < 0 || stream >= audio_num_streams) return -1;
    struct audio_stream *s = &audio_streams[stream];
    if (!s->open || !s->is_input) return -1;

    /* Try to read from USB into ring buffer */
    int r = ehci_bulk_transfer(s->dev, s->ep_in, USB_DIR_IN, audio_buf, s->max_packet);
    if (r > 0) {
        for (int i = 0; i < r; i++) {
            if (s->ring_count < AUDIO_RING_SIZE) {
                s->ring_buf[s->ring_head] = audio_buf[i];
                s->ring_head = (s->ring_head + 1) % AUDIO_RING_SIZE;
                s->ring_count++;
            }
        }
    }

    /* Copy from ring buffer to user */
    if (s->ring_count == 0) return 0;
    int cpy = s->ring_count < max_len ? s->ring_count : max_len;
    uint8_t *dst = (uint8_t *)buf;
    for (int i = 0; i < cpy; i++) {
        dst[i] = s->ring_buf[s->ring_tail];
        s->ring_tail = (s->ring_tail + 1) % AUDIO_RING_SIZE;
        s->ring_count--;
    }
    return cpy;
}

void usb_audio_set_volume(int stream, int volume) {
    if (stream < 0 || stream >= audio_num_streams) return;
    struct audio_stream *s = &audio_streams[stream];
    if (!s->dev) return;
    uint16_t vol = (uint16_t)volume;
    audio_set_cur(s->dev, 0, s->is_input ? s->ep_in : s->ep_out,
                  0, AUDIO_FU_VOLUME, 2, &vol);
}

void usb_audio_set_mute(int stream, int muted) {
    if (stream < 0 || stream >= audio_num_streams) return;
    struct audio_stream *s = &audio_streams[stream];
    if (!s->dev) return;
    uint8_t mute = muted ? 1 : 0;
    audio_set_cur(s->dev, 0, s->is_input ? s->ep_in : s->ep_out,
                  0, AUDIO_FU_MUTE, 1, &mute);
}

int usb_audio_get_volume(int stream) {
    if (stream < 0 || stream >= audio_num_streams) return 0;
    struct audio_stream *s = &audio_streams[stream];
    if (!s->dev) return 0;
    uint16_t vol = 0;
    audio_get_cur(s->dev, 0, s->is_input ? s->ep_in : s->ep_out,
                  0, AUDIO_FU_VOLUME, 2, &vol);
    return (int)vol;
}

void usb_audio_print_info(void) {
    if (!audio_ok) {
        kprintf("usb_audio: no streams\n");
        return;
    }
    kprintf("usb_audio: %d stream(s)\n", audio_num_streams);
    for (int i = 0; i < audio_num_streams; i++) {
        struct audio_stream *s = &audio_streams[i];
        kprintf("  stream %d: %s %dHz %dbit %dch %s\n",
                i, s->is_input ? "mic   " : "speaker",
                s->sample_rate, s->bit_depth, s->channels,
                s->open ? "(open)" : "");
    }
}

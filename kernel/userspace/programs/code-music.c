#include "unistd.h"
#include "stdio.h"
#include "string.h"
#include "stdlib.h"
#include <stdint.h>
#include <stddef.h>

/* ───── WAV file parsing ───── */

#pragma pack(push, 1)
typedef struct {
    char     riff_id[4];
    uint32_t riff_size;
    char     wave_id[4];
} riff_header_t;

typedef struct {
    char     chunk_id[4];
    uint32_t chunk_size;
} chunk_header_t;

typedef struct {
    uint16_t audio_format;
    uint16_t num_channels;
    uint32_t sample_rate;
    uint32_t byte_rate;
    uint16_t block_align;
    uint16_t bits_per_sample;
} fmt_chunk_t;
#pragma pack(pop)

typedef struct {
    int      fd;
    int      has_file;
    char     filename[128];
    uint32_t sample_rate;
    uint16_t num_channels;
    uint16_t bits_per_sample;
    uint32_t data_size;
    uint32_t data_offset;
    uint32_t data_pos;
} wav_file_t;

static wav_file_t wav;

static int wav_open(const char *path) {
    int fd = sys_open(path, 0);
    if (fd < 0) return -1;

    riff_header_t riff;
    int n = sys_read(fd, &riff, sizeof(riff));
    if (n < (int)sizeof(riff) ||
        riff.riff_id[0] != 'R' || riff.riff_id[1] != 'I' ||
        riff.riff_id[2] != 'F' || riff.riff_id[3] != 'F' ||
        riff.wave_id[0] != 'W' || riff.wave_id[1] != 'A' ||
        riff.wave_id[2] != 'V' || riff.wave_id[3] != 'E') {
        sys_close(fd);
        return -1;
    }

    int fmt_found = 0, data_found = 0;
    while (1) {
        chunk_header_t chk;
        n = sys_read(fd, &chk, sizeof(chk));
        if (n < (int)sizeof(chk)) break;

        uint32_t chunk_size = chk.chunk_size;
        if (chk.chunk_id[0] == 'f' && chk.chunk_id[1] == 'm' &&
            chk.chunk_id[2] == 't' && chk.chunk_id[3] == ' ') {
            fmt_chunk_t fmt;
            n = sys_read(fd, &fmt, sizeof(fmt));
            if (n < (int)sizeof(fmt)) break;
            wav.sample_rate = fmt.sample_rate;
            wav.num_channels = fmt.num_channels;
            wav.bits_per_sample = fmt.bits_per_sample;
            fmt_found = 1;
            int skip = chunk_size - sizeof(fmt);
            while (skip > 0) {
                char tmp[64];
                int to_skip = skip < 64 ? skip : 64;
                sys_read(fd, tmp, to_skip);
                skip -= to_skip;
            }
        } else if (chk.chunk_id[0] == 'd' && chk.chunk_id[1] == 'a' &&
                   chk.chunk_id[2] == 't' && chk.chunk_id[3] == 'a') {
            wav.data_offset = sys_lseek(fd, 0, 1);
            wav.data_size = chunk_size;
            wav.data_pos = 0;
            data_found = 1;
            break;
        } else {
            int skip = chunk_size;
            while (skip > 0) {
                char tmp[64];
                int to_skip = skip < 64 ? skip : 64;
                sys_read(fd, tmp, to_skip);
                skip -= to_skip;
            }
        }
    }

    if (!fmt_found || !data_found) {
        sys_close(fd);
        return -1;
    }

    wav.fd = fd;
    wav.has_file = 1;
    int len = strlen(path);
    if (len >= 128) len = 127;
    memcpy(wav.filename, path, len);
    wav.filename[len] = 0;
    return 0;
}

static void wav_close(void) {
    if (wav.has_file) {
        sys_close(wav.fd);
        wav.has_file = 0;
    }
}

static int wav_read_pcm(int16_t *buf, int max_samples) {
    if (!wav.has_file) return 0;
    int bytes_per_sample = wav.bits_per_sample / 8;
    int channels = wav.num_channels;
    int max_bytes = max_samples * bytes_per_sample * channels;
    if (max_bytes > 4096) max_bytes = 4096;

    char raw[4096];
    int n = sys_read(wav.fd, raw, max_bytes);
    if (n <= 0) return 0;

    int out_samples = 0;
    int pos = 0;
    while (pos + bytes_per_sample * channels <= n && out_samples < max_samples) {
        int32_t sample = 0;
        if (bytes_per_sample == 2) {
            sample = *(int16_t *)(raw + pos);
        } else if (bytes_per_sample == 1) {
            sample = ((int)raw[pos] - 128) << 8;
        }
        if (channels > 1) {
            int32_t sample2 = 0;
            if (bytes_per_sample == 2)
                sample2 = *(int16_t *)(raw + pos + 2);
            else
                sample2 = ((int)raw[pos + 1] - 128) << 8;
            sample = (sample + sample2) / 2;
        }
        if (sample < -32768) sample = -32768;
        if (sample > 32767) sample = 32767;
        buf[out_samples++] = (int16_t)sample;
        pos += bytes_per_sample * channels;
    }
    wav.data_pos += pos;
    return out_samples;
}

/* ───── Playback state ───── */

static int playing;
static int paused;
static int playback_eof;
static int volume = 75;

static int16_t pcm_buf[1024];
static int pcm_pending;

/* ───── UI ───── */

static const char *status_bar =
    "  === Code Music [VLC Engine] ===  |  Now Playing  |  Vol:75  |";

static void draw_player(void) {
    puts("");
    puts(status_bar);
    puts("");

    if (wav.has_file) {
        printf("  File: %s\n", wav.filename);
        printf("  Format: %u Hz, %d ch, %d-bit PCM\n",
               wav.sample_rate, wav.num_channels, wav.bits_per_sample);
        printf("  Data size: %u bytes\n", wav.data_size);
        if (wav.data_size > 0) {
            int pct = (int)(wav.data_pos * 100 / wav.data_size);
            if (pct > 100) pct = 100;
            printf("  Progress: [");
            for (int i = 0; i < 40; i++)
                putchar(i < pct * 40 / 100 ? '=' : ' ');
            printf("] %d%%\n", pct);
        }
    } else {
        puts("  No file loaded. Use 'load <file>' to open a WAV file.");
    }
    puts("");

    if (playing && !paused) {
        puts("  |  >> Playing  |");
    } else if (paused) {
        puts("  |  || Paused   |");
    } else {
        puts("  |  [] Stopped  |");
    }

    printf("  Volume: %d%%\n", volume);
    puts("");

    puts("  ----- Commands -----");
    puts("  load <file>  Load a WAV file");
    puts("  play         Start playback");
    puts("  pause        Pause/resume playback");
    puts("  stop         Stop playback");
    puts("  vol N        Set volume (0-100)");
    puts("  help         Show this help");
    puts("  quit         Exit Code Music");
    puts("");
}

/* ───── Command handler ───── */

static char inbuf[256];
static int inpos;

/* ───── Audio tick — called from main loop ───── */

static void audio_tick(void) {
    if (!playing || paused) return;

    if (pcm_pending > 0) {
        if (sys_audio_status() != 0) return;
        sys_audio_play(pcm_buf, pcm_pending);
        pcm_pending = 0;
        return;
    }

    int samples = wav_read_pcm(pcm_buf, 1024);
    if (samples <= 0) {
        if (sys_audio_status() == 0) {
            playing = 0;
            playback_eof = 1;
            puts("  [] Playback finished");
        }
        return;
    }

    pcm_pending = samples;
    if (sys_audio_status() == 0) {
        sys_audio_play(pcm_buf, pcm_pending);
        pcm_pending = 0;
    }
}

static void handle_command(const char *cmd) {
    if (strncmp(cmd, "load", 4) == 0) {
        const char *path = cmd + 4;
        while (*path == ' ') path++;
        if (!*path) {
            puts("  Usage: load <path>");
            return;
        }
        playing = 0;
        paused = 0;
        pcm_pending = 0;
        playback_eof = 0;
        wav_close();
        if (wav_open(path) < 0) {
            printf("  Failed to load '%s'\n", path);
        } else {
            printf("  Loaded: %s\n", wav.filename);
        }
    } else if (strcmp(cmd, "play") == 0) {
        if (!wav.has_file) {
            puts("  No file loaded. Use 'load <file>' first.");
            return;
        }
        if (playing) {
            puts("  Already playing");
            return;
        }
        sys_lseek(wav.fd, wav.data_offset, 0);
        wav.data_pos = 0;
        pcm_pending = 0;
        playback_eof = 0;
        playing = 1;
        paused = 0;
        puts("  >> Playing");
    } else if (strcmp(cmd, "pause") == 0) {
        paused = !paused;
        puts(paused ? "  || Paused" : "  >> Resumed");
    } else if (strcmp(cmd, "stop") == 0) {
        playing = 0;
        paused = 0;
        pcm_pending = 0;
        puts("  [] Stopped");
    } else if (strncmp(cmd, "vol", 3) == 0) {
        const char *v = cmd + 3;
        while (*v == ' ') v++;
        if (*v >= '0' && *v <= '9') {
            volume = 0;
            while (*v >= '0' && *v <= '9') { volume = volume * 10 + (*v - '0'); v++; }
            if (volume > 100) volume = 100;
        }
        printf("  Volume: %d%%\n", volume);
    } else if (strcmp(cmd, "help") == 0) {
        puts("  Commands: load <file>, play, pause, stop, vol N, help, quit");
    } else if (strcmp(cmd, "quit") == 0 || strcmp(cmd, "exit") == 0) {
        playing = 0;
        puts("  Code Music shutting down...");
        sys_sleep(200);
        wav_close();
        sys_exit(0);
    } else if (cmd[0]) {
        printf("  Unknown command: '%s'\n", cmd);
    }
}

int main(int argc, char **argv) {
    volume = 75;
    playing = 0;
    paused = 0;
    pcm_pending = 0;
    playback_eof = 0;
    wav.has_file = 0;

    if (argc > 1) {
        if (wav_open(argv[1]) < 0)
            printf("  Failed to load '%s'\n", argv[1]);
        else
            printf("  Loaded: %s\n", wav.filename);
    }

    draw_player();
    while (1) {
        /* Check for input (non-blocking) */
        int n = sys_read(0, inbuf + inpos, 1);
        if (n > 0) {
            char c = inbuf[inpos];
            if (c == '\n' || c == '\r') {
                inbuf[inpos] = 0;
                puts("");
                handle_command(inbuf);
                inpos = 0;
            } else if (c == '\b' || c == 127) {
                if (inpos > 0) { inpos--; puts("\b \b"); }
            } else if (inpos < 255) {
                inpos++;
                putchar(c);
            }
        }

        audio_tick();

        sys_sleep(50);
    }
    return 0;
}
#include "adb.h"
#ifdef __aarch64__
#include "../arch/arm64/serial.h"
#else
#include "../arch/x86_64/serial.h"
#endif
#include "kprintf.h"
#include "string.h"
#include "../drivers/timer.h"
#include "shell.h"
#include "sched.h"

/* ADB Serial Transport — minimal Android Debug Bridge over serial */

#define ADB_VERSION     0x01000001
#define ADB_MAX_DATA    2048
#define A_SYNC          0x434E5953
#define A_CNXN          0x4E584E43
#define A_OPEN          0x4E45504F
#define A_OKAY          0x59414B4F
#define A_CLSE          0x45534C43
#define A_WRTE          0x45545257
#define A_AUTH          0x48545541

typedef struct {
    uint32_t command;
    uint32_t arg0;
    uint32_t arg1;
    uint32_t data_length;
    uint32_t data_crc32;
    uint32_t magic;
} __attribute__((packed)) adb_packet_t;

static uint32_t crc32_tab[256];
static int crc32_ready;
static int adb_connected;

static void init_crc32(void) {
    for (uint32_t i = 0; i < 256; i++) {
        uint32_t crc = i;
        for (int j = 0; j < 8; j++)
            crc = (crc >> 1) ^ (crc & 1 ? 0xEDB88320 : 0);
        crc32_tab[i] = crc;
    }
    crc32_ready = 1;
}

static uint32_t calc_crc32(const uint8_t *data, uint32_t len) {
    if (!crc32_ready) init_crc32();
    uint32_t crc = 0xFFFFFFFF;
    for (uint32_t i = 0; i < len; i++)
        crc = crc32_tab[(crc ^ data[i]) & 0xFF] ^ (crc >> 8);
    return crc ^ 0xFFFFFFFF;
}

static void send_packet(uint32_t cmd, uint32_t a0, uint32_t a1,
                        const void *data, uint32_t dlen) {
    adb_packet_t pkt;
    pkt.command = cmd;
    pkt.arg0 = a0;
    pkt.arg1 = a1;
    pkt.data_length = dlen;
    pkt.data_crc32 = (data && dlen) ? calc_crc32(data, dlen) : 0;
    pkt.magic = cmd ^ 0xFFFFFFFF;
    uint8_t *hdr = (uint8_t *)&pkt;
    for (int i = 0; i < 24; i++) serial_putchar(hdr[i]);
    if (data && dlen) {
        uint8_t *dp = (uint8_t *)data;
        for (uint32_t i = 0; i < dlen; i++) serial_putchar(dp[i]);
    }
}

static int recv_header(adb_packet_t *pkt) {
    uint8_t *hdr = (uint8_t *)pkt;
    for (int i = 0; i < 24; i++) {
        int tries = 50000;
        while (tries-- && !serial_available()) asm volatile("pause");
        if (tries < 0) return -1;
        hdr[i] = serial_readchar();
    }
    if (pkt->magic != (pkt->command ^ 0xFFFFFFFF)) return -2;
    if (pkt->data_length > ADB_MAX_DATA) return -3;
    return 0;
}

static int recv_data(uint8_t *buf, uint32_t len, uint32_t expected_crc) {
    for (uint32_t i = 0; i < len; i++) {
        int tries = 50000;
        while (tries-- && !serial_available()) asm volatile("pause");
        if (tries < 0) return -1;
        buf[i] = serial_readchar();
    }
    if (len && calc_crc32(buf, len) != expected_crc) return -4;
    return 0;
}

void adb_init(void) {
    kprintf("adb: ADB daemon initializing on serial\n");
    init_crc32();
    adb_connected = 0;
    kprintf("adb: ready\n");
}

void adb_poll(void) {
    adb_packet_t pkt;
    static uint8_t buf[ADB_MAX_DATA];

    if (recv_header(&pkt) < 0) return;

    switch (pkt.command) {
    case A_CNXN: {
        if (pkt.data_length > 0 && recv_data(buf, pkt.data_length, pkt.data_crc32) < 0)
            return;
        buf[pkt.data_length < ADB_MAX_DATA ? pkt.data_length : ADB_MAX_DATA - 1] = 0;
        kprintf("adb: connect from host (version=%d maxdata=%d id='%s')\n",
                pkt.arg0, pkt.arg1, buf);
        static const char banner[] = "device::ro.product.name=CodeOS;ro.product.model=CodeOS;";
        send_packet(A_CNXN, ADB_VERSION, ADB_MAX_DATA, banner, sizeof(banner) - 1);
        adb_connected = 1;
        break;
    }
    case A_OPEN: {
        if (pkt.data_length > 0 && recv_data(buf, pkt.data_length, pkt.data_crc32) < 0)
            return;
        buf[pkt.data_length < ADB_MAX_DATA ? pkt.data_length : ADB_MAX_DATA - 1] = 0;
        kprintf("adb: open local_id=%d dest='%s'\n", pkt.arg0, buf);
        uint32_t remote_id = pkt.arg0;

        if (strncmp((const char *)buf, "shell:", 6) == 0) {
            char *cmd = (char *)buf + 6;
            while (*cmd == ' ') cmd++;
            if (*cmd) {
                /* Execute command and capture output */
                kprintf_capture_begin();
                shell_execute(cmd);
                int has_output = kprintf_capture_end();
                const char *captured = kprintf_capture_get();
                uint32_t out_len = has_output ? (uint32_t)strlen(captured) : 0;
                if (out_len > ADB_MAX_DATA - 1) out_len = ADB_MAX_DATA - 1;
                send_packet(A_OKAY, remote_id, 0, NULL, 0);
                if (out_len)
                    send_packet(A_WRTE, remote_id, 0, captured, out_len);
                send_packet(A_CLSE, remote_id, 0, NULL, 0);
            } else {
                send_packet(A_CLSE, remote_id, 0, NULL, 0);
            }
        } else {
            send_packet(A_CLSE, remote_id, 0, NULL, 0);
        }
        break;
    }
    case A_WRTE: {
        uint32_t remote_id = pkt.arg0;
        if (pkt.data_length > 0 && recv_data(buf, pkt.data_length, pkt.data_crc32) < 0)
            return;
        buf[pkt.data_length < ADB_MAX_DATA ? pkt.data_length : ADB_MAX_DATA - 1] = 0;
        send_packet(A_OKAY, remote_id, 0, NULL, 0);
        break;
    }
    case A_CLSE:
        kprintf("adb: close\n");
        break;
    default:
        break;
    }
}

#include "ps2.h"
#include "../arch/x86_64/io.h"

#define PS2_DATA_PORT    0x60
#define PS2_STATUS_PORT  0x64
#define PS2_COMMAND_PORT 0x64

#define PS2_STATUS_OUTPUT_FULL 0x01
#define PS2_STATUS_INPUT_FULL  0x02

bool ps2_wait_output_ready(uint32_t timeout) {
    while (timeout--) {
        if (!(inb(PS2_STATUS_PORT) & PS2_STATUS_INPUT_FULL)) {
            return true;
        }
    }
    return false;
}

bool ps2_wait_input_ready(uint32_t timeout) {
    while (timeout--) {
        if (inb(PS2_STATUS_PORT) & PS2_STATUS_OUTPUT_FULL) {
            return true;
        }
    }
    return false;
}

bool ps2_send_command(uint16_t port, uint8_t command) {
    if (!ps2_wait_output_ready(100000)) return false;
    outb(port, command);
    return true;
}

bool ps2_send_data(uint8_t data) {
    if (!ps2_wait_output_ready(100000)) return false;
    outb(PS2_DATA_PORT, data);
    return true;
}

int ps2_read_data_ex(uint8_t *out, uint32_t timeout) {
    if (!ps2_wait_input_ready(timeout)) return -1;
    *out = inb(PS2_DATA_PORT);
    return 0;
}

uint8_t ps2_read_data(void) {
    uint8_t val = 0;
    ps2_read_data_ex(&val, 100000);
    return val;
}

void ps2_flush(void) {
    while (inb(PS2_STATUS_PORT) & PS2_STATUS_OUTPUT_FULL) {
        (void)inb(PS2_DATA_PORT);
    }
}

void ps2_init(void) {
    /* Drain any stale bytes from both PS/2 ports before configuration. */
    ps2_flush();

    /* Enable the auxiliary (mouse) port. 0xAE enables the keyboard port;
     * keep both ports active for polling-based drivers. */
    ps2_send_command(PS2_COMMAND_PORT, 0xAE);
    ps2_send_command(PS2_COMMAND_PORT, 0xA8);

    /* Read controller config, disable IRQ1/IRQ12 (bits 0,1) for polling
     * (keyboard/mouse are polled by input_poll/mouse_poll; no IRQ handlers
     * are registered for PS/2, so leaving IRQs enabled can cause unhandled
     * interrupts / lost data). Enable translation (bit 6) is preserved. */
    ps2_send_command(PS2_COMMAND_PORT, 0x20);
    if (ps2_wait_input_ready(100000)) {
        uint8_t status = ps2_read_data();
        status &= (uint8_t)~0x03; /* no IRQ1, no IRQ12 — polling mode */
        ps2_send_command(PS2_COMMAND_PORT, 0x60);
        ps2_send_data(status);
    }

    ps2_flush();
}

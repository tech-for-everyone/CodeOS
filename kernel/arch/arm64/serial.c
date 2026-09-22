#include "serial.h"
#include "io.h"

/* PL011 UART on QEMU ARM64 virt platform at 0x09000000 */
#define UART_BASE       0x09000000
#define UART_DR         (UART_BASE + 0x000)
#define UART_FR         (UART_BASE + 0x018)
#define UART_IBRD       (UART_BASE + 0x024)
#define UART_FBRD       (UART_BASE + 0x028)
#define UART_LCR_H      (UART_BASE + 0x02C)
#define UART_CR         (UART_BASE + 0x030)
#define UART_IMSC       (UART_BASE + 0x038)

#define FR_TXFF         (1 << 5)
#define FR_RXFE         (1 << 4)

void serial_init(void) {
    mmio_write32(UART_CR, 0x0);
    mmio_write32(UART_IBRD, 13);
    mmio_write32(UART_FBRD, 1);
    mmio_write32(UART_LCR_H, (0x3 << 5) | (1 << 4));
    mmio_write32(UART_IMSC, 0x0);
    mmio_write32(UART_CR, (1 << 0) | (1 << 8) | (1 << 9));
}

void serial_putchar(char c) {
    while (mmio_read32(UART_FR) & FR_TXFF)
        __asm__ volatile("yield");
    mmio_write32(UART_DR, (uint8_t)c);
    if (c == '\n')
        serial_putchar('\r');
}

int serial_available(void) {
    return (mmio_read32(UART_FR) & FR_RXFE) ? 0 : 1;
}

char serial_readchar(void) {
    while (mmio_read32(UART_FR) & FR_RXFE)
        __asm__ volatile("yield");
    return (char)(mmio_read32(UART_DR) & 0xFF);
}

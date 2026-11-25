/* rtl8139_rx_test.c — host test for the real RTL8139 receive ring logic.
 *
 * The driver source is included verbatim (as `rtl8139_under_test.c`, a copy
 * made by the Makefile).  The x86 port-I/O header is replaced with a small
 * in-memory I/O space, so CAPR/CBR reads and writes are observable; the RX
 * ring arithmetic itself is the real source.
 *
 * The Makefile also builds a reverted copy and requires that this test FAIL
 * against it.  Build/run: make rtl8139-rx-check
 */
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ── Host stand-ins ── */
void kprintf(const char *fmt, ...) { (void)fmt; }
static inline uint64_t phys_to_virt(uint64_t p) { return p; }
static inline uint64_t virt_to_phys(uint64_t v) { return v; }
uint64_t pmm_alloc_pages(size_t n) { (void)n; return (uint64_t)(uintptr_t)calloc(1, 4096); }

int pci_find_device(uint16_t v, uint16_t d, uint8_t *b, uint8_t *s, uint8_t *f) {
    (void)v; (void)d; (void)b; (void)s; (void)f; return 0;
}
uint32_t pci_config_read(uint8_t b, uint8_t s, uint8_t f, uint8_t o) {
    (void)b; (void)s; (void)f; (void)o; return 0;
}
void pci_config_write(uint8_t b, uint8_t s, uint8_t f, uint8_t o, uint32_t v) {
    (void)b; (void)s; (void)f; (void)o; (void)v;
}

#define PAGE_SIZE 0x1000UL

static uint8_t io_space[0x100];
static inline uint8_t inb(uint16_t p) { return io_space[p]; }
static inline uint16_t inw(uint16_t p) {
    return (uint16_t)(io_space[p] | ((uint16_t)io_space[p + 1] << 8));
}
static inline uint32_t inl(uint16_t p) {
    return (uint32_t)io_space[p] | ((uint32_t)io_space[p + 1] << 8) |
           ((uint32_t)io_space[p + 2] << 16) | ((uint32_t)io_space[p + 3] << 24);
}
static inline void outb(uint16_t p, uint8_t v) { io_space[p] = v; }
static inline void outw(uint16_t p, uint16_t v) {
    io_space[p] = (uint8_t)v; io_space[p + 1] = (uint8_t)(v >> 8);
}
static inline void outl(uint16_t p, uint32_t v) {
    io_space[p] = (uint8_t)v;
    io_space[p + 1] = (uint8_t)(v >> 8);
    io_space[p + 2] = (uint8_t)(v >> 16);
    io_space[p + 3] = (uint8_t)(v >> 24);
}

#include "rtl8139_under_test.c"

static int failures;
#define CHECK(cond, what)                                                      \
    do {                                                                       \
        if (cond) { printf("ok  : %s\n", what); }                              \
        else { printf("FAIL: %s\n", what); failures++; }                       \
    } while (0)

static void put16(int off, uint16_t v) {
    rx_ring[off % RTL_RX_BUF_SIZE] = (uint8_t)v;
    rx_ring[(off + 1) % RTL_RX_BUF_SIZE] = (uint8_t)(v >> 8);
}

static void put_packet(int off, uint16_t status, uint16_t len,
                       const uint8_t *data, int n) {
    put16(off, status);
    put16(off + 2, len);
    for (int i = 0; i < n; i++)
        rx_ring[(off + 4 + i) % RTL_RX_BUF_SIZE] = data[i];
}

static void reset_ring(void) {
    memset(rx_ring, 0, RTL_RX_BUF_SIZE);
    rx_offset = 0;
    rtl_ok = 1;
    rtl_io_base = 0;
    memset(io_space, 0, sizeof(io_space));
    outw(RTL_REG_CBR, 4095); /* hardware reports data available */
}

int main(void) {
    rx_ring = calloc(1, RTL_RX_BUF_SIZE);

    /* ── A caller buffer smaller than the frame must still consume it all ── */
    reset_ring();
    uint8_t payload[100];
    for (int i = 0; i < 100; i++) payload[i] = (uint8_t)(i * 7 + 1);
    put_packet(0, RTL_RX_OK, 104, payload, 100);

    uint8_t small[16];
    int r = rtl8139_recv(small, 16);
    CHECK(r == 16, "short read returns only the bytes that fit");
    CHECK(memcmp(small, payload, 16) == 0, "short read copies the first bytes of the frame");
    /* Old code clamped rx_len to max_len before advancing, leaving the
     * ring cursor at 20 -- inside the frame it had just "consumed". */
    CHECK(rx_offset == 104,
          "whole frame is consumed even when the caller's buffer is small");
    CHECK(inw(RTL_REG_CAPR) == 88, "CAPR follows the full frame, not the clamped copy");

    /* ── The next read must see the next packet, not the old one again ── */
    uint8_t second[4] = { 0xAB, 0xCD, 0xEF, 0x11 };
    put_packet(104, RTL_RX_OK, 8, second, 4);
    uint8_t buf[64];
    r = rtl8139_recv(buf, 64);
    CHECK(r == 4, "next read sees the next frame");
    CHECK(memcmp(buf, second, 4) == 0, "next frame's payload is intact");
    CHECK(rx_offset == 112, "second frame advances the cursor to 112");

    /* ── Ring wrap: header and payload straddle the end of the buffer ── */
    reset_ring();
    rx_offset = RTL_RX_BUF_SIZE - 8;
    uint8_t wrap[12];
    for (int i = 0; i < 12; i++) wrap[i] = (uint8_t)(0x80 + i);
    put_packet(RTL_RX_BUF_SIZE - 8, RTL_RX_OK, 16, wrap, 12);
    r = rtl8139_recv(buf, 64);
    CHECK(r == 12, "wrapped frame copies all of its bytes");
    CHECK(memcmp(buf, wrap, 12) == 0, "wrapped payload is reassembled in order");
    CHECK(rx_offset == 8, "wrapped frame wraps the cursor modulo the ring size");
    CHECK(inw(RTL_REG_CAPR) == 0, "CAPR is zeroed when the cursor is inside the guard gap");

    /* ── Error frames: length must be masked before advancing ── */
    reset_ring();
    put_packet(0, 0, 0x8014, NULL, 0); /* no RTL_RX_OK, high status bits in len */
    r = rtl8139_recv(buf, 64);
    CHECK(r == -1, "frame without the OK bit is rejected");
    CHECK(rx_offset == 24,
          "error frame advance masks the length and wraps (was a huge offset)");

    if (failures) {
        printf("rtl8139-rx: %d FAILURES\n", failures);
        return 1;
    }
    printf("rtl8139-rx: all checks passed\n");
    return 0;
}

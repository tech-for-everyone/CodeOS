/* rtl8169_desc_test.c — host test for the real RTL8169 register map and
 * descriptor flags.
 *
 * The driver source is included verbatim (as `rtl8169_under_test.c`, a copy
 * made by the Makefile).  MMIO is a small in-memory region reached through
 * the BAR the probe discovers, so every register write is observable.  The
 * Makefile also builds a reverted copy and requires that this test FAIL
 * against it.  Build/run: make rtl8169-desc-check
 */
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>

/* ── Host stand-ins ── */
void kprintf(const char *fmt, ...) { (void)fmt; }

#define FAKE_BAR_PHYS 0x10000000UL
#define FAKE_IO_BAR   0x00100001UL /* I/O BAR: must not be used as MMIO */
static uint8_t *mmio;
static inline uint64_t phys_to_virt(uint64_t p) {
    return p == FAKE_BAR_PHYS ? (uint64_t)(uintptr_t)mmio : p;
}
static inline uint64_t virt_to_phys(uint64_t v) { return v; }

int pci_find_device(uint16_t v, uint16_t d, uint8_t *b, uint8_t *s, uint8_t *f) {
    (void)v; (void)d; *b = *s = *f = 0; return 1;
}
uint32_t pci_config_read(uint8_t b, uint8_t s, uint8_t f, uint8_t o) {
    (void)b; (void)s; (void)f;
    if (o == 0x10) return FAKE_IO_BAR;  /* BAR0 is I/O */
    if (o == 0x14) return FAKE_BAR_PHYS; /* BAR1 is the 32-bit memory BAR */
    return 0;
}
void pci_config_write(uint8_t b, uint8_t s, uint8_t f, uint8_t o, uint32_t v) {
    (void)b; (void)s; (void)f; (void)o; (void)v;
}

#include "rtl8169_under_test.c"

static int failures;
#define CHECK(cond, what)                                                      \
    do {                                                                       \
        if (cond) { printf("ok  : %s\n", what); }                              \
        else { printf("FAIL: %s\n", what); failures++; }                       \
    } while (0)

static uint16_t mmio16(int off) {
    return (uint16_t)(mmio[off] | ((uint16_t)mmio[off + 1] << 8));
}

static uint32_t mmio32(int off) {
    return (uint32_t)mmio[off] | ((uint32_t)mmio[off + 1] << 8) |
           ((uint32_t)mmio[off + 2] << 16) | ((uint32_t)mmio[off + 3] << 24);
}

int main(void) {
    mmio = malloc(0x100);
    memset(mmio, 0xFF, 0x100); /* untouched registers stay at a sentinel */
    mmio[0x37] = 0; /* reset bit clears immediately */

    /* Give the pre-fix code's bogus BAR-derived address somewhere safe to
     * write, so the reverted control fails assertions instead of faulting. */
    void *bogus = mmap((void *)(FAKE_IO_BAR & ~0xFUL), 0x1000,
                       PROT_READ | PROT_WRITE,
                       MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, -1, 0);
    if (bogus == MAP_FAILED) { perror("mmap"); return 2; }

    CHECK(rtl8169_probe() == 0, "probe succeeds with BAR0=I/O, BAR1=MMIO");
    CHECK(gig_mmio_base == (uintptr_t)mmio,
          "probe picks the first 32-bit memory BAR, not BAR0");

    /* Register map: RTL8168/8169 values, not the RTL8139 offsets. */
    CHECK(mmio[0x50] == 0xC0,
          "config unlock key is written at Cfg9346 (0x50)");
    CHECK(mmio16(0xE0) == 0,
          "CPlusCmd is written at 0xE0");
    CHECK(mmio16(0x88) == 0xFFFF,
          "CPlusCmd is not written at the RTL8139-era offset 0x88");
    CHECK(mmio32(0x40) == 0x03000700,
          "TxConfig is written at 0x40");
    CHECK(mmio32(0x44) ==
              (RTL_GIG_RCR_AAP | RTL_GIG_RCR_APM | RTL_GIG_RCR_AM | RTL_GIG_RCR_AB),
          "RxConfig is written at 0x44");
    CHECK(mmio32(0xE4) == (uint32_t)(uintptr_t)rx_descs,
          "RX descriptor pointer is written at 0xE4, not 0x40");
    CHECK(mmio32(0x20) == (uint32_t)(uintptr_t)tx_descs,
          "TX descriptor pointer is written at 0x20");

    /* Descriptors handed to the NIC must advertise the buffer capacity. */
    CHECK(rx_descs[0].status == (uint32_t)(RTL_GIG_RX_OWN | 2048u),
          "RX descriptor carries OWN plus the buffer-size field");
    CHECK(rx_descs[RTL_GIG_NUM_RX - 1].status ==
              (uint32_t)(RTL_GIG_RX_OWN | 2048u | RTL_GIG_RX_EOR),
          "last RX descriptor keeps its End-of-Ring bit");
    CHECK(tx_descs[RTL_GIG_NUM_TX - 1].status == RTL_GIG_TX_EOR,
          "last TX descriptor starts with its End-of-Ring bit");

    /* Receiving on the last descriptor must restore both fields. */
    rx_cur = RTL_GIG_NUM_RX - 1;
    rx_descs[rx_cur].status = 0x00000014; /* 20 bytes incl. CRC, no OWN */
    memset(rx_bufs[rx_cur], 0x5A, 16);
    uint8_t buf[64];
    int r = rtl8169_recv(buf, 64);
    CHECK(r == 16, "receive strips the 4-byte CRC");
    CHECK(buf[0] == 0x5A && buf[15] == 0x5A, "received payload is copied");
    CHECK(rx_descs[RTL_GIG_NUM_RX - 1].status ==
              (uint32_t)(RTL_GIG_RX_OWN | 2048u | RTL_GIG_RX_EOR),
          "recycled RX descriptor restores size and End-of-Ring");

    /* Sending on the last descriptor must not erase its EOR bit.  The fake
     * NIC never clears OWN, so send times out; the status is still written
     * and is what the controller would have consumed. */
    tx_cur = RTL_GIG_NUM_TX - 1;
    tx_descs[tx_cur].status = 0;
    uint8_t frame[32];
    memset(frame, 0xC3, sizeof(frame));
    (void)rtl8169_send(frame, 32);
    CHECK((tx_descs[RTL_GIG_NUM_TX - 1].status & RTL_GIG_TX_EOR) != 0,
          "reused TX descriptor keeps its End-of-Ring bit");
    CHECK((tx_descs[RTL_GIG_NUM_TX - 1].status & RTL_GIG_TX_OWN) != 0 &&
              (tx_descs[RTL_GIG_NUM_TX - 1].status & 0x1FFF) == 32,
          "reused TX descriptor still carries OWN and the length");

    if (failures) {
        printf("rtl8169-desc: %d FAILURES\n", failures);
        return 1;
    }
    printf("rtl8169-desc: all checks passed\n");
    return 0;
}

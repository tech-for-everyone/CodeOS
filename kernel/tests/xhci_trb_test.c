/* xhci_trb_test.c — host test for the real kernel xHCI TRB encoding.
 *
 * The driver source is included verbatim (as `usb_xhci_under_test.c`, a copy
 * made by the Makefile).  Only the low-level headers are replaced with empty
 * files: the x86 port I/O they declare cannot compile on a non-x86 host, and
 * none of it is used by the transfer paths under test.  Everything else --
 * TRB layouts, the ring walker, the control/bulk transfer builders and the
 * mouse report decode -- is the real source.
 *
 * The Makefile also builds a deliberately reverted copy and requires that
 * this test FAIL against it, so a green run means the assertions are
 * sensitive to the bugs rather than merely compatible with the fix.
 *
 * Build/run: make xhci-trb-check
 */
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ── Host stand-ins for the kernel functions the driver calls ── */
void kprintf(const char *fmt, ...) { (void)fmt; }
static inline uint64_t phys_to_virt(uint64_t p) { return p; }
static inline uint64_t virt_to_phys(uint64_t v) { return v; }
uint64_t pmm_alloc_page(void) { return (uint64_t)(uintptr_t)calloc(1, 4096); }

int pci_find_device(uint16_t v, uint16_t d, uint8_t *b, uint8_t *s, uint8_t *f) {
    (void)v; (void)d; (void)b; (void)s; (void)f; return 0;
}
int pci_find_class_idx(uint8_t c, uint8_t s, int i, uint8_t *b, uint8_t *sl, uint8_t *f) {
    (void)c; (void)s; (void)i; (void)b; (void)sl; (void)f; return 0;
}
uint32_t pci_config_read(uint8_t b, uint8_t s, uint8_t f, uint8_t o) {
    (void)b; (void)s; (void)f; (void)o; return 0;
}
void pci_config_write(uint8_t b, uint8_t s, uint8_t f, uint8_t o, uint32_t v) {
    (void)b; (void)s; (void)f; (void)o; (void)v;
}

int shift_down, ctrl_down, alt_down, super_down;

/* The driver itself */
#include "usb_xhci_under_test.c"

static int failures;
#define CHECK(cond, what)                                                      \
    do {                                                                       \
        if (cond) {                                                            \
            printf("ok  : %s\n", what);                                        \
        } else {                                                               \
            printf("FAIL: %s\n", what);                                        \
            failures++;                                                        \
        }                                                                      \
    } while (0)

static struct xhci_trb *ring;
static struct xhci_trb *cmd_ring_host;
static struct xhci_trb *event_ring_host;

static void harness_init(void) {
    memset(dev_states, 0, sizeof(dev_states));
    num_devs = 1;
    dev_states[0].slot_id = 1;
    xhci_ok = 1;
    xhci_mouse_dev_idx = 0;

    ring = calloc(TRB_RING_SIZE, sizeof(*ring));
    dev_transfer_rings[1] = ring;
    dev_transfer_paddrs[1] = 0x0000000001234000ULL;
    dev_transfer_cycle[1] = 1;
    dev_transfer_idx[1] = 0;

    xfer_buf = calloc(1, 4096);
    xfer_buf_paddr = 0x0000000000500000ULL;

    cmd_ring_host = calloc(TRB_RING_SIZE, sizeof(*cmd_ring_host));
    cmd_ring = cmd_ring_host;
    cmd_ring_paddr = 0x0000000000777000ULL;
    cmd_cycle = 1;
    cmd_enq_idx = 0;

    event_ring_host = calloc(EVENT_RING_SIZE, sizeof(*event_ring_host));
    event_ring = event_ring_host;
    event_cycle = 1;
    event_deq_idx = 0;
    for (int i = 0; i < EVENT_RING_SIZE; i++) {
        event_ring_host[i].params[3] = (32u << 10) | 1u; /* Transfer Event, cycle=1 */
        event_ring_host[i].params[2] = 1u << 24;         /* CC=1 */
    }

    xhci_doorbell_base = (uintptr_t)calloc(1, 64);
    xhci_runtime_base = (uintptr_t)calloc(1, 256);
    xhci_op_base = (uintptr_t)calloc(1, 256);
}

static void reset_ring(void) {
    memset(ring, 0, TRB_RING_SIZE * sizeof(*ring));
    dev_transfer_cycle[1] = 1;
    dev_transfer_idx[1] = 0;
    event_deq_idx = 0;
}

int main(void) {
    harness_init();
    struct usb_dev *dev = &dev_states[0].dev;
    uint8_t data[32];
#define TRB_TYPE(dw3) (((dw3) >> 10) & 0x3F)

    /* ── Control IN with an 18-byte descriptor ── */
    memset(data, 0, sizeof(data));
    CHECK(xhci_control_transfer(dev, 1, 0x80, 6, 0x0100, 0, 18, data) == 0,
          "control IN completes");
    CHECK(ring[0].params[0] == (0x80u | (6u << 8) | (0x0100u << 16)),
          "setup DW0 holds bmRequestType/bRequest/wValue verbatim");
    /* Old code OR'd (8<<16) into s1, silently turning wLength=18 into 8. */
    CHECK(ring[0].params[1] == (18u << 16),
          "setup DW1 holds wLength=18, not a hardcoded 8");
    CHECK((ring[0].params[3] & 0x30000u) == (3u << 16),
          "setup TRT=3 (IN data stage)");
    CHECK((ring[0].params[3] & TRB_IDT) != 0,
          "setup uses IDT (packet inline in DW0/DW1)");

    CHECK(ring[1].params[0] == (uint32_t)xfer_buf_paddr,
          "data TRB DW0 holds low 32 bits of bounce buffer");
    /* Old code put the length in DW1 bits 31:17 and left DW2 zero. */
    CHECK(ring[1].params[1] == (uint32_t)(xfer_buf_paddr >> 32),
          "data TRB DW1 holds only the high address, no length bits");
    CHECK(ring[1].params[2] == 18,
          "data TRB DW2 holds the transfer length");
    CHECK((ring[1].params[3] & TRB_CHAIN) == 0,
          "single data TRB is not chained");
    CHECK((ring[1].params[3] & (1u << 16)) != 0,
          "data TRB direction bit is IN");
    CHECK(TRB_TYPE(ring[2].params[3]) == TRB_STATUS &&
              (ring[2].params[3] & (1u << 16)) == 0,
          "status TRB is OUT after an IN data stage");
    CHECK(TRB_TYPE(ring[0].params[3]) == TRB_SETUP,
          "setup TRB has the Setup type");

    /* ── Control OUT with no data stage ── */
    reset_ring();
    CHECK(xhci_control_transfer(dev, 0, 0x00, 9, 1, 0, 0, NULL) == 0,
          "control OUT with no data stage completes");
    CHECK((ring[0].params[3] & 0x30000u) == 0,
          "setup TRT=0 when there is no data stage");
    CHECK(dev_transfer_idx[1] == 2,
          "no-data transfer emits exactly setup + status TRBs");
    CHECK((ring[1].params[3] & (1u << 16)) != 0,
          "status TRB is IN after a no-data OUT setup");

    /* ── Bulk OUT: length must be in DW2 too ── */
    reset_ring();
    dev->ep_out_addr = 2;
    uint8_t bulk[64];
    memset(bulk, 0x5A, sizeof(bulk));
    CHECK(xhci_bulk_transfer(dev, 0, 0, bulk, 100) == 100,
          "bulk OUT transfer completes and reports the byte count");
    CHECK(ring[0].params[2] == 100,
          "normal TRB DW2 holds the bulk length");
    CHECK(ring[0].params[1] == (uint32_t)(xfer_buf_paddr >> 32),
          "normal TRB DW1 holds only the high address");
    CHECK(TRB_TYPE(ring[0].params[3]) == TRB_NORMAL,
          "bulk uses a Normal TRB");

    /* ── Link TRB cycle on the command ring ── */
    cmd_enq_idx = TRB_RING_SIZE - 2;
    cmd_cycle = 0;
    cmd_push(0, 0, 0, 0);
    CHECK(cmd_enq_idx == 0 && cmd_cycle == 1,
          "command ring wraps and toggles its producer cycle");
    CHECK(TRB_TYPE(cmd_ring_host[TRB_RING_SIZE - 1].params[3]) == TRB_LINK,
          "command ring actually writes a Link TRB in the last slot");
    CHECK((cmd_ring_host[TRB_RING_SIZE - 1].params[3] & 1u) == 0,
          "command Link TRB cycle matches the outgoing cycle (0)");
    CHECK((cmd_ring_host[TRB_RING_SIZE - 1].params[3] & TRB_TOGGLE) != 0,
          "command Link TRB sets TC so the controller toggles its cycle");
    CHECK((cmd_ring_host[TRB_RING_SIZE - 1].params[3] & TRB_CHAIN) == 0,
          "command Link TRB has no bogus CHAIN bit");

    /* ── Link TRB cycle on a transfer ring ── */
    reset_ring();
    dev_transfer_idx[1] = TRB_RING_SIZE - 2;
    dev_transfer_cycle[1] = 0;
    tpush(1, 0, 0, 0, 0);
    CHECK(dev_transfer_idx[1] == 0 && dev_transfer_cycle[1] == 1,
          "transfer ring wraps and toggles its producer cycle");
    CHECK(TRB_TYPE(ring[TRB_RING_SIZE - 1].params[3]) == TRB_LINK,
          "transfer ring actually writes a Link TRB in the last slot");
    CHECK((ring[TRB_RING_SIZE - 1].params[3] & 1u) == 0,
          "transfer Link TRB cycle matches the outgoing cycle (0)");
    CHECK((ring[TRB_RING_SIZE - 1].params[3] & TRB_TOGGLE) != 0,
          "transfer Link TRB sets TC");

    /* ── Mouse wheel: the return value is 0 on success, not a byte count ── */
    reset_ring();
    memset(xfer_buf, 0, sizeof(xfer_buf) > 8 ? 8 : 1);
    xfer_buf[0] = 0x01;   /* left button */
    xfer_buf[1] = 0x10;   /* dx */
    xfer_buf[2] = (uint8_t)-5;
    xfer_buf[3] = (uint8_t)-3; /* wheel */
    int dx = 0, dy = 0, buttons = 0, wheel = 0;
    CHECK(xhci_mouse_poll(&dx, &dy, &buttons, &wheel) == 1,
          "mouse poll reports a report");
    CHECK(dx == 16 && dy == -5 && buttons == MOUSE_LEFT,
          "mouse deltas and buttons decode from the boot report");
    CHECK(wheel == -3,
          "wheel comes from report byte 3 even though control transfer returns 0");

    if (failures) {
        printf("xhci-trb: %d FAILURES\n", failures);
        return 1;
    }
    printf("xhci-trb: all checks passed\n");
    return 0;
}

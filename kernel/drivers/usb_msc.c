#include "usb_msc.h"
#include "usb_ehci.h"
#include "usb.h"
#include "../kernel/kprintf.h"
#include "../kernel/string.h"
#include "../kernel/vmm.h"
#include "../kernel/pmm.h"

/* ─── SCSI / BBB constants ─── */

#define SCSI_CMD_TEST_READY     0x00
#define SCSI_CMD_REQUEST_SENSE  0x03
#define SCSI_CMD_INQUIRY        0x12
#define SCSI_CMD_MODE_SENSE_6   0x1A
#define SCSI_CMD_READ_CAP_10    0x25
#define SCSI_CMD_READ_10        0x28
#define SCSI_CMD_WRITE_10       0x2A
#define SCSI_CMD_PREVENT_ALLOW  0x1E
#define SCSI_CMD_MODE_SENSE_10  0x5A

#define BBB_CBW_SIGNATURE  0x43425355
#define BBB_CSW_SIGNATURE  0x53425355
#define CBW_LEN  31
#define CSW_LEN  13

/* CBW direction */
#define CBW_DIR_OUT  0x00
#define CBW_DIR_IN   0x80

/* CSW status */
#define CSW_PASS  0
#define CSW_FAIL  1
#define CSW_ERR   2

#pragma pack(push, 1)
struct usb_msc_cbw {
    uint32_t dCBWSignature;
    uint32_t dCBWTag;
    uint32_t dCBWDataTransferLength;
    uint8_t  bmCBWFlags;
    uint8_t  bCBWLUN;
    uint8_t  bCBWCBLength;
    uint8_t  CBWCB[16];
};

struct usb_msc_csw {
    uint32_t dCSWSignature;
    uint32_t dCSWTag;
    uint32_t dCSWDataResidue;
    uint8_t  bCSWStatus;
};

struct usb_msc_inquiry_data {
    uint8_t  peripheral;
    uint8_t  removable;
    uint8_t  version;
    uint8_t  response_format;
    uint8_t  additional_length;
    uint8_t  reserved[3];
    char     vendor[8];
    char     product[16];
    char     revision[4];
};
#pragma pack(pop)

/* ─── State ─── */

static struct usb_dev *msc_dev;
static int msc_ok;
static uint32_t msc_sector_count;
static uint32_t msc_sector_size;
static int msc_bulk_in;
static int msc_bulk_out;
static uint8_t msc_lun;

/* DMA-safe bounce buffers (page-aligned) */
static uint8_t *msc_buf;
static uint64_t msc_buf_paddr;

static struct usb_msc_cbw cbw;
static struct usb_msc_csw csw;

/* ─── Helpers ─── */

static int xfer_in(void *data, int len) {
    return ehci_bulk_transfer(msc_dev, msc_bulk_in, 1, data, len);
}

static int xfer_out(const void *data, int len) {
    return ehci_bulk_transfer(msc_dev, msc_bulk_out, 0, (void *)data, len);
}

static int send_cbw(int dir_in, int data_len, const uint8_t *cmd, int cmd_len) {
    memset(&cbw, 0, sizeof(cbw));
    cbw.dCBWSignature = BBB_CBW_SIGNATURE;
    cbw.dCBWTag = 0xDEADBEEF;
    cbw.dCBWDataTransferLength = data_len;
    cbw.bmCBWFlags = dir_in ? CBW_DIR_IN : CBW_DIR_OUT;
    cbw.bCBWLUN = msc_lun;
    cbw.bCBWCBLength = cmd_len;
    memcpy(cbw.CBWCB, cmd, cmd_len);

    return xfer_out(&cbw, CBW_LEN);
}

static int recv_csw(void) {
    memset(&csw, 0, sizeof(csw));
    int r = xfer_in(&csw, CSW_LEN);
    if (r < 0) return -1;
    if (csw.dCSWSignature != BBB_CSW_SIGNATURE) return -1;
    return (csw.bCSWStatus == CSW_PASS) ? 0 : -1;
}

/* ─── SCSI commands ─── */

static int scsi_test_unit_ready(void) {
    uint8_t cmd[6] = { SCSI_CMD_TEST_READY, 0, 0, 0, 0, 0 };
    send_cbw(CBW_DIR_IN, 0, cmd, 6);
    return recv_csw();
}

static int scsi_request_sense(void *buf, int len) {
    uint8_t cmd[6] = { SCSI_CMD_REQUEST_SENSE, 0, 0, 0, len, 0 };
    send_cbw(CBW_DIR_IN, len, cmd, 6);
    int r = xfer_in(buf, len);
    recv_csw();
    return r;
}

static int scsi_inquiry(void *buf, int len) {
    uint8_t cmd[6] = { SCSI_CMD_INQUIRY, 0, 0, 0, len, 0 };
    send_cbw(CBW_DIR_IN, len, cmd, 6);
    int r = xfer_in(buf, len);
    recv_csw();
    return r;
}

static int scsi_read_capacity(void) {
    uint8_t cmd[10] = { SCSI_CMD_READ_CAP_10, 0, 0,0,0,0, 0,0,0,0 };
    uint8_t data[8] __attribute__((aligned(4)));
    memset(data, 0, sizeof(data));

    send_cbw(CBW_DIR_IN, 8, cmd, 10);
    int r = xfer_in(data, 8);
    recv_csw();
    if (r < 0) return -1;

    msc_sector_count = ((uint32_t)data[0] << 24) | ((uint32_t)data[1] << 16)
                     | ((uint32_t)data[2] << 8)  | (uint32_t)data[3];
    msc_sector_size  = ((uint32_t)data[4] << 24) | ((uint32_t)data[5] << 16)
                     | ((uint32_t)data[6] << 8)  | (uint32_t)data[7];
    msc_sector_count++;  /* last LBA + 1 = count */
    return 0;
}

static int scsi_read_10(uint32_t lba, uint32_t count, void *buf) {
    uint8_t cmd[10] = {
        SCSI_CMD_READ_10,
        0,
        (uint8_t)(lba >> 24), (uint8_t)(lba >> 16),
        (uint8_t)(lba >> 8),  (uint8_t)(lba),
        0,
        (uint8_t)(count >> 8), (uint8_t)(count),
        0
    };

    int total = count * msc_sector_size;
    send_cbw(CBW_DIR_IN, total, cmd, 10);

    /* Read in chunks that fit in the bounce buffer */
    int chunk_size = 4096;
    int done = 0;
    uint8_t *dst = (uint8_t *)buf;

    while (done < total) {
        int to_read = total - done;
        if (to_read > chunk_size) to_read = chunk_size;
        int r = xfer_in(msc_buf, to_read);
        if (r < 0) break;
        memcpy(dst + done, msc_buf, r);
        done += r;
    }

    recv_csw();
    return (done == total) ? 0 : -1;
}

static int scsi_write_10(uint32_t lba, uint32_t count, const void *buf) {
    uint8_t cmd[10] = {
        SCSI_CMD_WRITE_10,
        0,
        (uint8_t)(lba >> 24), (uint8_t)(lba >> 16),
        (uint8_t)(lba >> 8),  (uint8_t)(lba),
        0,
        (uint8_t)(count >> 8), (uint8_t)(count),
        0
    };

    int total = count * msc_sector_size;
    send_cbw(CBW_DIR_OUT, total, cmd, 10);

    int chunk_size = 4096;
    int done = 0;
    const uint8_t *src = (const uint8_t *)buf;

    while (done < total) {
        int to_write = total - done;
        if (to_write > chunk_size) to_write = chunk_size;
        memcpy(msc_buf, src + done, to_write);
        int r = xfer_out(msc_buf, to_write);
        if (r < 0) break;
        done += r;
    }

    recv_csw();
    return (done == total) ? 0 : -1;
}

/* ─── Init / probe ─── */

static int try_sense(void) {
    uint8_t sense[18] __attribute__((aligned(4)));
    memset(sense, 0, sizeof(sense));
    return scsi_request_sense(sense, 18);
}

int usb_msc_init(void) {
    msc_ok = 0;
    msc_dev = NULL;

    /* Scan EHCI devices */
    int nd = ehci_get_num_devs();
    for (int i = 0; i < nd; i++) {
        struct usb_dev *d = ehci_get_dev(i);
        if (!d) continue;
        /* Class 0x08 = Mass Storage, subclass 0x06 = SCSI, protocol 0x50 = BBB */
        if (d->class_code == 0x08) {
            kprintf("usb_msc: found mass storage device at dev %d "
                    "(VID=%04x PID=%04x class=%02x sub=%02x proto=%02x)\n",
                    i, d->vendor_id, d->product_id,
                    d->class_code, d->subclass, d->protocol);
            msc_dev = d;
            msc_bulk_in = d->ep_in_addr & 0x0F;
            msc_bulk_out = d->ep_out_addr & 0x0F;
            break;
        }
    }

    if (!msc_dev) return -1;

    /* Allocate DMA bounce buffer */
    uint64_t page = pmm_alloc_page();
    if (!page) { kprintf("usb_msc: out of memory\n"); return -1; }
    msc_buf = (uint8_t *)phys_to_virt(page);
    msc_buf_paddr = page;
    memset(msc_buf, 0, 4096);

    /* Reset LUN */
    msc_lun = 0;

    /* Wait for device to be ready (retry with sense) */
    for (int attempt = 0; attempt < 5; attempt++) {
        if (scsi_test_unit_ready() == 0) break;
        try_sense();
    }

    /* Inquiry */
    struct usb_msc_inquiry_data inq;
    memset(&inq, 0, sizeof(inq));
    if (scsi_inquiry(&inq, sizeof(inq)) < 0) {
        kprintf("usb_msc: inquiry failed\n");
        return -1;
    }
    inq.vendor[7] = 0;
    inq.product[15] = 0;
    inq.revision[3] = 0;
    kprintf("usb_msc: \"%s %s\" rev \"%s\"\n", inq.vendor, inq.product, inq.revision);

    /* Read capacity */
    if (scsi_read_capacity() < 0) {
        kprintf("usb_msc: read capacity failed\n");
        return -1;
    }

    uint64_t size_mb = ((uint64_t)msc_sector_count * msc_sector_size) / (1024 * 1024);
    kprintf("usb_msc: %u sectors x %u bytes = %llu MB\n",
            msc_sector_count, msc_sector_size, (unsigned long long)size_mb);

    msc_ok = 1;
    return 0;
}

/* ─── Public API ─── */

int usb_msc_available(void) { return msc_ok; }

int usb_msc_get_sector_size(void) { return msc_sector_size; }

uint32_t usb_msc_get_sector_count(void) { return msc_sector_count; }

int usb_msc_read_sectors(uint32_t lba, uint32_t count, void *buf) {
    if (!msc_ok || !buf) return -1;
    return scsi_read_10(lba, count, buf);
}

int usb_msc_write_sectors(uint32_t lba, uint32_t count, const void *buf) {
    if (!msc_ok || !buf) return -1;
    return scsi_write_10(lba, count, buf);
}

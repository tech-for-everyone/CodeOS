#include "ac97.h"
#include "../arch/x86_64/io.h"
#include "pci.h"
#include "../kernel/kprintf.h"
#include "../kernel/pmm.h"
#include "../kernel/vmm.h"
#include "../kernel/string.h"
#include "../kernel/audio_mixer.h"

#define AC97_VENDOR 0x8086
#define AC97_DEVICE_ICH   0x2415
#define AC97_DEVICE_ICH4  0x2668
#define AC97_DEVICE_ICH6  0x27DE

#define NUM_BD       32
#define BUFFER_SIZE  4096

ac97_t ac97;

static ac97_bd_t bd_list[NUM_BD] __attribute__((aligned(64)));
static uint8_t pcm_buffer[NUM_BD][BUFFER_SIZE] __attribute__((aligned(64)));

static void namm_write(uint16_t reg, uint16_t val) {
    outw(ac97.namm_base + reg, val);
}

static uint16_t namm_read(uint16_t reg) {
    return inw(ac97.namm_base + reg);
}

static void nabm_writeb(uint16_t reg, uint8_t val) {
    outb(ac97.nabm_base + reg, val);
}

static void nabm_writew(uint16_t reg, uint16_t val) {
    outw(ac97.nabm_base + reg, val);
}

static void nabm_writel(uint16_t reg, uint32_t val) {
    outl(ac97.nabm_base + reg, val);
}

static uint8_t nabm_readb(uint16_t reg) {
    return inb(ac97.nabm_base + reg);
}

int ac97_init(void) {
    ac97.present = 0;

    uint8_t bus, slot, func;
    int found = pci_find_class(0x04, 0x01, &bus, &slot, &func);
    if (!found) {
        found = pci_find_device(AC97_VENDOR, AC97_DEVICE_ICH, &bus, &slot, &func);
        if (!found)
            found = pci_find_device(AC97_VENDOR, AC97_DEVICE_ICH4, &bus, &slot, &func);
        if (!found)
            found = pci_find_device(AC97_VENDOR, AC97_DEVICE_ICH6, &bus, &slot, &func);
    }
    if (!found) {
        kprintf("ac97: no controller found\n");
        return -1;
    }

    uint16_t cmd = pci_config_read(bus, slot, func, 0x04);
    cmd |= 0x0005;
    pci_config_write(bus, slot, func, 0x04, cmd);

    uint32_t bar0 = pci_config_read(bus, slot, func, 0x10);
    uint32_t bar1 = pci_config_read(bus, slot, func, 0x14);
    ac97.nabm_base = bar0 & 0xFFFC;
    ac97.namm_base = bar1 & 0xFFFC;
    ac97.irq = pci_config_read(bus, slot, func, 0x3C) & 0xFF;
    ac97.num_bds = NUM_BD;

    kprintf("ac97: NABM=0x%04x NAMM=0x%04x IRQ=%d\n",
            ac97.nabm_base, ac97.namm_base, ac97.irq);

    namm_write(AC97_RESET, 0x0000);
    for (volatile int i = 0; i < 100000; i++) asm volatile("pause");

    uint16_t id = namm_read(AC97_RESET);
    if (id == 0xFFFF || id == 0x0000) {
        kprintf("ac97: codec not responding (id=0x%04x)\n", id);
        return -1;
    }
    kprintf("ac97: codec ID=0x%04x\n", id);

    nabm_writeb(AC97_PO_CTRL, AC97_CTRL_RESET);
    for (volatile int i = 0; i < 100000; i++) asm volatile("pause");
    nabm_writeb(AC97_PO_CTRL, 0);

    uint16_t vol = 0x1A1A;
    namm_write(AC97_MASTER_VOL, vol);
    namm_write(AC97_PCM_VOL, 0x0808);

    for (int i = 0; i < NUM_BD; i++) {
        bd_list[i].pointer = 0;
        bd_list[i].control = 0;
        bd_list[i].length  = 0;
    }
    bd_list[NUM_BD - 1].control = 0x0001;

    ac97.bds = bd_list;
    ac97.present = 1;
    kprintf("ac97: initialized\n");
    return 0;
}

int ac97_play_pcm(const void *data, int samples) {
    if (!ac97.present) return -1;
    if (samples <= 0 || samples > BUFFER_SIZE / 2)
        return -1;

    if (ac97_is_playing()) {
        nabm_writeb(AC97_PO_CTRL, 0);
        for (volatile int i = 0; i < 50000; i++) asm volatile("pause");
    }

    memset(pcm_buffer[0], 0, BUFFER_SIZE);
    memcpy(pcm_buffer[0], data, samples * 2);

    uint64_t pcm0_phys = virt_to_phys((uint64_t)pcm_buffer[0]);
    uint64_t pcm1_phys = virt_to_phys((uint64_t)pcm_buffer[1]);
    uint64_t bdl_phys  = virt_to_phys((uint64_t)bd_list);
    if (pcm0_phys > 0xFFFFFFFF || pcm1_phys > 0xFFFFFFFF || bdl_phys > 0xFFFFFFFF)
        return -1;

    bd_list[0].pointer = (uint32_t)pcm0_phys;
    bd_list[0].length  = samples - 1;
    bd_list[0].control = 0x0003;

    memset(pcm_buffer[1], 0, BUFFER_SIZE);
    bd_list[1].pointer = (uint32_t)pcm1_phys;
    bd_list[1].length  = 0;
    bd_list[1].control = 0x0001;

    nabm_writel(AC97_PO_BDBAR, (uint32_t)bdl_phys);
    nabm_writew(AC97_PO_LVI,       1);
    nabm_writeb(AC97_PO_STAT,      0x3F);
    nabm_writeb(AC97_PO_CTRL,      AC97_CTRL_RUN | AC97_CTRL_IOCE);

    return 0;
}

void ac97_set_volume(int vol) {
    if (!ac97.present) return;
    if (vol < 0) vol = 0;
    if (vol > 100) vol = 100;
    uint16_t v = (uint16_t)(31 - (vol * 31 / 100));
    namm_write(AC97_MASTER_VOL, (v << 8) | v);
}

int ac97_is_playing(void) {
    if (!ac97.present) return 0;
    return (nabm_readb(AC97_PO_CTRL) & AC97_CTRL_RUN) != 0;
}

void ac97_stop(void) {
    if (!ac97.present) return;
    nabm_writeb(AC97_PO_CTRL, 0);
}

/* ── Streaming mode: continuous DMA with mixer ── */

static int stream_running = 0;
static int16_t stream_buf[NUM_BD][BUFFER_SIZE / 2] __attribute__((aligned(64)));

void ac97_stream_init(void) {
    if (!ac97.present) return;

    for (int i = 0; i < NUM_BD; i++) {
        for (int j = 0; j < BUFFER_SIZE / 2; j++)
            stream_buf[i][j] = 0;
        bd_list[i].pointer = (uint32_t)virt_to_phys((uint64_t)&stream_buf[i][0]);
        bd_list[i].length = (BUFFER_SIZE / 4) - 1;  /* samples per buffer = 1024 (stereo) */
        bd_list[i].control = 0x0003;
    }
    bd_list[NUM_BD - 1].control = 0x0001;

    uint64_t bdl_phys = virt_to_phys((uint64_t)bd_list);
    nabm_writel(AC97_PO_BDBAR, (uint32_t)bdl_phys);
    nabm_writew(AC97_PO_LVI, NUM_BD - 2);
    nabm_writeb(AC97_PO_STAT, 0x3F);

    kprintf("ac97: streaming mode (%d buffers x %d bytes)\n", NUM_BD, BUFFER_SIZE);
}

void ac97_stream_start(void) {
    if (!ac97.present || stream_running) return;

    mixer_init();
    stream_running = 1;
    nabm_writeb(AC97_PO_CTRL, AC97_CTRL_RUN | AC97_CTRL_IOCE);
    kprintf("ac97: streaming started\n");
}

void ac97_stream_stop(void) {
    if (!ac97.present) return;
    stream_running = 0;
    nabm_writeb(AC97_PO_CTRL, 0);
    kprintf("ac97: streaming stopped\n");
}

void ac97_stream_fill(int16_t *buf, int samples) {
    mixer_mix(buf, samples);
}

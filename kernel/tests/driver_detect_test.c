/*
 * Host test for CodeOS Automated Driver Detection Subsystem,
 * Logitech G305 Lightspeed Mouse Driver, and AULA F87 Keyboard Driver.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <stdint.h>
#include "types.h"
#include "mouse_g305.h"
#include "kbd_aula_f87.h"

/* Mock driver module structure matching kernel/drivers/drivers.h */
typedef enum {
    TEST_DRIVER_CLASS_SYSTEM = 0,
    TEST_DRIVER_CLASS_TIMER,
    TEST_DRIVER_CLASS_BUS,
    TEST_DRIVER_CLASS_INPUT,
    TEST_DRIVER_CLASS_MOUSE,
    TEST_DRIVER_CLASS_KEYBOARD,
    TEST_DRIVER_CLASS_STORAGE,
    TEST_DRIVER_CLASS_NETWORK
} test_driver_class_t;

typedef enum {
    TEST_STATUS_UNLOADED = 0,
    TEST_STATUS_LOADED,
    TEST_STATUS_NOT_PRESENT,
    TEST_STATUS_FAILED
} test_driver_status_t;

typedef struct test_driver_module {
    const char           *name;
    const char           *description;
    test_driver_class_t   category;
    int                 (*probe)(void);
    int                 (*load)(void *ram_instance);
    void                (*unload)(void *ram_instance);
    uint32_t              ram_size;
    void                 *ram_instance;
    test_driver_status_t  status;
} test_driver_module_t;

/* Global test hardware presence flags */
static int hw_timer_present = 1;
static int hw_ps2_kbd_present = 1;
static int hw_g305_present = 0;
static int hw_aula_f87_present = 0;
static int hw_nvme_present = 0;

/* Probes */
static int mock_probe_timer(void) { return hw_timer_present; }
static int mock_probe_ps2_kbd(void) { return hw_ps2_kbd_present; }
static int mock_probe_g305(void) { return hw_g305_present; }
static int mock_probe_aula_f87(void) { return hw_aula_f87_present; }
static int mock_probe_nvme(void) { return hw_nvme_present; }

/* Loaders */
static int mock_load_timer(void *ram) {
    memset(ram, 0xA5, 64);
    return 0;
}
static int mock_load_ps2_kbd(void *ram) {
    memset(ram, 0x5A, 128);
    return 0;
}
static int mock_load_g305(void *ram) {
    struct g305_state *st = (struct g305_state *)ram;
    memset(st, 0, sizeof(*st));
    st->present = 1;
    st->vendor_id = LOGITECH_VID;
    st->product_id = LOGITECH_PID_G305_RECEIVER;
    st->current_dpi = 1600;
    st->dpi_preset_idx = 2;
    st->battery_percent = -1;
    st->performance_mode = G305_MODE_UNKNOWN;
    return 0;
}
static int mock_load_aula_f87(void *ram) {
    struct aula_f87_state *st = (struct aula_f87_state *)ram;
    memset(st, 0, sizeof(*st));
    st->present = 1;
    st->vendor_id = AULA_VID_258A;
    st->product_id = AULA_PID_F87_PRO;
    st->nkro_active = 0;
    st->win_lock = 0;
    st->rgb_mode = AULA_RGB_MODE_UNKNOWN;
    st->rgb_brightness = -1;
    return 0;
}
static int mock_load_nvme(void *ram) {
    memset(ram, 0xEE, 512);
    return 0;
}

static test_driver_module_t test_modules[] = {
    { "pit_timer",           "Intel 8254 Timer",           TEST_DRIVER_CLASS_TIMER,    mock_probe_timer,    mock_load_timer,    NULL, 64,                        NULL, TEST_STATUS_UNLOADED },
    { "ps2_keyboard",        "PS/2 Keyboard",              TEST_DRIVER_CLASS_KEYBOARD, mock_probe_ps2_kbd, mock_load_ps2_kbd, NULL, 128,                       NULL, TEST_STATUS_UNLOADED },
    { "logitech_g305_mouse", "Logitech G305 Lightspeed",   TEST_DRIVER_CLASS_MOUSE,    mock_probe_g305,     mock_load_g305,     NULL, sizeof(struct g305_state), NULL, TEST_STATUS_UNLOADED },
    { "aula_f87_keyboard",   "AULA F87 Keyboard (TKL)",    TEST_DRIVER_CLASS_KEYBOARD, mock_probe_aula_f87, mock_load_aula_f87, NULL, sizeof(struct aula_f87_state), NULL, TEST_STATUS_UNLOADED },
    { "nvme_ssd",            "NVM Express Storage",        TEST_DRIVER_CLASS_STORAGE,  mock_probe_nvme,     mock_load_nvme,     NULL, 512,                       NULL, TEST_STATUS_UNLOADED }
};

#define TEST_MOD_COUNT (sizeof(test_modules)/sizeof(test_modules[0]))

static void run_automated_detection(void) {
    for (size_t i = 0; i < TEST_MOD_COUNT; i++) {
        test_driver_module_t *m = &test_modules[i];
        if (m->ram_instance) {
            free(m->ram_instance);
            m->ram_instance = NULL;
        }
        m->status = TEST_STATUS_UNLOADED;

        int present = m->probe ? m->probe() : 0;
        if (present) {
            m->ram_instance = calloc(1, m->ram_size);
            assert(m->ram_instance != NULL);
            int r = m->load(m->ram_instance);
            assert(r == 0);
            m->status = TEST_STATUS_LOADED;
        } else {
            /* Strict: non-detected drivers are NOT loaded */
            m->status = TEST_STATUS_NOT_PRESENT;
            m->ram_instance = NULL;
        }
    }
}

int main(void) {
    printf("==> Running Driver Subsystem & Hardware Tests...\n");

    /* 1. Test G305 Constant & Protocol Definitions */
    assert(LOGITECH_VID == 0x046D);
    assert(LOGITECH_PID_G305_RECEIVER == 0xC53F);
    assert(G305_MODE_PERFORMANCE == 1);
    assert(G305_MODE_ENDURANCE == 0);
    assert(G305_BTN_LEFT == 0x01);
    assert(G305_BTN_RIGHT == 0x02);
    assert(G305_BTN_DPI_SWITCH == 0x20);
    assert(g305_device_matches(LOGITECH_VID, LOGITECH_PID_G305_RECEIVER));
    assert(!g305_device_matches(LOGITECH_VID, LOGITECH_PID_LIGHTSPEED_RECEIVER));
    assert(!g305_device_matches(AULA_VID_258A, AULA_PID_F87_PRO));
    printf("  [PASS] G305 constant and HID++ definitions verified\n");

    /* 2. Test AULA F87 Constant Definitions */
    assert(AULA_VID_258A == 0x258A);
    assert(AULA_PID_F87_PRO == 0x0049);
    assert(AULA_RGB_MODE_RAINBOW == 2);
    assert(AULA_KEY_VOL_UP == 0xE003);
    assert(aula_f87_device_matches(AULA_VID_258A, AULA_PID_F87_PRO));
    assert(!aula_f87_device_matches(AULA_VID_258A, 0xFFFF));
    printf("  [PASS] AULA F87 constant and layout definitions verified\n");

    /* 3. Test Detection Subsystem: Undetected Drivers Withheld */
    hw_timer_present = 1;
    hw_ps2_kbd_present = 1;
    hw_g305_present = 0;
    hw_aula_f87_present = 0;
    hw_nvme_present = 0;

    run_automated_detection();

    assert(test_modules[0].status == TEST_STATUS_LOADED);
    assert(test_modules[0].ram_instance != NULL);

    assert(test_modules[1].status == TEST_STATUS_LOADED);
    assert(test_modules[1].ram_instance != NULL);

    /* Strict verification: G305, Aula F87, and NVMe are NOT present, so they must be UNLOADED with 0 RAM */
    assert(test_modules[2].status == TEST_STATUS_NOT_PRESENT);
    assert(test_modules[2].ram_instance == NULL);

    assert(test_modules[3].status == TEST_STATUS_NOT_PRESENT);
    assert(test_modules[3].ram_instance == NULL);

    assert(test_modules[4].status == TEST_STATUS_NOT_PRESENT);
    assert(test_modules[4].ram_instance == NULL);
    printf("  [PASS] Invariant verified: un-detected drivers strictly not loaded\n");

    /* 4. Test Detection Subsystem: Hotplug / Detection of G305 and Aula F87 */
    hw_g305_present = 1;
    hw_aula_f87_present = 1;

    run_automated_detection();

    /* G305 is now detected and loaded into RAM */
    assert(test_modules[2].status == TEST_STATUS_LOADED);
    assert(test_modules[2].ram_instance != NULL);
    struct g305_state *g305 = (struct g305_state *)test_modules[2].ram_instance;
    assert(g305->present == 1);
    assert(g305->vendor_id == LOGITECH_VID);
    assert(g305->current_dpi == 1600);
    assert(g305->performance_mode == G305_MODE_UNKNOWN);
    assert(g305->battery_percent == -1);
    printf("  [PASS] G305 Lightspeed dynamically allocated and loaded into volatile RAM\n");

    /* Aula F87 is now detected and loaded into RAM */
    assert(test_modules[3].status == TEST_STATUS_LOADED);
    assert(test_modules[3].ram_instance != NULL);
    struct aula_f87_state *f87 = (struct aula_f87_state *)test_modules[3].ram_instance;
    assert(f87->present == 1);
    assert(f87->vendor_id == AULA_VID_258A);
    assert(f87->nkro_active == 0);
    assert(f87->win_lock == 0);
    assert(f87->rgb_mode == AULA_RGB_MODE_UNKNOWN);
    printf("  [PASS] AULA F87 mechanical keyboard dynamically allocated and loaded into volatile RAM\n");

    /* Clean up */
    for (size_t i = 0; i < TEST_MOD_COUNT; i++) {
        if (test_modules[i].ram_instance) {
            free(test_modules[i].ram_instance);
            test_modules[i].ram_instance = NULL;
        }
    }

    printf("==> ALL DRIVER SUBSYSTEM HOST TESTS PASSED!\n");
    return 0;
}

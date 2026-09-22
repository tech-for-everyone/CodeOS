#include "sdl3_cpu_driver.h"
#include <string.h>

void sdl3_cpu_driver_init(SDL3_CPU_Driver *driver) {
    if (!driver) return;
    driver->initialized = false;
    memset(&driver->info, 0, sizeof(driver->info));
    driver->info.vendor = SDL3_CPU_VENDOR_UNKNOWN;
    driver->info.cores = 4;
    driver->info.threads = 4;
    driver->info.l2_cache_kb = 512;
    driver->info.l3_cache_kb = 16384;
    driver->info.base_freq_mhz = 3500.0f;
    driver->info.max_freq_mhz = 4900.0f;
    strcpy(driver->info.brand, "CodeOS Virtual CPU");
    driver->initialized = true;
}

const char *sdl3_cpu_driver_get_brand(SDL3_CPU_Driver *driver) {
    return driver ? driver->info.brand : "Unknown";
}

int sdl3_cpu_driver_get_cores(SDL3_CPU_Driver *driver) {
    return driver ? driver->info.cores : 0;
}

int sdl3_cpu_driver_get_threads(SDL3_CPU_Driver *driver) {
    return driver ? driver->info.threads : 0;
}

void sdl3_cpu_driver_render_info(SDL3_CPU_Driver *driver, int x, int y, int w, int h) {
    (void)driver; (void)x; (void)y; (void)w; (void)h;
}

void sdl3_cpu_driver_render_core_map(SDL3_CPU_Driver *driver, int x, int y, int w, int h) {
    (void)driver; (void)x; (void)y; (void)w; (void)h;
}

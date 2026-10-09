#include "sdl3_cpu.h"
#include <string.h>

static bool amd_detected = false;
static SDL3_CPUInfo amd_info;

static void amd_init(void) {
    amd_detected = true;
    memset(&amd_info, 0, sizeof(amd_info));
    amd_info.vendor = SDL3_CPU_VENDOR_AMD;
    amd_info.family = 25;
    amd_info.model = 1;
    amd_info.stepping = 1;
    amd_info.cores = 8;
    amd_info.threads = 16;
    amd_info.l2_cache_kb = 512;
    amd_info.l3_cache_kb = 16384;
    amd_info.base_freq_mhz = 3500.0f;
    amd_info.max_freq_mhz = 4900.0f;
    amd_info.features = 0xFBFFAFF;
    amd_info.ext_features = 0x1C37D3;
    strcpy(amd_info.brand, "AMD Ryzen 7 7800X3D");
}

bool sdl3_cpu_detect(SDL3_CPUInfo *info) {
    if (!amd_detected) {
        amd_init();
    }
    if (info) {
        *info = amd_info;
    }
    return amd_detected;
}

const char *sdl3_cpu_vendor_name(SDL3_CPUVendor vendor) {
    switch (vendor) {
    case SDL3_CPU_VENDOR_AMD: return "AMD";
    case SDL3_CPU_VENDOR_INTEL: return "Intel";
    default: return "Unknown";
    }
}

const char *sdl3_cpu_get_brand(SDL3_CPUInfo *info) {
    return info ? info->brand : "Unknown";
}

int sdl3_cpu_get_cores(SDL3_CPUInfo *info) {
    return info ? info->cores : 0;
}

int sdl3_cpu_get_threads(SDL3_CPUInfo *info) {
    return info ? info->threads : 0;
}

void sdl3_cpu_render_info(SDL3_CPUInfo *info, int x, int y, int w, int h) {
    (void)info; (void)x; (void)y; (void)w; (void)h;
}

void sdl3_cpu_render_core_map(SDL3_CPUInfo *info, int x, int y, int w, int h) {
    (void)info; (void)x; (void)y; (void)w; (void)h;
}

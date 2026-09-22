#include "sdl3_cpu.h"
#include <string.h>

static bool intel_detected = false;
static SDL3_CPUInfo intel_info;

static void intel_init(void) {
    intel_detected = true;
    memset(&intel_info, 0, sizeof(intel_info));
    intel_info.vendor = SDL3_CPU_VENDOR_INTEL;
    intel_info.family = 6;
    intel_info.model = 140;
    intel_info.stepping = 2;
    intel_info.cores = 12;
    intel_info.threads = 24;
    intel_info.l2_cache_kb = 128;
    intel_info.l3_cache_kb = 30720;
    intel_info.base_freq_mhz = 3400.0f;
    intel_info.max_freq_mhz = 5800.0f;
    intel_info.features = 0xFBFFAFF;
    intel_info.ext_features = 0x1C37D3;
    strcpy(intel_info.brand, "Intel Core i9-14900K");
}

bool sdl3_cpu_detect(SDL3_CPUInfo *info) {
    if (!intel_detected) {
        intel_init();
    }
    if (info) {
        *info = intel_info;
    }
    return intel_detected;
}

const char *sdl3_cpu_vendor_name(SDL3_CPUVendor vendor) {
    switch (vendor) {
    case SDL3_CPU_VENDOR_INTEL: return "Intel";
    case SDL3_CPU_VENDOR_AMD: return "AMD";
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

#ifndef SDL3_CPU_H
#define SDL3_CPU_H

#include <stdint.h>
#include <stdbool.h>

typedef enum {
    SDL3_CPU_VENDOR_INTEL = 0,
    SDL3_CPU_VENDOR_AMD = 1,
    SDL3_CPU_VENDOR_UNKNOWN = 2,
} SDL3_CPUVendor;

typedef struct {
    SDL3_CPUVendor vendor;
    char brand[64];
    int family;
    int model;
    int stepping;
    int cores;
    int threads;
    uint64_t features;
    uint64_t ext_features;
    uint32_t l2_cache_kb;
    uint32_t l3_cache_kb;
    float base_freq_mhz;
    float max_freq_mhz;
} SDL3_CPUInfo;

bool sdl3_cpu_detect(SDL3_CPUInfo *info);
const char *sdl3_cpu_vendor_name(SDL3_CPUVendor vendor);
const char *sdl3_cpu_get_brand(SDL3_CPUInfo *info);
int sdl3_cpu_get_cores(SDL3_CPUInfo *info);
int sdl3_cpu_get_threads(SDL3_CPUInfo *info);
void sdl3_cpu_render_info(SDL3_CPUInfo *info, int x, int y, int w, int h);
void sdl3_cpu_render_core_map(SDL3_CPUInfo *info, int x, int y, int w, int h);

#endif /* SDL3_CPU_H */

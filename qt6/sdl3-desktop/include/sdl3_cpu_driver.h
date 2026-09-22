#ifndef SDL3_CPU_DRIVER_H
#define SDL3_CPU_DRIVER_H

#include "sdl3_cpu.h"

typedef struct {
    SDL3_CPUInfo info;
    bool initialized;
    const char *(*detect)(void);
    void (*render_info)(int x, int y, int w, int h);
    void (*render_core_map)(int x, int y, int w, int h);
} SDL3_CPU_Driver;

extern SDL3_CPU_Driver cpu_driver_amd;
extern SDL3_CPU_Driver cpu_driver_intel;

void sdl3_cpu_driver_init(SDL3_CPU_Driver *driver);
const char *sdl3_cpu_driver_detect(SDL3_CPU_Driver *driver);
void sdl3_cpu_driver_render_info(SDL3_CPU_Driver *driver, int x, int y, int w, int h);
void sdl3_cpu_driver_render_core_map(SDL3_CPU_Driver *driver, int x, int y, int w, int h);

#endif /* SDL3_CPU_DRIVER_H */

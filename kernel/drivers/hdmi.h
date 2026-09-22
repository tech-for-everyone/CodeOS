#ifndef HDMI_H
#define HDMI_H

#include "types.h"

int          hdmi_detect(uint64_t *fb_addr, uint32_t *width, uint32_t *height, uint32_t *pitch);
const char  *hdmi_gpu_name(void);

#endif

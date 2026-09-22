#ifndef XORA_H
#define XORA_H

#include "types.h"

void cmd_xora(int argc, char **argv);
void cmd_nettest(int argc, char **argv);

int  xora_gzip_decompress(const uint8_t *gzdata, int gzlen, uint8_t **out, int *outlen);
void xora_inspect(const char *path);
void xora_install(const char *path);
void xora_pack(const char *source_dir, const char *output);
void xora_list(void);

#endif

#ifndef __IOFS_H
#define __IOFS_H

#include <stdint.h>
#include <stddef.h>

int read(const char *path, void *buffer, uint32_t count, uint32_t offset);
int write(const char *path, const void *buffer, uint32_t count, uint32_t offset);
int update(const char *path, uint32_t mode, uint32_t mtime);
int remove(const char *path);

#endif

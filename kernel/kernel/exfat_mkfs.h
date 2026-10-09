#ifndef EXFAT_MKFS_H
#define EXFAT_MKFS_H

#include "types.h"

/* File to be laid down on the ExFAT boot partition. */
typedef struct {
    const char *name;      /* filename, ASCII (e.g. "limine-bios.sys") */
    const void *data;
    uint64_t    size;
} exfat_mkfs_file_t;

/* Format pseudo-partition `dir` (either "boot" or "limine") onto the ExFAT
 * volume at partition part_idx.  All metadata (VBR, FAT, bitmap, upcase
 * table, root directory) is created with the same layout as the verified
 * reference volume.  Returns 0 on success, -1 on failure. */
int exfat_mkfs_partition(int part_idx);

/* High-level: format the whole boot volume with the two pseudo-partitions
 * (boot/ and limine/), writing the given files.  files with dir name "boot"
 * go into /boot, "limine" into /limine. */
int exfat_mkfs_volume(int part_idx,
                      const exfat_mkfs_file_t *boot_files, int n_boot,
                      const exfat_mkfs_file_t *limine_files, int n_limine);

#endif
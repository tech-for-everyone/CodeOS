#ifndef CODEFS_MKFS_H
#define CODEFS_MKFS_H

#include "types.h"

/* Format the CodeFS (CPF) data/home partition at `part_idx`.
 *
 * On-disk layout (matches codefs.c runtime EXACTLY — this is the format the
 * kernel mounts, NOT the stale scripts/mkcodefs.py):
 *   block 0:                    reserved (boot area)
 *   block 1:                    superblock at byte offset CODEFS_SB_OFFSET
 *   blocks inode_table_start:   inode table (256-byte slots, 16 per block)
 *   journal_start..+journal_len: journal
 *   data_area_start-2:          block bitmap (bit idx -> data_area_start+idx)
 *   data_area_start-1:          inode bitmap (bit idx -> inode idx+1)
 *   data_area_start..:          data blocks
 *
 * Returns 0 on success, -1 on failure.
 */
int codefs_mkfs(int part_idx);

#endif
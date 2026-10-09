#ifndef CODEFS_H
#define CODEFS_H

#include <stdint.h>

#define CODEFS_MAGIC       0x434F4445
#define CODEFS_VERSION     1
#define CODEFS_BLOCK_SIZE  4096
#define CODEFS_NAME_MAX    255
#define CODEFS_PATH_MAX    4096
#define CODEFS_INLINE_MAX  60
#define CODEFS_MAX_EXTENTS 4
#define CODEFS_INODE_SIZE  256
#define CODEFS_JOURNAL_BLOCKS 256

#define CODEFS_SB_OFFSET   1024

#define CODEFS_S_CLEAN     0x0001
#define CODEFS_S_DIRTY     0x0002
#define CODEFS_S_ERROR     0x0004

#define CODEFS_FT_UNKNOWN  0
#define CODEFS_FT_REG      1
#define CODEFS_FT_DIR      2
#define CODEFS_FT_SYMLINK  3

#define CODEFS_MAGIC_BE 0x45444F43

struct codefs_superblock {
    uint32_t magic;
    uint32_t version;
    uint32_t flags;
    uint32_t block_size;
    uint32_t blocks_count;
    uint32_t free_blocks;
    uint32_t inodes_count;
    uint32_t free_inodes;
    uint32_t root_inode;
    uint32_t journal_inode;
    uint32_t journal_start;
    uint32_t journal_len;
    uint32_t inode_table_start;
    uint32_t data_area_start;
    uint32_t inode_count;
    uint32_t total_inodes;
    uint32_t mount_count;
    uint32_t max_mounts;
    uint32_t state;
    uint32_t created_time;
    uint32_t last_mount;
    uint32_t last_write;
    uint32_t pad[101];
    uint32_t crc;
} __attribute__((packed));

struct codefs_extent {
    uint32_t block;
    uint32_t length;
    uint32_t disk_start;
    uint32_t pad;
} __attribute__((packed));

struct codefs_inode {
    uint16_t mode;
    uint16_t uid;
    uint16_t gid;
    uint32_t size;
    uint32_t atime;
    uint32_t ctime;
    uint32_t mtime;
    uint32_t links;
    uint32_t flags;
    uint32_t inline_len;
    uint8_t  inline_data[CODEFS_INLINE_MAX];
    struct codefs_extent extents[CODEFS_MAX_EXTENTS];
    uint32_t extent_count;
    uint32_t pad[2];
    uint32_t crc;
} __attribute__((packed));

struct codefs_dirent {
    uint32_t inode;
    uint16_t rec_len;
    uint8_t  name_len;
    uint8_t  file_type;
    char     name[0];
} __attribute__((packed));

struct codefs_journal_header {
    uint32_t magic;
    uint32_t seq;
    uint32_t start;
    uint32_t len;
    uint32_t crc;
};

#define CODEFS_DIR_REC_MIN 12

static inline uint32_t codefs_dir_rec_len(uint32_t name_len) {
    uint32_t len = CODEFS_DIR_REC_MIN + name_len;
    len = (len + 3) & ~3u;
    return len;
}

int  codefs_mount(int part_idx);
int  codefs_read_file(const char *path, void *buf, int max);
int  codefs_write_file(const char *path, const void *buf, int len);
int  codefs_mkdir(const char *path);
int  codefs_unlink(const char *path);
int  codefs_rmdir(const char *path);
int  codefs_find(const char *path, struct codefs_inode *out);
int  codefs_list_dir(const char *path);
int  codefs_stat(const char *path, struct codefs_inode *out);
int  codefs_mounted(void);
int  codefs_sync(void);

extern struct vfs_ops codefs_ops;

#endif

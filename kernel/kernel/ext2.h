#ifndef EXT2_H
#define EXT2_H

#include "types.h"

#define EXT2_NAME_MAX 256
#define EXT2_FILES 128

#define EXT2_S_IFMT     0xF000
#define EXT2_S_IFDIR    0x4000
#define EXT2_S_IFREG    0x8000
#define EXT2_S_IFLNK    0xA000
#define EXT2_FT_DIR     2
#define EXT2_FT_REG     1
#define EXT2_FT_SYMLINK 7

struct ext2_inode {
    uint16_t mode;
    uint16_t uid;
    uint32_t size;
    uint32_t atime;
    uint32_t ctime;
    uint32_t mtime;
    uint32_t dtime;
    uint16_t gid;
    uint16_t links_count;
    uint32_t blocks;
    uint32_t flags;
    uint32_t osd1;
    uint32_t block[15];
    uint32_t generation;
    uint32_t file_acl;
    uint32_t dir_acl;
    uint32_t faddr;
    uint32_t osd2[3];
} __attribute__((packed));

struct ext2_dirent {
    uint32_t inode;
    uint16_t rec_len;
    uint8_t  name_len;
    uint8_t  file_type;
    char     name[0];
} __attribute__((packed));

typedef struct {
    int   inode;
    int   is_dir;
    char  name[EXT2_NAME_MAX];
    int   size;
    int   valid;
} ext2_dirent_t;

int  ext2_mount(int part_idx);
int  ext2_read_inode(int inode, void *buf);
int  ext2_read_file(int inode, void *buf, int max, int offset);
int  ext2_list_root(void);
int  ext2_find(const char *path, ext2_dirent_t *ent);
int  ext2_mounted(void);
int  ext2_list_dir(const char *path);
int  ext2_read_file_path(const char *path, void *buf, int max);
int  ext2_write_file_path(const char *path, const void *buf, int max);
int  ext2_mkdir(const char *path);
int  ext2_creat(const char *path);
int  ext2_unlink(const char *path);
int  ext2_rmdir(const char *path);

#endif

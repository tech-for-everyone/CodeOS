#ifndef FS_H
#define FS_H

#define FS_NAME_MAX  32
#define FS_PATH_MAX  256
#define FS_CONTENT_MAX 65536
#define FS_MAX_NODES 512

typedef struct {
    char name[FS_NAME_MAX];
    int  is_dir;
    char content[FS_CONTENT_MAX];
    int  content_len;
    /* Files too large for `content` (an initramfs binary bigger than
     * FS_CONTENT_MAX) keep their bytes in an image-resident blob instead;
     * content/content_len are then unused and blob/blob_len describe the
     * whole file. See fs_attach_blob(). */
    const char *blob;
    int  blob_len;
    int  parent;
    int  first_child;
    int  next_sibling;
} fs_node_t;

void fs_init(void);
int  fs_find(int dir_idx, const char *name);
int  fs_mkfile(const char *path);
int  fs_mkdir(const char *path);
int  fs_rm(const char *path);
int  fs_rmdir(const char *path);
int  fs_write(const char *path, const char *data, int len);
int  fs_read(const char *path, char *buf, int max);
int  fs_read_at(const char *path, int off, char *buf, int len);
int  fs_attach_blob(const char *path, const char *data, int len);
void fs_ls(const char *path);
int  fs_dir_list(const char *path, char *out, int max);
int  fs_resolve(const char *path, int *is_dir);
int  fs_node_count(void);
int  fs_cd(const char *path);
void fs_getcwd(char *buf, int max);
int  fs_listdir(const char *path, char names[][FS_NAME_MAX], int max);
int  fs_get_info(const char *path, int *size, int *is_dir);
int  fs_rename(const char *oldpath, const char *newpath);

#endif

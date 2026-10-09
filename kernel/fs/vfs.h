#ifndef FS_VFS_H
#define FS_VFS_H

#define VFS_NAME_MAX    32
#define VFS_PATH_MAX    256
#define VFS_CONTENT_MAX 4096

typedef struct vfs_node {
    char name[VFS_NAME_MAX];
    int  is_dir;
    int  size;
    struct vfs_node *parent;
    struct vfs_node *first_child;
    struct vfs_node *next_sibling;
} vfs_node_t;

void vfs_init(void);
int  vfs_lookup(const char *path, vfs_node_t **out);
int  vfs_create_file(const char *path);
int  vfs_mkdir(const char *path);
int  vfs_mount(const char *source, const char *target, const char *fstype);
int  vfs_open(const char *path, const char *mode);

#endif

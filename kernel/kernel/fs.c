#include "fs.h"
#include "kprintf.h"
#include "string.h"
#include "ext2.h"

static fs_node_t nodes[FS_MAX_NODES];
static int node_count;

/* The current directory, as a path.
 *
 * It used to be an index into the in-memory table, which cannot express a
 * directory that lives on the mounted volume: the table has no node for
 * it, so `cd` into one failed and the volume's directories were
 * unreachable.  A path can express both, and it is also what the volume's
 * own API wants -- every ext2_* call takes a path -- so the path is the
 * truth now and the table's index is derived from it. */
static char cwd_path[FS_PATH_MAX] = "/";

int fs_node_count(void) { return node_count; }

static int alloc_node(void) {
    if (node_count >= FS_MAX_NODES) return -1;
    int i = node_count++;
    /* zero everything */
    for (int j = 0; j < FS_NAME_MAX; j++) nodes[i].name[j] = 0;
    for (int j = 0; j < FS_CONTENT_MAX; j++) nodes[i].content[j] = 0;
    nodes[i].is_dir = 0;
    nodes[i].content_len = 0;
    nodes[i].blob = 0;
    nodes[i].blob_len = 0;
    nodes[i].parent = -1;
    nodes[i].first_child = -1;
    nodes[i].next_sibling = -1;
    return i;
}

void fs_init(void) {
    node_count = 0;
    memset(nodes, 0, sizeof(nodes));
    int root = alloc_node();
    nodes[root].name[0] = '/';
    nodes[root].name[1] = 0;
    nodes[root].is_dir = 1;
    nodes[root].parent = root;
    cwd_path[0] = '/';
    cwd_path[1] = 0;
}

/* Make a path absolute and normal.
 *
 * A relative path is taken against the current directory; "." and ".."
 * are resolved textually, so the result never contains either and the
 * same string can be handed to both layers -- the in-memory table and the
 * mounted volume -- and mean the same thing in each.  ".." at the root
 * stays at the root: it has no parent to go to.
 *
 * Returns 0 on success, -1 if the path does not fit or is malformed. */
static int abs_path(const char *in, char *out, int out_sz) {
    if (!in || !out || out_sz < 2) return -1;

    char joined[FS_PATH_MAX * 2];
    if (!*in || strcmp(in, ".") == 0) {
        strlcpy(joined, cwd_path, sizeof(joined));
    } else if (in[0] == '/') {
        strlcpy(joined, in, sizeof(joined));
    } else {
        int n = snprintf(joined, sizeof(joined), "%s/%s", cwd_path, in);
        if (n < 0 || n >= (int)sizeof(joined)) return -1;
    }

    char norm[FS_PATH_MAX];
    int pos = 0;
    norm[0] = 0;
    const char *p = joined;
    while (*p) {
        while (*p == '/') p++;
        if (!*p) break;
        const char *seg = p;
        while (*p && *p != '/') p++;
        int len = (int)(p - seg);
        if (len == 1 && seg[0] == '.') continue;
        if (len == 2 && seg[0] == '.' && seg[1] == '.') {
            /* Back up one component. */
            while (pos > 0 && norm[pos - 1] != '/') pos--;
            if (pos > 0) pos--;   /* and the slash in front of it */
            continue;
        }
        if (pos + len + 2 > (int)sizeof(norm)) return -1;
        norm[pos++] = '/';
        memcpy(norm + pos, seg, len);
        pos += len;
    }
    if (pos == 0) { norm[0] = '/'; norm[1] = 0; pos = 1; }
    norm[pos] = 0;
    if (pos + 1 > out_sz) return -1;
    memcpy(out, norm, pos + 1);
    return 0;
}

/* split path into parent dir path + last component */
/* returns 0 on success, -1 on failure */
/* caller must provide buffers */
static int split_path(const char *path, char *dir_part, int dmax,
                      char *name_part, int nmax) {
    if (!path || !dir_part || !name_part || dmax <= 0 || nmax <= 0)
        return -1;
    int len = strlen(path);
    if (len == 0 || len >= dmax) return -1;

    /* copy path to dir_part, then find last '/' */
    int i;
    for (i = 0; i < len && i < dmax - 1; i++) dir_part[i] = path[i];
    dir_part[i] = 0;

    /* find last '/' */
    int slash = -1;
    for (i = 0; dir_part[i]; i++)
        if (dir_part[i] == '/') slash = i;

    if (slash < 0) {
        /* relative path, no slash */
        for (i = 0; path[i] && i < nmax - 1; i++) name_part[i] = path[i];
        name_part[i] = 0;
        dir_part[0] = '.';
        dir_part[1] = 0;
    } else if (slash == 0 && path[1] == 0) {
        /* "/" -> root */
        name_part[0] = 0;
        dir_part[0] = '/';
        dir_part[1] = 0;
    } else {
        /* split at last slash */
        int j;
        for (j = 0; j < slash && j < dmax - 1; j++) dir_part[j] = path[j];
        dir_part[j] = 0;
        if (j == 0) { dir_part[0] = '/'; dir_part[1] = 0; }

        int k = 0;
        for (j = slash + 1; path[j] && k < nmax - 1; j++)
            name_part[k++] = path[j];
        name_part[k] = 0;
    }
    return 0;
}

/* Resolve a path to an index in the in-memory table, or -1 if the table
 * does not have it.  The table is the *base* layer; the mounted volume is
 * the other one, and a path the table does not know may still exist
 * there (see fs_path_exists). */
static int resolve_dir(const char *path) {
    char abs[FS_PATH_MAX];
    if (abs_path(path, abs, sizeof(abs)) < 0) return -1;

    int cur = 0;   /* the root is always index 0 */
    char comp[FS_NAME_MAX];
    int depth = 0;

    const char *p = abs;
    while (*p) {
        while (*p == '/') p++;
        if (!*p) break;

        int ci = 0;
        while (*p && *p != '/' && ci < FS_NAME_MAX - 1)
            comp[ci++] = *p++;
        comp[ci] = 0;

        /* depth guard against corrupted trees */
        if (++depth > FS_MAX_NODES) return -1;

        if (cur < 0 || cur >= node_count) return -1;
        int child = nodes[cur].first_child;
        int found = 0;
        while (child >= 0) {
            if (child >= node_count) return -1;
            if (strcmp(nodes[child].name, comp) == 0) {
                cur = child;
                found = 1;
                break;
            }
            child = nodes[child].next_sibling;
        }
        if (!found) return -1;
    }
    return cur;
}

int fs_find(int dir_idx, const char *name) {
    if (dir_idx < 0 || dir_idx >= node_count) return -1;
    int child = nodes[dir_idx].first_child;
    while (child >= 0) {
        if (child >= node_count) return -1;
        if (strcmp(nodes[child].name, name) == 0) return child;
        child = nodes[child].next_sibling;
    }
    return -1;
}

int fs_mkfile(const char *path) {
    char dir_part[FS_PATH_MAX], name_part[FS_NAME_MAX];
    if (split_path(path, dir_part, FS_PATH_MAX, name_part, FS_NAME_MAX) < 0)
        return -1;
    if (!name_part[0]) return -1;

    int dir = resolve_dir(dir_part);
    if (dir < 0 || !nodes[dir].is_dir) return -1;

    /* check if already exists */
    if (fs_find(dir, name_part) >= 0) return -1;

    int idx = alloc_node();
    if (idx < 0) return -1;

    int i;
    for (i = 0; name_part[i] && i < FS_NAME_MAX - 1; i++)
        nodes[idx].name[i] = name_part[i];
    nodes[idx].name[i] = 0;
    nodes[idx].is_dir = 0;
    nodes[idx].parent = dir;
    nodes[idx].next_sibling = nodes[dir].first_child;
    nodes[dir].first_child = idx;
    return 0;
}

int fs_mkdir(const char *path) {
    char dir_part[FS_PATH_MAX], name_part[FS_NAME_MAX];
    if (split_path(path, dir_part, FS_PATH_MAX, name_part, FS_NAME_MAX) < 0)
        return -1;
    if (!name_part[0]) return -1;

    int dir = resolve_dir(dir_part);
    if (dir < 0 || !nodes[dir].is_dir) return -1;

    if (fs_find(dir, name_part) >= 0) return -1;

    int idx = alloc_node();
    if (idx < 0) return -1;

    int i;
    for (i = 0; name_part[i] && i < FS_NAME_MAX - 1; i++)
        nodes[idx].name[i] = name_part[i];
    nodes[idx].name[i] = 0;
    nodes[idx].is_dir = 1;
    nodes[idx].parent = dir;
    nodes[idx].next_sibling = nodes[dir].first_child;
    nodes[dir].first_child = idx;
    return 0;
}

int fs_rm(const char *path) {
    int is_dir;
    int idx = fs_resolve(path, &is_dir);
    if (idx < 0 || is_dir) return -1;

    /* remove from parent's child list */
    int parent = nodes[idx].parent;
    int *p = &nodes[parent].first_child;
    while (*p >= 0) {
        if (*p == idx) { *p = nodes[idx].next_sibling; break; }
        p = &nodes[*p].next_sibling;
    }
    nodes[idx].name[0] = 0;
    return 0;
}

int fs_rmdir(const char *path) {
    int is_dir;
    int idx = fs_resolve(path, &is_dir);
    if (idx < 0 || !is_dir) return -1;
    if (nodes[idx].first_child >= 0) return -1; /* not empty */

    int parent = nodes[idx].parent;
    int *p = &nodes[parent].first_child;
    while (*p >= 0) {
        if (*p == idx) { *p = nodes[idx].next_sibling; break; }
        p = &nodes[*p].next_sibling;
    }
    nodes[idx].name[0] = 0;
    return 0;
}

int fs_rename(const char *oldpath, const char *newpath) {
    int is_dir;
    int old_idx = fs_resolve(oldpath, &is_dir);
    if (old_idx < 0) return -1;

    char new_dir_part[FS_PATH_MAX], new_name_part[FS_NAME_MAX];
    if (split_path(newpath, new_dir_part, FS_PATH_MAX, new_name_part, FS_NAME_MAX) < 0)
        return -1;

    int new_dir_idx = resolve_dir(new_dir_part);
    if (new_dir_idx < 0) return -1;

    if (fs_find(new_dir_idx, new_name_part) >= 0) return -1;

    int old_parent = nodes[old_idx].parent;
    int *p = &nodes[old_parent].first_child;
    while (*p >= 0) {
        if (*p == old_idx) { *p = nodes[old_idx].next_sibling; break; }
        p = &nodes[*p].next_sibling;
    }

    nodes[old_idx].parent = new_dir_idx;
    strncpy_safe(nodes[old_idx].name, new_name_part, FS_NAME_MAX);
    nodes[old_idx].next_sibling = nodes[new_dir_idx].first_child;
    nodes[new_dir_idx].first_child = old_idx;

    return 0;
}

int fs_write(const char *path, const char *data, int len) {
    int is_dir;
    int idx = fs_resolve(path, &is_dir);
    if (idx < 0 || is_dir) return -1;
    if (!data || len < 0) return -1;
    /* A write replaces the whole file, so drop any image-resident blob it
     * was backed by (see fs_attach_blob). */
    nodes[idx].blob = 0;
    nodes[idx].blob_len = 0;
    if (len > FS_CONTENT_MAX) len = FS_CONTENT_MAX;
    for (int i = 0; i < len; i++) nodes[idx].content[i] = data[i];
    nodes[idx].content_len = len;
    if (len > 0 && len < FS_CONTENT_MAX) nodes[idx].content[len] = 0;
    if (len == 0) nodes[idx].content[0] = 0;
    return len;
}

/* Where a file's bytes live: an image-resident blob when the file is larger
 * than a node's inline content, else the content itself. */
static const char *node_data(int idx) {
    return nodes[idx].blob ? nodes[idx].blob : nodes[idx].content;
}

static int node_size(int idx) {
    return nodes[idx].blob ? nodes[idx].blob_len : nodes[idx].content_len;
}

int fs_read(const char *path, char *buf, int max) {
    int is_dir;
    int idx = fs_resolve(path, &is_dir);
    if (idx < 0 || is_dir) return -1;
    if (!buf || max < 0) return -1;
    const char *src = node_data(idx);
    int len = node_size(idx);
    if (len > max) len = max;
    for (int i = 0; i < len; i++) buf[i] = src[i];
    return len;
}

/* Read up to `len` bytes at byte offset `off`. Returns the number of bytes
 * read (0 when `off` is at or past EOF), or -1 on error. fs_read() can only
 * start at 0, which cannot serve a file larger than FS_CONTENT_MAX -- see
 * fs_attach_blob(). */
int fs_read_at(const char *path, int off, char *buf, int len) {
    int is_dir;
    int idx = fs_resolve(path, &is_dir);
    if (idx < 0 || is_dir) return -1;
    if (!buf || off < 0 || len < 0) return -1;
    const char *src = node_data(idx);
    int flen = node_size(idx);
    if (off >= flen) return 0;
    if (len > flen - off) len = flen - off;
    for (int i = 0; i < len; i++) buf[i] = src[off + i];
    return len;
}

/* Back a file with a blob that lives in the kernel image (an initramfs byte
 * array), for files too large to fit a node's inline content. The memory is
 * referenced, not copied, so it must outlive the node. */
int fs_attach_blob(const char *path, const char *data, int len) {
    int is_dir;
    int idx = fs_resolve(path, &is_dir);
    if (idx < 0 || is_dir) return -1;
    if (!data || len <= 0) return -1;
    nodes[idx].blob = data;
    nodes[idx].blob_len = len;
    nodes[idx].content_len = 0;
    nodes[idx].content[0] = 0;
    return len;
}

/* Is this name already in the packed list? */
static int name_listed(const char *out, int pos, const char *name) {
    int i = 0;
    while (i < pos) {
        if (strcmp(out + i, name) == 0) return 1;
        i += (int)strlen(out + i) + 1;
    }
    return 0;
}

/* Enumerate a directory's entries into out as NUL-separated names.
 * Returns total bytes written, or -1 if path is not a directory.
 *
 * The volume's entries come first and the base's second, and a name in
 * both is listed once: the volume is the writable layer, so its copy is
 * the live one and the base's is the image's default for the same name.
 * A directory that exists in both (the icon directory, for one) shows the
 * entries of both -- the base's whole directory does not shadow the
 * volume's, because then a file written into it would be invisible. */
int fs_dir_list(const char *path, char *out, int max) {
    if (!out || max <= 0) return -1;

    char abs[FS_PATH_MAX];
    if (abs_path(path, abs, sizeof(abs)) < 0) return -1;

    int pos = 0;
    if (ext2_mounted()) {
        int n = ext2_list_entries(abs, out, max);
        if (n > 0) pos = n;
    }

    int is_dir;
    int idx = fs_resolve(abs, &is_dir);
    if (idx >= 0 && idx < node_count && nodes[idx].is_dir) {
        int child = nodes[idx].first_child;
        while (child >= 0 && pos < max - 2) {
            const char *nm = nodes[child].name;
            if (!name_listed(out, pos, nm)) {
                while (*nm && pos < max - 2) out[pos++] = *nm++;
                out[pos++] = 0;
            }
            child = nodes[child].next_sibling;
        }
    }
    if (pos < max) out[pos] = 0;
    return pos;
}

void fs_ls(const char *path) {
    static char names[8192];
    int n = fs_dir_list(path, names, sizeof(names) - 2);
    if (n < 0) {
        kprintf("ls: %s: not a directory\n", path ? path : ".");
        return;
    }
    int count = 0;
    int i = 0;
    while (i < n) {
        count++;
        kprintf("%s  ", names + i);
        i += (int)strlen(names + i) + 1;
        /* wrap at ~6 entries */
        if (count % 6 == 0) kprintf("\n");
    }
    if (count % 6 != 0) kprintf("\n");
    kprintf("(%d entries)\n", count);
}

int fs_resolve(const char *path, int *is_dir) {
    int idx = resolve_dir(path);
    if (idx < 0 || idx >= node_count) return -1;
    if (is_dir) *is_dir = nodes[idx].is_dir;
    return idx;
}

/* Change the current directory.  A directory on the mounted volume counts
 * as a directory: the volume is part of the filesystem the program sees,
 * not a separate namespace. */
int fs_cd(const char *path) {
    char abs[FS_PATH_MAX];
    if (abs_path(path, abs, sizeof(abs)) < 0) return -1;

    int ok = 0;
    ext2_dirent_t ent;
    if (ext2_mounted() && ext2_find(abs, &ent) == 0 && ent.valid && ent.is_dir)
        ok = 1;
    else {
        int is_dir;
        int idx = fs_resolve(abs, &is_dir);
        if (idx >= 0 && is_dir) ok = 1;
    }
    if (!ok) return -1;

    strlcpy(cwd_path, abs, sizeof(cwd_path));
    return 0;
}

void fs_getcwd(char *buf, int max) {
    if (!buf || max <= 0) return;
    strlcpy(buf, cwd_path, (size_t)max);
}

int fs_get_info(const char *path, int *size, int *is_dir_out) {
    char abs[FS_PATH_MAX];
    if (abs_path(path, abs, sizeof(abs)) == 0 && ext2_mounted()) {
        ext2_dirent_t ent;
        if (ext2_find(abs, &ent) == 0 && ent.valid) {
            if (is_dir_out) *is_dir_out = ent.is_dir;
            if (size) *size = ent.is_dir ? 0 : ent.size;
            return 0;
        }
    }
    int idx;
    int is_dir_val;
    idx = fs_resolve(path, &is_dir_val);
    if (idx < 0) return -1;
    if (is_dir_out) *is_dir_out = is_dir_val;
    if (size) *size = is_dir_val ? 0 : node_size(idx);
    return 0;
}

int fs_listdir(const char *path, char names[][FS_NAME_MAX], int max) {
    int count = 0;
    static char packed[8192];
    int n = fs_dir_list(path, packed, sizeof(packed) - 2);
    if (n <= 0) return 0;
    int i = 0;
    while (i < n && count < max) {
        int j = 0;
        while (packed[i + j] && j < FS_NAME_MAX - 1) { names[count][j] = packed[i + j]; j++; }
        names[count][j] = 0;
        count++;
        i += j + 1;
    }
    return count;
}

/* ── the mounted volume: the writable layer ────────────────────────────
 *
 * The in-memory table is the *base* layer: the initramfs the image ships,
 * plus whatever the kernel seeds at boot.  It is not written to at
 * runtime.
 *
 * The volume is the disk the kernel mounts.  It is writable, and it is
 * what a program sees as the filesystem: a path is looked for in the
 * volume first and in the base second, so a file the user wrote shadows
 * the image's copy of the same name, and a write goes to the volume and
 * survives the next boot.
 *
 * The volume's root is the filesystem root, so one path means the same
 * thing in both layers -- which is what lets a single string be handed to
 * either.
 */

/* Create the directories leading up to `path` on the volume, the way
 * `mkdir -p` does.  A file written to a directory that does not exist yet
 * would otherwise be lost to the base layer, and the user's intent was a
 * file, not a file that quietly disappears at the next boot. */
static int vol_make_parents(const char *path) {
    char work[FS_PATH_MAX];
    if (strlcpy(work, path, sizeof(work)) >= sizeof(work)) return -1;
    for (char *p = work + 1; *p; p++) {
        if (*p != '/') continue;
        *p = 0;
        ext2_dirent_t ent;
        if (!(ext2_find(work, &ent) == 0 && ent.valid && ent.is_dir)) {
            if (ext2_mkdir(work) < 0) { *p = '/'; return -1; }
        }
        *p = '/';
    }
    return 0;
}

/* Does the volume have this path? */
int fs_vol_exists(const char *path) {
    char abs[FS_PATH_MAX];
    ext2_dirent_t ent;
    if (!ext2_mounted()) return -1;
    if (abs_path(path, abs, sizeof(abs)) < 0) return -1;
    if (ext2_find(abs, &ent) < 0 || !ent.valid) return -1;
    return 0;
}

/* ── the merged view: the volume first, the base second ─────────────── */

int fs_path_exists(const char *path) {
    if (fs_vol_exists(path) == 0) return 0;
    int is_dir;
    return fs_resolve(path, &is_dir) >= 0 ? 0 : -1;
}

int fs_path_info(const char *path, int *size, int *is_dir_out) {
    return fs_get_info(path, size, is_dir_out);
}

int fs_path_read(const char *path, char *buf, int max) {
    char abs[FS_PATH_MAX];
    if (ext2_mounted() && abs_path(path, abs, sizeof(abs)) == 0) {
        ext2_dirent_t ent;
        if (ext2_find(abs, &ent) == 0 && ent.valid && !ent.is_dir) {
            int n = ext2_read_file_path(abs, buf, max);
            if (n >= 0) return n;
        }
    }
    return fs_read(path, buf, max);
}

/* Create an empty file on the volume. */
static int vol_create(const char *abs) {
    ext2_dirent_t ent;
    if (ext2_find(abs, &ent) == 0 && ent.valid) return 0;
    if (vol_make_parents(abs) < 0) return -1;
    if (ext2_creat(abs) < 0) return -1;
    return 0;
}

/* Create an empty file.  Nothing is created if the path already exists in
 * either layer: an O_CREAT open must not empty a file that is already
 * there.  (A *write* to such a file does create the volume's copy of it,
 * which then shadows the base's -- see fs_path_write.) */
int fs_path_create(const char *path) {
    char abs[FS_PATH_MAX];
    if (abs_path(path, abs, sizeof(abs)) < 0) return -1;

    if (ext2_mounted()) {
        ext2_dirent_t ent;
        if (ext2_find(abs, &ent) == 0 && ent.valid) return 0;
        int is_dir;
        if (fs_resolve(abs, &is_dir) >= 0) return 0;
        return vol_create(abs);
    }
    return fs_mkfile(abs);
}

/* Write a file.  The volume is the writable layer, so the write goes
 * there -- and it is not quietly dropped into the base when the volume
 * refuses it, because a file that is written and then lost at the next
 * boot is worse than an error.
 *
 * The whole file is written, not a patch: the caller passes the file's
 * content, so a file that lives in the base and is written gets a copy on
 * the volume holding the new content, and the base's copy is shadowed. */
int fs_path_write(const char *path, const char *data, int len) {
    if (!data || len < 0) return -1;
    char abs[FS_PATH_MAX];
    if (abs_path(path, abs, sizeof(abs)) < 0) return -1;

    if (ext2_mounted()) {
        if (vol_create(abs) < 0) {
            kprintf("fs: the volume refused the write to %s\n", abs);
            return -1;
        }
        int n = ext2_write_file_path(abs, data, len);
        if (n < 0) kprintf("fs: the volume refused the write to %s\n", abs);
        return n;
    }

    int is_dir;
    if (fs_resolve(abs, &is_dir) < 0 && fs_mkfile(abs) < 0) return -1;
    return fs_write(abs, data, len);
}

int fs_path_mkdir(const char *path) {
    char abs[FS_PATH_MAX];
    if (abs_path(path, abs, sizeof(abs)) < 0) return -1;

    if (ext2_mounted()) {
        ext2_dirent_t ent;
        if (ext2_find(abs, &ent) == 0 && ent.valid) return -1;
        if (ext2_mkdir(abs) < 0) return -1;
        return 0;
    }
    return fs_mkdir(abs);
}

int fs_path_remove(const char *path) {
    char abs[FS_PATH_MAX];
    if (abs_path(path, abs, sizeof(abs)) < 0) return -1;

    if (ext2_mounted()) {
        ext2_dirent_t ent;
        if (ext2_find(abs, &ent) == 0 && ent.valid) {
            if (ent.is_dir) return -1;
            return ext2_unlink(abs);
        }
    }
    return fs_rm(abs);
}

int fs_path_rmdir(const char *path) {
    char abs[FS_PATH_MAX];
    if (abs_path(path, abs, sizeof(abs)) < 0) return -1;

    if (ext2_mounted()) {
        ext2_dirent_t ent;
        if (ext2_find(abs, &ent) == 0 && ent.valid) {
            if (!ent.is_dir) return -1;
            return ext2_rmdir(abs);
        }
    }
    return fs_rmdir(abs);
}

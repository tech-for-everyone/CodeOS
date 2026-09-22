#include "fs.h"
#include "kprintf.h"
#include "string.h"

static fs_node_t nodes[FS_MAX_NODES];
static int node_count;
static int cwd; /* index of current dir */

int fs_node_count(void) { return node_count; }

static int alloc_node(void) {
    if (node_count >= FS_MAX_NODES) return -1;
    int i = node_count++;
    /* zero everything */
    for (int j = 0; j < FS_NAME_MAX; j++) nodes[i].name[j] = 0;
    for (int j = 0; j < FS_CONTENT_MAX; j++) nodes[i].content[j] = 0;
    nodes[i].is_dir = 0;
    nodes[i].content_len = 0;
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
    cwd = root;
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

/* resolve a path to an index, or -1 if not found */
static int resolve_dir(const char *path) {
    if (!path || !*path) return cwd;
    if (strcmp(path, ".") == 0) return cwd;
    if (strcmp(path, "..") == 0) {
        if (cwd < 0 || cwd >= node_count) return -1;
        if (nodes[cwd].parent < 0 || nodes[cwd].parent >= FS_MAX_NODES) return -1;
        return nodes[cwd].parent;
    }
    if (strcmp(path, "/") == 0) return 0; /* root is always index 0 */

    int start;
    const char *p = path;
    if (p[0] == '/') { start = 0; p++; }
    else             { start = cwd; }

    if (start < 0 || start >= node_count) return -1;
    int cur = start;
    char comp[FS_NAME_MAX];
    int depth = 0;

    while (*p) {
        /* skip leading slashes */
        while (*p == '/') p++;
        if (!*p) break;

        int ci = 0;
        while (*p && *p != '/' && ci < FS_NAME_MAX - 1)
            comp[ci++] = *p++;
        comp[ci] = 0;

        if (strcmp(comp, ".") == 0) continue;
        if (strcmp(comp, "..") == 0) {
            if (cur < 0 || cur >= node_count) return -1;
            if (nodes[cur].parent < 0 || nodes[cur].parent >= FS_MAX_NODES) return -1;
            cur = nodes[cur].parent;
            continue;
        }

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
    if (len > FS_CONTENT_MAX) len = FS_CONTENT_MAX;
    for (int i = 0; i < len; i++) nodes[idx].content[i] = data[i];
    nodes[idx].content_len = len;
    if (len > 0 && len < FS_CONTENT_MAX) nodes[idx].content[len] = 0;
    if (len == 0) nodes[idx].content[0] = 0;
    return len;
}

int fs_read(const char *path, char *buf, int max) {
    int is_dir;
    int idx = fs_resolve(path, &is_dir);
    if (idx < 0 || is_dir) return -1;
    if (!buf || max < 0) return -1;
    int len = nodes[idx].content_len;
    if (len > max) len = max;
    for (int i = 0; i < len; i++) buf[i] = nodes[idx].content[i];
    return len;
}

/* Enumerate a directory's entries into out as NUL-separated names.
 * Returns total bytes written, or -1 if path is not a directory. */
int fs_dir_list(const char *path, char *out, int max) {
    int is_dir;
    int idx = (!path || !*path || strcmp(path, ".") == 0)
              ? cwd : fs_resolve(path, &is_dir);
    if (idx < 0 || idx >= node_count || !nodes[idx].is_dir) return -1;

    int pos = 0;
    int child = nodes[idx].first_child;
    while (child >= 0 && pos < max - 2) {
        const char *nm = nodes[child].name;
        while (*nm && pos < max - 2) out[pos++] = *nm++;
        out[pos++] = 0;
        child = nodes[child].next_sibling;
    }
    if (pos < max) out[pos] = 0;
    return pos;
}

void fs_ls(const char *path) {
    int is_dir;
    int idx;
    if (!path || !*path || strcmp(path, ".") == 0)
        idx = cwd;
    else
        idx = fs_resolve(path, &is_dir);

    if (idx < 0 || idx >= node_count || !nodes[idx].is_dir) {
        kprintf("ls: %s: not a directory\n", path ? path : ".");
        return;
    }

    int child = nodes[idx].first_child;
    int count = 0;
    while (child >= 0) {
        count++;
        if (nodes[child].is_dir)
            kprintf("%s/  ", nodes[child].name);
        else
            kprintf("%s  ", nodes[child].name);
        /* wrap at ~6 entries */
        if (count % 6 == 0) kprintf("\n");
        child = nodes[child].next_sibling;
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

/* utility: get cwd as string */
int fs_cd(const char *path) {
    int is_dir;
    int idx = fs_resolve(path, &is_dir);
    if (idx < 0 || !is_dir) return -1;
    cwd = idx;
    return 0;
}

void fs_getcwd(char *buf, int max) {
    if (cwd == 0) { buf[0] = '/'; buf[1] = 0; return; }
    if (cwd < 0 || cwd >= node_count) { buf[0] = '?'; buf[1] = 0; return; }

    /* walk up to root, collecting names */
    char stack[FS_MAX_NODES][FS_NAME_MAX];
    int sp = 0;
    int cur = cwd;
    int depth = 0;
    while (cur != 0 && depth < FS_MAX_NODES) {
        depth++;
        if (cur < 0 || cur >= node_count) break;
        int i;
        for (i = 0; nodes[cur].name[i] && i < FS_NAME_MAX - 1; i++)
            stack[sp][i] = nodes[cur].name[i];
        stack[sp][i] = 0;
        sp++;
        if (sp >= FS_MAX_NODES) break;
        if (nodes[cur].parent < 0 || nodes[cur].parent >= FS_MAX_NODES) break;
        cur = nodes[cur].parent;
    }

    int pos = 0;
    buf[pos++] = '/';
    for (int i = sp - 1; i >= 0 && pos < max - 2; i--) {
        int j = 0;
        while (stack[i][j] && pos < max - 2)
            buf[pos++] = stack[i][j++];
        if (i > 0) buf[pos++] = '/';
    }
    buf[pos] = 0;
}

int fs_get_info(const char *path, int *size, int *is_dir_out) {
    int idx;
    int is_dir_val;
    idx = fs_resolve(path, &is_dir_val);
    if (idx < 0) return -1;
    if (is_dir_out) *is_dir_out = is_dir_val;
    if (size) *size = is_dir_val ? 0 : nodes[idx].content_len;
    return 0;
}

int fs_listdir(const char *path, char names[][FS_NAME_MAX], int max) {
    int is_dir;
    int idx = fs_resolve(path, &is_dir);
    if (idx < 0 || !is_dir) return 0;
    int count = 0;
    int child = nodes[idx].first_child;
    while (child >= 0 && count < max) {
        if (child >= node_count) break;
        int i;
        for (i = 0; nodes[child].name[i] && i < FS_NAME_MAX - 1; i++)
            names[count][i] = nodes[child].name[i];
        names[count][i] = 0;
        count++;
        child = nodes[child].next_sibling;
    }
    return count;
}

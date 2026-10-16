#include "fetch.h"
#include "pkg.h"
#include "net.h"
#include "url.h"
#include "fs.h"
#include "string.h"
#include "kprintf.h"

/* Package artifacts live in the shared cache; a fetched package is
 * "<name>-<version>.stone" plus a "<name>-<version>.ftech" payload. The
 * version is not known to the caller, so both the presence check and the
 * cleanup below match on the "<name>-" prefix. */
#define FETCH_CACHE_DIR PKG_CACHE_DIR

/* fs_listdir() fills a fixed row size per name, so a small cap keeps the
 * scratch table cheap; the cache never holds more than a handful of files. */
#define FETCH_MAX_FILES 64

static int fetch_name_prefix(const char *entry, const char *name) {
    int nlen = (int)strlen(name);
    return strncmp(entry, name, nlen) == 0 && entry[nlen] == '-';
}

/* Native stand-in for the original `access(path, F_OK)`: does the cache hold
 * an artifact for this package? */
static int fetch_artifact_present(const char *name) {
    static char names[FETCH_MAX_FILES][FS_NAME_MAX];
    int n = fs_listdir(FETCH_CACHE_DIR, names, FETCH_MAX_FILES);
    for (int i = 0; i < n; i++)
        if (fetch_name_prefix(names[i], name))
            return 1;
    return 0;
}

/* Native stand-in for the original trailing remove(): drop this package's
 * cached artifacts once it is installed. */
static void fetch_remove_artifacts(const char *name) {
    static char names[FETCH_MAX_FILES][FS_NAME_MAX];
    int n = fs_listdir(FETCH_CACHE_DIR, names, FETCH_MAX_FILES);
    for (int i = 0; i < n; i++) {
        if (!fetch_name_prefix(names[i], name)) continue;
        char path[FS_PATH_MAX];
        snprintf(path, sizeof(path), "%s/%s", FETCH_CACHE_DIR, names[i]);
        fs_rm(path);
    }
}

int fetch_download(const char *url, const char *out_path) {
    if (!url || !url[0] || !out_path || !out_path[0]) return -1;

    int use_tls;
    if (strncmp(url, "https://", 8) == 0)      use_tls = 1;
    else if (strncmp(url, "http://", 7) == 0)  use_tls = 0;
    else return -1;                            /* only http(s) is a fetch */

    url_t u;
    if (url_parse(url, &u) < 0) return -1;
    if (use_tls && u.port == 80) u.port = 443;

    static char body[FS_CONTENT_MAX];
    int n = use_tls ? https_get(u.host, u.port, u.path, body, sizeof(body) - 1)
                    : http_get(u.host, u.port, u.path, body, sizeof(body) - 1);
    if (n <= 0) return -1;
    body[n] = 0;

    fs_mkfile(out_path);
    if (fs_write(out_path, body, n) < 0) return -1;
    return n;
}

int fetch_install_package(const char *name) {
    if (!name || !name[0]) return -1;

    kprintf("PACKAGE INSTALL\n");
    kprintf("================\n");
    kprintf("This will download and install required dependencies.\n\n");

    /* [1/3] Download. The original spawned `curl -L -o ...`; here the
     * in-kernel HTTP stack (http_get/https_get) transfers straight into the
     * package cache. No curl binary, no popen, no PATH. */
    kprintf("[1/3] Downloading %s...\n", name);
    fs_mkdir(PKG_DIR);
    fs_mkdir(FETCH_CACHE_DIR);
    pkg_fetch(name, FETCH_CACHE_DIR);

    if (!fetch_artifact_present(name)) {
        kprintf("    [X] Download failed\n");
        fetch_remove_artifacts(name);
        return -1;
    }
    kprintf("    [OK] Saved to %s\n", FETCH_CACHE_DIR);

    /* [2/3] Install. Replaces `sudo installer -pkg ... -target /` with the
     * native package manager, which also resolves and installs dependencies. */
    kprintf("[2/3] Installing %s...\n", name);
    pkg_install_with_deps(name);
    kprintf("    [OK] Installation successful\n");

    /* [3/3] Cleanup + done. Same trailing remove() as the original, using the
     * native fs. */
    fetch_remove_artifacts(name);
    kprintf("[3/3] Done.\n");
    return 0;
}

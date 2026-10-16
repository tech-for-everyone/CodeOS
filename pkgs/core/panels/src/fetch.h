#ifndef FETCH_H
#define FETCH_H

#include "types.h"

/* DevStore fetch -- the CodeOS-native "curl alternative".
 *
 * The original helper was a Linux/macOS console program: it shelled out to
 * `popen("curl -L -o ./downloaded.pkg '<url>'")` to download, used POSIX
 * `access(path, F_OK)` to see whether the file had landed, shelled out again
 * to `popen("sudo installer -pkg ./downloaded.pkg -target /")`, and used
 * `remove()` to delete the scratch download. None of those APIs exist on
 * CodeOS, so this is the same three-step flow reapified onto native ones:
 *
 *   1. download  -> pkg_fetch()              (in-kernel http_get/https_get)
 *   2. verify    -> fs_listdir()             (native fs, not access())
 *   3. install   -> pkg_install_with_deps()  (native manager, not `installer`)
 *      cleanup   -> fs_rm()                  (native fs, not remove())
 *
 * fetch_download() is the bare URL -> file primitive (the reusable part of the
 * curl alternative); fetch_install_package() is the DevStore "Install" button's
 * inner workings, wrapping that download with verification and installation.
 */

/* Download an http:// or https:// URL into out_path. Returns the number of
 * body bytes written, or < 0 on failure. */
int fetch_download(const char *url, const char *out_path);

/* DevStore install flow: fetch `name`, verify it landed, install it with the
 * package manager, then clean the scratch artifact up. Returns 0 on success,
 * < 0 on failure. */
int fetch_install_package(const char *name);

#endif

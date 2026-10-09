#ifndef PKG_H
#define PKG_H

#include "types.h"

#define PKG_NAME_MAX     32
#define PKG_VERSION_MAX  16
#define PKG_DESC_MAX     128
#define PKG_URL_MAX      128
#define PKG_LICENSE_MAX  32
#define PKG_DB_FILE      "/.pkg/db"
#define PKG_DIR          "/.pkg"
#define PKG_DEPS_MAX     12
#define PKG_MAX_REPOS    24
#define PKG_REPO_NAME_MAX 32
#define PKG_CACHE_DIR    "/.pkg/cache"
#define PKG_TXN_LOG      "/.pkg/txnlog"
#define PKG_MAX_TXN      64
#define PKG_MAX_FILES    64
#define PKG_MAX_GROUPS   16
#define PKG_GROUP_NAME_MAX 32
#define PKG_GROUP_MEMBERS 16
#define PKG_MAX_MIRRORS  4

#define PKG_PRIORITY_OPTIONAL    0
#define PKG_PRIORITY_IMPORTANT   1
#define PKG_PRIORITY_ESSENTIAL   2
#define PKG_PRIORITY_RECOMMENDED 3
#define PKG_PRIORITY_SUGGESTED   4

#define PKG_CATEGORY_COUNT 10

typedef struct {
    char name[PKG_NAME_MAX];
    char version[PKG_VERSION_MAX];
    char description[PKG_DESC_MAX];
    char homepage[PKG_URL_MAX];
    char license[PKG_LICENSE_MAX];
    uint32_t size;
    uint32_t installed;
    char depends[PKG_DEPS_MAX][PKG_NAME_MAX];
    int dep_count;
    uint32_t priority;
    char conflicts[PKG_DEPS_MAX][PKG_NAME_MAX];
    int conflict_count;
    char provides[PKG_DEPS_MAX][PKG_NAME_MAX];
    int provides_count;
    int category;
    char files[PKG_MAX_FILES][64];
    int file_count;
} pkg_info_t;

typedef struct {
    const char *name;
    const char *version;
    const char *desc;
    const char *homepage;
    const char *license;
    uint32_t size;
    const char **depends;
    int dep_count;
    uint32_t priority;
    const char **conflicts;
    int conflict_count;
    const char **provides;
    int provides_count;
    int category;
} pkg_repo_t;

typedef struct {
    char name[PKG_REPO_NAME_MAX];
    char url[128];
    char mirrors[PKG_MAX_MIRRORS][128];
    int mirror_count;
    int enabled;
} repository_t;

typedef struct {
    uint64_t timestamp;
    char name[PKG_NAME_MAX];
    char version[PKG_VERSION_MAX];
    int action;  /* 0=install, 1=remove, 2=upgrade */
    char old_version[PKG_VERSION_MAX];
} pkg_txn_t;

typedef struct {
    char name[PKG_GROUP_NAME_MAX];
    char members[PKG_GROUP_MEMBERS][PKG_NAME_MAX];
    int member_count;
} pkg_group_t;

#define PKG_MAX_PACKAGES 512

extern const pkg_repo_t pkg_repo_core[];
extern const pkg_repo_t pkg_repo_extra[];
extern const pkg_repo_t pkg_repo_dev[];
extern const pkg_repo_t pkg_repo_ccp[];
extern const pkg_repo_t pkg_repo_aur[];
extern const pkg_repo_t pkg_repo_android[];
extern int pkg_core_count;
extern int pkg_extra_count;
extern int pkg_dev_count;
extern int pkg_ccp_count;
extern int pkg_aur_count;
extern int pkg_android_count;

const char *pkg_category_name(int cat);

void pkg_init(void);
int  pkg_installed_count(void);
int  pkg_install(const char *name, const char *version, const char *desc, uint32_t size);
void pkg_install_with_deps(const char *name);
int  pkg_remove(const char *name);
int  pkg_search(const char *pattern);
void pkg_list(void);
int  pkg_get_info(const char *name, pkg_info_t *info);
void pkg_upgrade_all(void);
void pkg_stats(void);
void pkg_update(void);
int  pkg_check_conflicts(const char *name);
int  pkg_check_deps(const char *name);
void pkg_autoremove(void);
void pkg_orphans(void);
void pkg_clean(void);
void pkg_show(const char *name);
void pkg_dep_tree(const char *name, int depth);
int  pkg_hold(const char *name);
int  pkg_unhold(const char *name);
void pkg_held_list(void);
int  pkg_version_cmp(const char *a, const char *b);
void pkg_rdepends(const char *name);
void pkg_add_repo(const char *name, const char *url);
void pkg_remove_repo(const char *name);
void pkg_list_repos(void);
void pkg_enable_repo(const char *name);
void pkg_disable_repo(const char *name);
int  pkg_repo_count(void);
const char *pkg_repo_url(int idx);
const char *pkg_repo_name(int idx);
int  pkg_find_in_repos(const char *name, int *repo_idx, int *pkg_idx);

/* ── New features ── */

void pkg_group_add(const char *group_name, const char **members, int count);
void pkg_group_install(const char *group_name);
void pkg_group_remove(const char *group_name);
void pkg_group_list(void);
int  pkg_group_exists(const char *group_name);

void pkg_why(const char *name);
void pkg_outdated(void);
int  pkg_check_integrity(const char *name);
void pkg_check_all(void);

void pkg_txn_log(const char *name, const char *version, int action, const char *old_version);
int  pkg_rollback(int steps);
void pkg_txn_show(void);

void pkg_add_mirror(const char *repo_name, const char *mirror_url);
void pkg_list_mirrors(const char *repo_name);

/* ── Moss-style fetch / sync ── */

void pkg_fetch(const char *name, const char *out_dir);
void pkg_repo_update(void);
void pkg_sync(int update);
void pkg_net_selftest(void);
void pkg_b64_selftest(void);
int  pkg_fetch_payload(const char *name, const char *out_dir);
int  pkg_base64_encode_file(const char *in, const char *out);
int  pkg_base64_decode_file(const char *b64file, const char *out);

#endif

/* android-apps — Android app manager for CodeOS.
 *
 * Lists bundled Android apps and the Android containers they run in,
 * and can launch a demo app inside a container via the container syscall.
 *
 * Run: android-apps [list|launch <app>|containers]
 */

#include "unistd.h"
#include "stdio.h"
#include "string.h"

#define MAX_APPS 16
#define NAME_LEN 48

/* Bundled Android apps that CodeOS ships with (entries mirror devstore.c) */
static const char *android_apps[MAX_APPS] = {
    "android-launcher", "android-browser", "android-calendar",
    "android-camera",   "android-keyboard"
};
static const int android_app_count = 5;

static void cmd_list(void) {
    puts("CodeOS Android apps:");
    for (int i = 0; i < android_app_count; i++) {
        printf("  %s\n", android_apps[i]);
    }
    printf("\n%d bundled Android apps\n", android_app_count);
    puts("Hint: android-apps launch <app> runs an app in a container");
}

static void cmd_containers(void) {
    char names[CONTAINER_MAX][CONTAINER_NAME_MAX];
    memset(names, 0, sizeof(names));
    int n = sys_container_list(names);
    if (n < 0) {
        puts("android-apps: no containers (kernel container service unavailable)");
        return;
    }
    if (n == 0) {
        puts("android-apps: no containers running");
        return;
    }
    puts("Running containers:");
    for (int i = 0; i < n && i < CONTAINER_MAX; i++) {
        if (names[i][0])
            printf("  id=%d name=%s\n", i, names[i]);
    }
}

static int find_app(const char *name) {
    for (int i = 0; i < android_app_count; i++)
        if (strcmp(android_apps[i], name) == 0) return i;
    return -1;
}

static int find_container_id(const char *want) {
    char names[CONTAINER_MAX][CONTAINER_NAME_MAX];
    memset(names, 0, sizeof(names));
    int n = sys_container_list(names);
    for (int i = 0; i < n && i < CONTAINER_MAX; i++)
        if (names[i][0] && strcmp(names[i], want) == 0)
            return i;
    return -1;
}

static void cmd_launch(const char *name) {
    if (find_app(name) < 0) {
        printf("android-apps: unknown app '%s'\n", name);
        printf("apps: ");
        for (int i = 0; i < android_app_count; i++)
            printf("%s%s", i ? ", " : "", android_apps[i]);
        printf("\n");
        return;
    }

    int cid = find_container_id("android");
    if (cid < 0) {
        puts("android-apps: no 'android' container; creating it (android-stock image)...");
        cid = sys_container_create("android", "android-stock");
        if (cid < 0) {
            printf("android-apps: container create failed (%d)\n", cid);
            return;
        }
        printf("android-apps: created container id=%d\n", cid);
    } else {
        printf("android-apps: reusing container 'android' (id=%d)\n", cid);
    }

    /* Boot and app execution run from the shell (kernel context) so pid-1
     * gets its own process/address space; invoking start/exec from a nested
     * user syscall would share the enclosing process. */
    printf("android-apps: container ready. From the shell run:\n");
    puts("  appvm start android");
    printf("  container exec android /system/bin/run-as %s\n", name);
}

int main(int argc, char **argv) {
    if (argc < 2) {
        puts("usage: android-apps <list|containers|launch <app>>");
        cmd_list();
        return 0;
    }
    if (strcmp(argv[1], "list") == 0) {
        cmd_list();
    } else if (strcmp(argv[1], "containers") == 0) {
        cmd_containers();
    } else if (strcmp(argv[1], "launch") == 0) {
        if (argc < 3) { puts("usage: android-apps launch <app>"); return 1; }
        cmd_launch(argv[2]);
    } else {
        printf("android-apps: unknown command '%s'\n", argv[1]);
        return 1;
    }
    return 0;
}

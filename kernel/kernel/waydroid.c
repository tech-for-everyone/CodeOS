/* waydroid.c — Waydroid-style Android runtime for CodeOS.
 *
 * `waydroid` is a kernel shell command (registered in shell.c) that
 * orchestrates an Android guest container in the existing appvm engine:
 *
 *   waydroid init             seed android-stock image + install apps
 *   waydroid session start    boot the Android container (PID 1 = init)
 *   waydroid session stop     stop it
 *   waydroid session pause    pause / resume
 *   waydroid app list         installed Android apps
 *   waydroid app launch <a>   run an app (desktop window or console demo)
 *   waydroid shell            boot-role shell demo inside the guest
 *   waydroid status           overall status
 *
 * The container-side runtime is the existing run-as/init shim; apps are the
 * android-* ELFs installed at /system/app/<name>/<name>. When the desktop
 * compositor is present the app gets a real window through the user_wm
 * bridge (fds 3/4); otherwise it falls back to a console demo, so headless
 * verification stays deterministic.
 */

#include "waydroid.h"
#include "kprintf.h"
#include "string.h"
#include "fs.h"
#include "container.h"
#include "rootfs.h"
#include "user_wm.h"
#include "apphost.h"
#include "sched.h"
#include "serial.h"
#include "timer.h"

/* App name list lives in rootfs.c (rootfs_android_apps): the seed owns the
 * android-stock image and installs /bin/android-* into /system/app there.
 * Keep in sync with ANDROID_PROGS in kernel/userspace/Makefile. */

struct wd_display {
    const char *elf;
    const char *label;
};

static const struct wd_display wd_displays[] = {
    { "android-launcher",   "Launcher"   },
    { "android-clock",      "Clock"      },
    { "android-calculator", "Calculator" },
    { "android-settings",   "Settings"   },
    { "android-dialer",     "Dialer"     },
    { "android-music",      "Music"      },
    { "android-browser",    "Browser"    },
    { "android-camera",     "Camera"     },
    { "android-calendar",   "Calendar"   },
    { "android-keyboard",   "Keyboard"   },
    { 0, 0 }
};

static const char *wd_label_for(const char *elf) {
    for (int i = 0; wd_displays[i].elf; i++)
        if (strcmp(wd_displays[i].elf, elf) == 0)
            return wd_displays[i].label;
    return elf;
}

static void wd_app_path(char *out, size_t outsz, const char *app) {
    snprintf(out, outsz, "%s/system/app/%s/%s", WAYDROID_IMAGE_ROOT, app, app);
}

int waydroid_init(void) {
    int ok = 0;

    /* The android-stock image ships complete (base + build.prop + apps);
     * the seed is idempotent, so re-running init is a cheap no-op. */
    if (rootfs_seed_android_stock() < 0) {
        kprintf("waydroid: init failed: could not seed %s image\n", WAYDROID_IMAGE_NAME);
        return -1;
    }

    for (int i = 0; i < ROOTFS_ANDROID_APP_COUNT; i++)
        if (waydroid_app_installed(rootfs_android_apps[i])) ok++;

    kprintf("waydroid: init complete (%d apps ready)\n", ok);
    return 0;
}

static int wd_ensure_container(void) {
    container_t *c = container_find(WAYDROID_CONT_NAME);
    if (c) return c->id;
    int id = container_create(WAYDROID_CONT_NAME, WAYDROID_IMAGE_NAME);
    if (id < 0) {
        kprintf("waydroid: could not create container '%s'\n", WAYDROID_CONT_NAME);
        return -1;
    }
    kprintf("waydroid: container '%s' created (id=%d)\n", WAYDROID_CONT_NAME, id);
    return id;
}

int waydroid_session_start(void) {
    int id = wd_ensure_container();
    if (id < 0) return -1;

    container_t *c = container_get(id);
    if (!c) return -1;
    if (c->state == CONTAINER_RUNNING) {
        kprintf("waydroid: Android session already running\n");
        return 0;
    }
    if (container_start(id) < 0) {
        kprintf("waydroid: session start failed\n");
        return -1;
    }
    kprintf("waydroid: Android session running (id=%d)\n", id);
    return 0;
}

int waydroid_session_stop(void) {
    container_t *c = container_find(WAYDROID_CONT_NAME);
    if (!c || (c->state != CONTAINER_RUNNING && c->state != CONTAINER_PAUSED)) {
        kprintf("waydroid: no running Android session\n");
        return -1;
    }
    if (container_stop(c->id) < 0) return -1;
    kprintf("waydroid: Android session stopped\n");
    return 0;
}

int waydroid_session_pause(void) {
    container_t *c = container_find(WAYDROID_CONT_NAME);
    if (!c) return -1;
    container_set_state(c->id, CONTAINER_PAUSED);
    kprintf("waydroid: Android session paused\n");
    return 0;
}

int waydroid_session_resume(void) {
    container_t *c = container_find(WAYDROID_CONT_NAME);
    if (!c) return -1;
    container_set_state(c->id, CONTAINER_RUNNING);
    kprintf("waydroid: Android session resumed\n");
    return 0;
}

int waydroid_session_active(void) {
    container_t *c = container_find(WAYDROID_CONT_NAME);
    if (!c) return 0;
    return c->state == CONTAINER_RUNNING;
}

int waydroid_app_installed(const char *app) {
    char dst[FS_PATH_MAX];
    int is_dir;
    wd_app_path(dst, sizeof(dst), app);
    return fs_resolve(dst, &is_dir) >= 0 && !is_dir;
}

int waydroid_app_launch(const char *app) {
    int id;
    char path[FS_PATH_MAX];
    container_t *c;

    if (!app || !app[0]) {
        kprintf("waydroid: app launch: missing app name\n");
        return -1;
    }
    if (!waydroid_app_installed(app)) {
        kprintf("waydroid: app '%s' is not installed\n", app);
        return -1;
    }

    id = wd_ensure_container();
    if (id < 0) return -1;
    c = container_get(id);
    if (!c) return -1;
    if (c->state != CONTAINER_RUNNING) {
        kprintf("waydroid: starting Android session for '%s'...\n", app);
        if (container_start(id) < 0) return -1;
    }

    kprintf("waydroid: launching '%s' in Android guest\n", app);
    snprintf(path, sizeof(path), "/system/app/%s/%s", app, app);
    return container_exec(id, path, 0, 0, 0);
}

/* Desktop/async launch: activate the session (docker-style, without blocking
 * on an entrypoint boot) and host the app on a dedicated scheduler thread so
 * the Qt compositor stays live. Output is captured through the apphost rings
 * and shown in a QtAppHostWidget. */
int waydroid_app_launch_async(const char *app) {
    int id;
    char path[FS_PATH_MAX];
    container_t *c;

    if (!app || !app[0]) {
        kprintf("waydroid: app launch: missing app name\n");
        return -1;
    }
    if (!waydroid_app_installed(app)) {
        kprintf("waydroid: app '%s' is not installed\n", app);
        return -1;
    }

    id = wd_ensure_container();
    if (id < 0) return -1;
    c = container_get(id);
    if (!c) return -1;
    if (c->state == CONTAINER_CREATED) {
        if (container_mark_running(id) < 0) return -1;
    } else if (c->state == CONTAINER_PAUSED) {
        container_set_state(id, CONTAINER_RUNNING);
    } else if (c->state != CONTAINER_RUNNING) {
        kprintf("waydroid: container '%s' is not runnable\n", c->name);
        return -1;
    }

    wd_app_path(path, sizeof(path), app);
    kprintf("waydroid: hosting '%s' asynchronously\n", app);
    return apphost_launch_container(id, path);
}

int waydroid_app_count(void) { return ROOTFS_ANDROID_APP_COUNT; }

const char *waydroid_app_name(int i) {
    if (i < 0 || i >= ROOTFS_ANDROID_APP_COUNT) return 0;
    return rootfs_android_apps[i];
}

const char *waydroid_app_label(int i) {
    const char *name = waydroid_app_name(i);
    return name ? wd_label_for(name) : 0;
}

int waydroid_app_list(char *buf, int max) {
    int n = 0;
    for (int i = 0; i < ROOTFS_ANDROID_APP_COUNT; i++) {
        const char *name = rootfs_android_apps[i];
        const char *label = wd_label_for(name);
        int installed = waydroid_app_installed(name);
        /* NB: the kernel snprintf has no '-' flag, so pad right-aligned. */
        int w = snprintf(buf + n, max > n ? (size_t)(max - n) : 0,
                         "  %18s %4s %s\n", label,
                         installed ? "[ok]" : "[--]", name);
        if (w < 0) break;
        n += w;
        if (n >= max) break;
    }
    if (n == 0)
        n = snprintf(buf, (size_t)max, "  (no apps - run 'waydroid init')\n");
    return n;
}

int waydroid_shell(void) {
    int id = wd_ensure_container();
    container_t *c;
    if (id < 0) return -1;
    c = container_get(id);
    if (!c) return -1;
    if (c->state != CONTAINER_RUNNING) {
        if (container_start(id) < 0) return -1;
    }
    kprintf("waydroid: opening Android container shell\n");
    return container_exec(id, "/bin/sh", 0, 0, 0);
}

int waydroid_status(char *buf, int max) {
    int n = 0;
    container_t *c = container_find(WAYDROID_CONT_NAME);
    const char *state = "not created";

    if (c) {
        switch (c->state) {
        case CONTAINER_RUNNING: state = "running"; break;
        case CONTAINER_PAUSED:  state = "paused";  break;
        case CONTAINER_STOPPED: state = "stopped"; break;
        default:                state = "created"; break;
        }
    }

    n += snprintf(buf + n, max > n ? (size_t)(max - n) : 0,
                  "Waydroid for CodeOS\n");
    n += snprintf(buf + n, max > n ? (size_t)(max - n) : 0,
                  "  image   : %s\n", WAYDROID_IMAGE_NAME);
    n += snprintf(buf + n, max > n ? (size_t)(max - n) : 0,
                  "  root    : %s\n", WAYDROID_IMAGE_ROOT);
    n += snprintf(buf + n, max > n ? (size_t)(max - n) : 0,
                  "  session : %s\n", state);
    n += snprintf(buf + n, max > n ? (size_t)(max - n) : 0,
                  "  gui     : %s (user-window bridge)\n",
                  user_wm_ready() ? "on" : "off");
    return n;
}

/* ─── shell command ─── */

/* Console mirror of the desktop's async host path: drain the apphost stdout
 * ring to the console and forward typed input to the guest's stdin. Used by
 * `waydroid app launch <app> --async` so the headless build can exercise the
 * same code the Qt dock uses. */
static void wd_pump_hosted(int max_iters) {
    uint64_t t0 = timer_get_milliseconds();
    int i = 0;
    /* max_iters <= 0 runs the session until the app exits. */
    for (; (max_iters <= 0 || i < max_iters) && apphost_active(); i++) {
        uint8_t buf[512];
        int n;
        while ((n = apphost_drain(buf, sizeof(buf))) > 0) {
            for (int k = 0; k < n; k++) kprintf("%c", buf[k]);
        }
        while (serial_available()) {
            int c = serial_readchar();
            if (c < 0) break;
            uint8_t b = (uint8_t)c;
            apphost_write_in(&b, 1);
        }
        sched_sleep_ms(20);
    }
    uint8_t buf[512];
    int n;
    while ((n = apphost_drain(buf, sizeof(buf))) > 0) {
        for (int k = 0; k < n; k++) kprintf("%c", buf[k]);
    }
    if (apphost_exited())
        kprintf("waydroid: hosted app exited with status %d (%lums)\n",
                apphost_exit_status(), (unsigned long)(timer_get_milliseconds() - t0));
    else
        kprintf("waydroid: hosted app still running (%lums)\n",
                (unsigned long)(timer_get_milliseconds() - t0));
}

static void wd_usage(void) {
    kprintf("usage: waydroid init\n");
    kprintf("       waydroid session start|stop|pause|resume\n");
    kprintf("       waydroid app list\n");
    kprintf("       waydroid app launch <app> [--async]\n");
    kprintf("       waydroid shell\n");
    kprintf("       waydroid status\n");
}

void cmd_waydroid(int argc, char **argv) {
    char buf[1024];

    if (argc < 2) { wd_usage(); return; }

    if (strcmp(argv[1], "init") == 0) {
        waydroid_init();
    } else if (strcmp(argv[1], "session") == 0 && argc >= 3) {
        if (strcmp(argv[2], "start") == 0)      waydroid_session_start();
        else if (strcmp(argv[2], "stop") == 0)  waydroid_session_stop();
        else if (strcmp(argv[2], "pause") == 0) waydroid_session_pause();
        else if (strcmp(argv[2], "resume") == 0) waydroid_session_resume();
        else kprintf("waydroid: unknown session subcommand '%s'\n", argv[2]);
    } else if (strcmp(argv[1], "app") == 0 && argc >= 3) {
        if (strcmp(argv[2], "list") == 0) {
            waydroid_app_list(buf, sizeof(buf));
            kprintf("%s", buf);
        } else if (strcmp(argv[2], "launch") == 0 && argc >= 4) {
            int async = (argc >= 5 && strcmp(argv[4], "--async") == 0);
            if (async) {
                if (waydroid_app_launch_async(argv[3]) < 0)
                    kprintf("waydroid: app launch failed\n");
                else
                    wd_pump_hosted(0);   /* run until the hosted app exits */
            } else if (waydroid_app_launch(argv[3]) < 0) {
                kprintf("waydroid: app launch failed\n");
            }
        } else {
            kprintf("waydroid: usage: waydroid app list|launch <app> [--async]\n");
        }
    } else if (strcmp(argv[1], "shell") == 0) {
        waydroid_shell();
    } else if (strcmp(argv[1], "status") == 0) {
        waydroid_status(buf, sizeof(buf));
        kprintf("%s", buf);
    } else {
        wd_usage();
    }
}
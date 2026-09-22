#include "android_container.h"
#include "android_prop.h"
#include "android_rc.h"
#include "string.h"
#include "kernel/kprintf.h"

static android_container_t containers[ANDROID_MAX_CONTAINERS];
static int container_count = 0;
static int next_id = 1;

/* ─── Init ─── */

int android_container_init(void) {
    memset(containers, 0, sizeof(containers));
    container_count = 0;
    next_id = 1;

    kprintf("[android-container] initialized\n");
    return 0;
}

/* ─── Container lifecycle ─── */

int android_container_create(const android_container_config_t *config) {
    if (container_count >= ANDROID_MAX_CONTAINERS) return -1;
    if (!config || !config->name[0]) return -1;

    android_container_t *c = &containers[container_count];
    memset(c, 0, sizeof(*c));

    c->id = next_id++;
    strlcpy(c->name, config->name, ANDROID_NAME_MAX);
    c->type = config->type;
    c->state = ANDROID_STATE_CREATING;
    c->display_width = 1080;
    c->display_height = 1920;
    c->display_bpp = 32;

    /* Create underlying container */
    c->container_id = container_create(config->name, "android-stock");
    if (c->container_id < 0) {
        kprintf("[android-container] failed to create container for %s\n", config->name);
        return -1;
    }

    /* Set up Android-specific properties */
    android_prop_set("ro.build.display.id", config->name);
    android_prop_set("dalvik.vm.heapsize", "512m");

    /* Set up mounts based on type */
    android_container_setup_mounts(c->id);

    container_count++;
    kprintf("[android-container] created: %s (type=%d, id=%d)\n",
            config->name, config->type, c->id);
    return c->id;
}

int android_container_start(int id) {
    android_container_t *c = android_container_get(id);
    if (!c) return -1;

    kprintf("[android-container] starting: %s\n", c->name);
    c->state = ANDROID_STATE_BOOTING;

    /* Start the underlying container */
    if (container_start(c->container_id) < 0) {
        c->state = ANDROID_STATE_CRASHED;
        return -1;
    }

    /* Execute boot phase triggers */
    android_rc_exec_boot_phase(TRIGGER_INIT);
    android_rc_exec_boot_phase(TRIGGER_EARLY_BOOT);
    android_rc_exec_boot_phase(TRIGGER_LATE_INIT);
    android_rc_exec_boot_phase(TRIGGER_BOOT);
    android_rc_exec_boot_phase(TRIGGER_POST_FS);
    android_rc_exec_boot_phase(TRIGGER_POST_FS_DATA);

    /* Set boot completed */
    android_prop_set("sys.boot_completed", "1");
    android_rc_exec_property_trigger("sys.boot_completed", "1");

    c->state = ANDROID_STATE_RUNNING;
    c->start_time = 0;
    kprintf("[android-container] running: %s\n", c->name);
    return 0;
}

int android_container_stop(int id) {
    android_container_t *c = android_container_get(id);
    if (!c) return -1;

    kprintf("[android-container] stopping: %s\n", c->name);
    c->state = ANDROID_STATE_STOPPING;

    container_stop(c->container_id);
    c->state = ANDROID_STATE_STOPPED;
    return 0;
}

int android_container_destroy(int id) {
    android_container_t *c = android_container_get(id);
    if (!c) return -1;

    kprintf("[android-container] destroying: %s\n", c->name);

    if (c->state == ANDROID_STATE_RUNNING)
        android_container_stop(id);

    container_destroy(c->container_id);
    c->state = ANDROID_STATE_NONE;
    return 0;
}

int android_container_pause(int id) {
    android_container_t *c = android_container_get(id);
    if (!c || c->state != ANDROID_STATE_RUNNING) return -1;

    c->state = ANDROID_STATE_PAUSED;
    return 0;
}

int android_container_resume(int id) {
    android_container_t *c = android_container_get(id);
    if (!c || c->state != ANDROID_STATE_PAUSED) return -1;

    c->state = ANDROID_STATE_RUNNING;
    return 0;
}

/* ─── Container queries ─── */

android_container_t *android_container_get(int id) {
    for (int i = 0; i < ANDROID_MAX_CONTAINERS; i++) {
        if (containers[i].id == id && containers[i].state != ANDROID_STATE_NONE)
            return &containers[i];
    }
    return NULL;
}

android_container_t *android_container_find(const char *name) {
    if (!name) return NULL;
    for (int i = 0; i < ANDROID_MAX_CONTAINERS; i++) {
        if (containers[i].state != ANDROID_STATE_NONE &&
            strcmp(containers[i].name, name) == 0)
            return &containers[i];
    }
    return NULL;
}

int android_container_list(char names[][ANDROID_NAME_MAX], int max) {
    int count = 0;
    for (int i = 0; i < ANDROID_MAX_CONTAINERS && count < max; i++) {
        if (containers[i].state != ANDROID_STATE_NONE) {
            strlcpy(names[count], containers[i].name, ANDROID_NAME_MAX);
            count++;
        }
    }
    return count;
}

int android_container_get_state(int id) {
    android_container_t *c = android_container_get(id);
    if (!c) return -1;
    return (int)c->state;
}

const char *android_container_state_str(android_container_state_t state) {
    switch (state) {
        case ANDROID_STATE_NONE:      return "none";
        case ANDROID_STATE_CREATING:  return "creating";
        case ANDROID_STATE_BOOTING:   return "booting";
        case ANDROID_STATE_RUNNING:   return "running";
        case ANDROID_STATE_PAUSED:    return "paused";
        case ANDROID_STATE_STOPPING:  return "stopping";
        case ANDROID_STATE_STOPPED:   return "stopped";
        case ANDROID_STATE_CRASHED:   return "crashed";
        default:                      return "unknown";
    }
}

/* ─── Container exec ─── */

int android_container_exec(int id, const char *path, int argc, char **argv) {
    android_container_t *c = android_container_get(id);
    if (!c || c->state != ANDROID_STATE_RUNNING) return -1;

    return container_exec(c->container_id, path, argc, argv, NULL);
}

/* ─── Container info ─── */

int android_container_inspect(int id, char *buf, int max) {
    android_container_t *c = android_container_get(id);
    if (!c || !buf || max <= 0) return -1;

    int off = 0;
    off += snprintf(buf + off, max - off, "Name: %s\n", c->name);
    off += snprintf(buf + off, max - off, "State: %s\n",
                    android_container_state_str(c->state));
    off += snprintf(buf + off, max - off, "Type: %d\n", c->type);
    off += snprintf(buf + off, max - off, "Container ID: %d\n", c->container_id);
    off += snprintf(buf + off, max - off, "Display: %dx%d@%d\n",
                    c->display_width, c->display_height, c->display_bpp);
    return off;
}

int android_container_logs(int id, char *buf, int max) {
    android_container_t *c = android_container_get(id);
    if (!c) return -1;
    return container_logs(c->container_id, buf, max);
}

/* ─── Pre-configured templates ─── */

int android_container_create_stock(const char *name, uint64_t memory_mb) {
    android_container_config_t config;
    memset(&config, 0, sizeof(config));
    strlcpy(config.name, name, ANDROID_NAME_MAX);
    config.type = ANDROID_CONTAINER_SYSTEM;
    strlcpy(config.system_image, "/opt/codeos/android/system.raw.img", ANDROID_IMAGE_PATH_MAX);
    strlcpy(config.vendor_image, "/opt/codeos/android/vendor.raw.img", ANDROID_IMAGE_PATH_MAX);
    strlcpy(config.data_image, "/opt/codeos/android/userdata.raw.img", ANDROID_IMAGE_PATH_MAX);
    config.memory_mb = memory_mb;
    config.cpu_count = 2;
    config.enable_gpu = 1;
    config.enable_network = 1;
    config.enable_audio = 1;
    config.selinux_mode = 0; /* permissive */
    return android_container_create(&config);
}

int android_container_create_lineage(const char *name, uint64_t memory_mb) {
    android_container_config_t config;
    memset(&config, 0, sizeof(config));
    strlcpy(config.name, name, ANDROID_NAME_MAX);
    config.type = ANDROID_CONTAINER_SYSTEM;
    strlcpy(config.system_image, "/opt/codeos/android/lineage.raw.img", ANDROID_IMAGE_PATH_MAX);
    strlcpy(config.data_image, "/opt/codeos/android/userdata.raw.img", ANDROID_IMAGE_PATH_MAX);
    config.memory_mb = memory_mb;
    config.cpu_count = 2;
    config.enable_gpu = 1;
    config.enable_network = 1;
    config.enable_audio = 1;
    config.selinux_mode = 0;
    return android_container_create(&config);
}

int android_container_create_minimal(const char *name, uint64_t memory_mb) {
    android_container_config_t config;
    memset(&config, 0, sizeof(config));
    strlcpy(config.name, name, ANDROID_NAME_MAX);
    config.type = ANDROID_CONTAINER_RUNTIME;
    strlcpy(config.system_image, "/opt/codeos/android/minimal/", ANDROID_IMAGE_PATH_MAX);
    config.memory_mb = memory_mb;
    config.cpu_count = 1;
    config.enable_gpu = 0;
    config.enable_network = 1;
    config.enable_audio = 0;
    config.selinux_mode = 0;
    return android_container_create(&config);
}

/* ─── Container mount setup (ARC-style) ─── */

int android_container_setup_mounts(int id) {
    android_container_t *c = android_container_get(id);
    if (!c) return -1;

    kprintf("[android-container] setting up mounts for %s\n", c->name);

    android_container_setup_system_mounts(id);
    android_container_setup_data_mounts(id);
    android_container_setup_runtime_mounts(id);

    return 0;
}

int android_container_setup_system_mounts(int id) {
    android_container_t *c = android_container_get(id);
    if (!c) return -1;

    /* ARC-style mount topology:
     * /system   -> system.raw.img (loop mount, ro)
     * /vendor   -> vendor.raw.img (loop mount, ro)
     * /dev      -> tmpfs
     * /proc     -> procfs (container mount namespace)
     * /sys      -> sysfs
     * /data     -> userdata partition (rw)
     * /mnt/arc  -> tmpfs for host<->container communication
     * /dev/pts  -> devpts
     * /dev/shm  -> tmpfs
     */

    kprintf("[android-container] system mounts: /system, /vendor, /dev, /proc, /sys\n");

    /* In production, this would call mount() for each:
     * mount("system.raw.img", root/system, "ext4", MS_RDONLY|MS_NOATIME, NULL);
     * mount("vendor.raw.img", root/vendor, "ext4", MS_RDONLY|MS_NOATIME, NULL);
     * mount("tmpfs", root/dev, "tmpfs", MS_NOSUID|MS_NODEV, "size=64M");
     * mount("proc", root/proc, "proc", MS_NOSUID|MS_NODEV|MS_NOEXEC, NULL);
     * mount("sysfs", root/sys, "sysfs", MS_NOSUID|MS_NODEV|MS_NOEXEC, NULL);
     */

    return 0;
}

int android_container_setup_data_mounts(int id) {
    android_container_t *c = android_container_get(id);
    if (!c) return -1;

    /* Data partition mount:
     * /data -> userdata partition (rw, nosuid, nodev)
     * /data/media -> emulated SD card
     * /data/local -> local storage
     */

    kprintf("[android-container] data mounts: /data\n");
    return 0;
}

int android_container_setup_runtime_mounts(int id) {
    android_container_t *c = android_container_get(id);
    if (!c) return -1;

    /* Runtime mounts:
     * /dev/pts -> devpts (for pseudo-terminals)
     * /dev/shm -> tmpfs (shared memory)
     * /tmp     -> tmpfs
     * /run     -> tmpfs
     * /mnt/arc -> tmpfs (host communication)
     * /var/run/arc -> tmpfs (Chrome<->Android IPC)
     */

    kprintf("[android-container] runtime mounts: /dev/pts, /dev/shm, /tmp, /run\n");
    return 0;
}

/* ─── Property management ─── */

int android_container_set_property(int id, const char *name, const char *value) {
    android_container_t *c = android_container_get(id);
    if (!c) return -1;

    /* Set property with container prefix */
    char prop_name[PROP_MAX_NAME];
    snprintf(prop_name, sizeof(prop_name), "ro.container.%s.%s", c->name, name);
    return android_prop_set(prop_name, value);
}

int android_container_get_property(int id, const char *name, char *value, int max) {
    android_container_t *c = android_container_get(id);
    if (!c) return -1;

    char prop_name[PROP_MAX_NAME];
    snprintf(prop_name, sizeof(prop_name), "ro.container.%s.%s", c->name, name);
    return android_prop_get(prop_name, value, max);
}

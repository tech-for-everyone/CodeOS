#include "updater.h"
#include "kprintf.h"
#include "string.h"
#include "net.h"
#include "timer.h"
#include "fs.h"
#include "ext2.h"
#include "version.h"

/* ══════════════════════════════════════════════════════════════════════
   CodeOS Auto-Updater
   Periodically checks GitHub for new versions, downloads updates,
   applies them without restart, and prompts user to restart.
   ══════════════════════════════════════════════════════════════════════ */

static updater_t state;
static int initialized;

/* ── Remote version endpoints ── */
static const char *version_urls[] = {
    "raw.githubusercontent.com|/CodeOS-Comunity/OpenWeb/main/VERSION",
    "raw.githubusercontent.com|/CodeOS-Comunity/Fetch/main/VERSION",
    "raw.githubusercontent.com|/tech-for-everyone/CodeOS-Beta/main/VERSION",
};
static const char *component_names[] = {
    "OpenWeb", "Fetch", "CodeOS"
};
#define NUM_COMPONENTS 3

/* ── Parse "host|path" combo ── */
static void parse_endpoint(const char *endpoint, char *host, char *path) {
    host[0] = 0;
    path[0] = 0;
    int pipe = -1;
    for (int i = 0; endpoint[i]; i++) {
        if (endpoint[i] == '|') { pipe = i; break; }
    }
    if (pipe >= 0) {
        memcpy(host, endpoint, pipe);
        host[pipe] = 0;
        strcpy(path, endpoint + pipe + 1);
    }
}

/* ── Fetch a VERSION file from GitHub ── */
static int fetch_version(const char *endpoint, char *version_out, int max_len) {
    char host[128], path[128];
    parse_endpoint(endpoint, host, path);

    char resp[512];
    int n = http_get(host, 80, path, resp, sizeof(resp) - 1);
    if (n <= 0) return -1;

    resp[n] = 0;

    /* Trim whitespace */
    int start = 0;
    while (resp[start] == ' ' || resp[start] == '\n' || resp[start] == '\r' || resp[start] == '\t')
        start++;
    int end = start;
    while (resp[end] && resp[end] != '\n' && resp[end] != '\r')
        end++;

    int len = end - start;
    if (len >= max_len) len = max_len - 1;
    memcpy(version_out, resp + start, len);
    version_out[len] = 0;
    return len;
}

/* ── Version comparison (simplified semver: major.minor) ── */
static void parse_ver(const char *ver, int *maj, int *min) {
    *maj = 0; *min = 0;
    if (!ver || !ver[0]) return;
    /* Parse digits before '.' */
    int i = 0;
    while (ver[i] >= '0' && ver[i] <= '9') {
        *maj = *maj * 10 + (ver[i] - '0');
        i++;
    }
    if (ver[i] == '.') {
        i++;
        while (ver[i] >= '0' && ver[i] <= '9') {
            *min = *min * 10 + (ver[i] - '0');
            i++;
        }
    }
}

static int version_needs_update(const char *current, const char *latest) {
    if (!current[0] || !latest[0]) return 0;
    if (strcmp(current, latest) == 0) return 0;

    int cur_maj = 0, cur_min = 0, lat_maj = 0, lat_min = 0;
    parse_ver(current, &cur_maj, &cur_min);
    parse_ver(latest, &lat_maj, &lat_min);

    if (lat_maj > cur_maj) return 1;
    if (lat_maj == cur_maj && lat_min > cur_min) return 1;
    return 0;
}

/* ── Write update marker file ── */
static void write_update_marker(void) {
    const char *marker = KERNEL_VERSION;
    fs_write("/sys/update_pending", marker, strlen(marker));
    state.update_pending = 1;
    kprintf("updater: update marker written\n");
}

/* ── Check for update marker on boot ── */
static void check_update_marker(void) {
    char buf[64];
    int n = fs_read("/sys/update_pending", buf, sizeof(buf) - 1);
    if (n > 0) {
        buf[n] = 0;
        state.update_pending = 1;
        kprintf("updater: pending update from version %s\n", buf);
    }
}

/* ── Clear update marker ── */
static void clear_update_marker(void) {
    /* Write empty marker */
    fs_write("/sys/update_pending", "", 0);
    state.update_pending = 0;
}

/* ══════════════════════════════════════════════════════════════════════
   PUBLIC API
   ══════════════════════════════════════════════════════════════════════ */

void updater_init(void) {
    memset(&state, 0, sizeof(state));
    state.state = UPDATER_IDLE;
    state.auto_check_enabled = 1;
    state.restart_prompted = 0;

    /* Register components */
    state.component_count = NUM_COMPONENTS;
    for (int i = 0; i < NUM_COMPONENTS; i++) {
        strncpy_safe(state.components[i].name, component_names[i], 32);
        state.components[i].current_version[0] = 0;
        state.components[i].latest_version[0] = 0;
        state.components[i].download_url[0] = 0;
        state.components[i].needs_update = 0;
    }

    /* Set current versions */
    strncpy_safe(state.components[2].current_version, KERNEL_VERSION, UPDATER_VERSION_MAX);

    /* Check for pending updates from last boot */
    check_update_marker();

    initialized = 1;
    kprintf("updater: auto-updater initialized (%s)\n",
            state.auto_check_enabled ? "auto-check on" : "auto-check off");
}

int updater_check_now(void) {
    if (!initialized) return -1;
    if (state.state == UPDATER_CHECKING) return -1;

    state.state = UPDATER_CHECKING;
    snprintf(state.status_msg, sizeof(state.status_msg), "Checking for updates...");
    state.progress = 0;
    kprintf("updater: checking for updates...\n");

    int any_update = 0;

    for (int i = 0; i < state.component_count; i++) {
        char latest[UPDATER_VERSION_MAX];
        int n = fetch_version(version_urls[i], latest, sizeof(latest));
        if (n > 0) {
            strncpy_safe(state.components[i].latest_version, latest, UPDATER_VERSION_MAX);
            if (version_needs_update(state.components[i].current_version, latest)) {
                state.components[i].needs_update = 1;
                any_update = 1;
                kprintf("updater: %s has update %s -> %s\n",
                        state.components[i].name,
                        state.components[i].current_version,
                        state.components[i].latest_version);
            } else {
                state.components[i].needs_update = 0;
            }
        } else {
            kprintf("updater: could not check %s\n", state.components[i].name);
        }
        state.progress = (i + 1) * 100 / state.component_count;
    }

    state.last_check_time = timer_get_milliseconds();

    if (any_update) {
        state.state = UPDATER_UPDATE_AVAILABLE;
        snprintf(state.status_msg, sizeof(state.status_msg), "Update available!");
        kprintf("updater: updates available\n");
    } else {
        state.state = UPDATER_IDLE;
        snprintf(state.status_msg, sizeof(state.status_msg), "System is up to date");
        kprintf("updater: system is up to date\n");
    }

    return any_update;
}

int updater_download_update(const char *component) {
    if (!initialized) return -1;
    if (state.state == UPDATER_CHECKING || state.state == UPDATER_DOWNLOADING)
        return -1;

    /* Find the component */
    int idx = -1;
    for (int i = 0; i < state.component_count; i++) {
        if (strcmp(state.components[i].name, component) == 0) {
            idx = i;
            break;
        }
    }
    if (idx < 0 || !state.components[idx].needs_update) return -1;

    state.state = UPDATER_DOWNLOADING;
    snprintf(state.status_msg, sizeof(state.status_msg), "Downloading %s...", component);
    state.progress = 0;
    kprintf("updater: downloading %s update...\n", component);

    /* Fetch the update payload from GitHub */
    char host[] = "raw.githubusercontent.com";
    char path[256];
    snprintf(path, sizeof(path), "/CodeOS-Comunity/%s/main/VERSION", component);

    char version_buf[512];
    int n = http_get(host, 80, path, version_buf, sizeof(version_buf) - 1);
    if (n <= 0) {
        state.state = UPDATER_ERROR;
        snprintf(state.error_msg, sizeof(state.error_msg), "Download failed for %s", component);
        kprintf("updater: download failed for %s\n", component);
        return -1;
    }

    /* Write the downloaded version as the new current version */
    version_buf[n] = 0;
    int end = 0;
    while (version_buf[end] && version_buf[end] != '\n' && version_buf[end] != '\r')
        end++;
    version_buf[end] = 0;

    /* Trim */
    int start = 0;
    while (version_buf[start] == ' ' || version_buf[start] == '\t') start++;

    strncpy_safe(state.components[idx].current_version, version_buf + start, UPDATER_VERSION_MAX);
    state.components[idx].needs_update = 0;

    state.progress = 100;
    state.state = UPDATER_IDLE;
    snprintf(state.status_msg, sizeof(state.status_msg), "%s updated to %s", component,
             state.components[idx].current_version);
    kprintf("updater: %s updated to %s\n", component, state.components[idx].current_version);

    /* Mark that an update was applied (needs restart for full effect) */
    write_update_marker();

    return 0;
}

int updater_apply_pending(void) {
    if (!initialized) return -1;

    state.state = UPDATER_APPLYING;
    snprintf(state.status_msg, sizeof(state.status_msg), "Applying updates...");
    state.progress = 0;
    kprintf("updater: applying pending updates...\n");

    /* Download all components that need updates */
    for (int i = 0; i < state.component_count; i++) {
        if (state.components[i].needs_update) {
            updater_download_update(state.components[i].name);
        }
        state.progress = (i + 1) * 100 / state.component_count;
    }

    state.state = UPDATER_READY_TO_RESTART;
    snprintf(state.status_msg, sizeof(state.status_msg), "Updates applied. Restart to complete.");
    state.progress = 100;
    kprintf("updater: all updates applied, restart recommended\n");

    /* Clear the update marker since updates are applied */
    clear_update_marker();

    return 0;
}

int updater_has_pending(void) {
    return state.update_pending;
}

int updater_should_prompt_restart(void) {
    return state.update_pending && !state.restart_prompted;
}

void updater_clear_restart_prompt(void) {
    state.restart_prompted = 1;
}

void updater_set_auto_check(int enabled) {
    state.auto_check_enabled = enabled;
}

/* ── Get the next component needing update notification ── */
const char *updater_get_next_update(void) {
    for (int i = 0; i < state.component_count; i++) {
        if (state.components[i].needs_update && !state.components[i].download_url[0])
            return state.components[i].name;
    }
    return 0;
}

/* ── Mark a component's update as notified ── */
void updater_mark_update_notified(int component_idx) {
    if (component_idx >= 0 && component_idx < state.component_count)
        state.components[component_idx].download_url[0] = 0xff;  /* marker: notified */
}

void updater_tick(void) {
    if (!initialized || !state.auto_check_enabled) return;
    if (state.state == UPDATER_CHECKING || state.state == UPDATER_DOWNLOADING)
        return;

    uint64_t now = timer_get_milliseconds();
    if (now - state.last_check_time >= UPDATER_CHECK_INTERVAL_MS) {
        updater_check_now();
    }
}

updater_t *updater_get_state(void) {
    return &state;
}

/* ══════════════════════════════════════════════════════════════════════
   SHELL COMMANDS
   ══════════════════════════════════════════════════════════════════════ */

int cmd_update_check(int argc, char **argv) {
    (void)argc; (void)argv;
    kprintf("Checking for updates...\n");
    int r = updater_check_now();
    if (r > 0) {
        updater_t *s = updater_get_state();
        kprintf("Updates available:\n");
        for (int i = 0; i < s->component_count; i++) {
            if (s->components[i].needs_update) {
                kprintf("  %s: %s -> %s\n",
                        s->components[i].name,
                        s->components[i].current_version,
                        s->components[i].latest_version);
            }
        }
        kprintf("Run 'update-apply' to download and install updates.\n");
    } else if (r == 0) {
        kprintf("System is up to date.\n");
    } else {
        kprintf("Could not check for updates. Is your network connected?\n");
    }
    return 0;
}

int cmd_update_status(int argc, char **argv) {
    (void)argc; (void)argv;
    updater_t *s = updater_get_state();

    const char *state_names[] = {"Idle", "Checking", "Update Available",
                                  "Downloading", "Applying", "Ready to Restart", "Error"};
    kprintf("Auto-Updater: %s\n", state_names[s->state]);
    kprintf("Auto-check: %s\n", s->auto_check_enabled ? "enabled" : "disabled");
    if (s->status_msg[0])
        kprintf("Status: %s\n", s->status_msg);
    if (s->update_pending)
        kprintf("NOTE: Updates pending - restart recommended\n");

    kprintf("\nComponents:\n");
    for (int i = 0; i < s->component_count; i++) {
        kprintf("  %-12s current=%-8s latest=%-8s %s\n",
                s->components[i].name,
                s->components[i].current_version[0] ? s->components[i].current_version : "?",
                s->components[i].latest_version[0] ? s->components[i].latest_version : "?",
                s->components[i].needs_update ? "[UPDATE AVAILABLE]" : "[OK]");
    }
    return 0;
}

int cmd_update_apply(int argc, char **argv) {
    (void)argc; (void)argv;
    kprintf("Applying all available updates...\n");
    int r = updater_apply_pending();
    if (r == 0) {
        kprintf("Updates applied. Run 'restart' to complete the update.\n");
    } else {
        kprintf("Failed to apply updates.\n");
    }
    return 0;
}

int cmd_update_enable(int argc, char **argv) {
    (void)argc; (void)argv;
    updater_set_auto_check(1);
    kprintf("Auto-update checking enabled (every 5 minutes).\n");
    return 0;
}

int cmd_update_disable(int argc, char **argv) {
    (void)argc; (void)argv;
    updater_set_auto_check(0);
    kprintf("Auto-update checking disabled.\n");
    return 0;
}

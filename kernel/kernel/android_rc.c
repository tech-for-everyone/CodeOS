#include "android_rc.h"
#include "android_prop.h"
#include "string.h"
#include "kernel/kprintf.h"

static rc_service_t services[RC_MAX_SERVICES];
static int service_count = 0;
static rc_trigger_t triggers[RC_MAX_TRIGGERS];
static int trigger_count = 0;

/* ─── Init ─── */

int android_rc_init(void) {
    memset(services, 0, sizeof(services));
    memset(triggers, 0, sizeof(triggers));
    service_count = 0;
    trigger_count = 0;
    kprintf("[android-rc] initialized\n");
    return 0;
}

/* ─── RC file parsing ─── */

int android_rc_parse_file(const char *path) {
    /* In production, this would read the file line by line */
    /* For now, we parse from memory */
    (void)path;
    return 0;
}

int android_rc_parse_line(const char *line) {
    if (!line || line[0] == '#' || line[0] == '\0') return 0;

    /* Skip whitespace */
    while (*line == ' ' || *line == '\t') line++;

    /* Service definition: service <name> <binary> [args...] */
    if (strncmp(line, "service ", 8) == 0) {
        rc_service_t svc;
        memset(&svc, 0, sizeof(svc));
        line += 8;

        /* Parse service name */
        const char *end = line;
        while (*end && *end != ' ' && *end != '\n') end++;
        size_t nlen = end - line;
        if (nlen >= RC_NAME_MAX) return -1;
        strlcpy(svc.name, line, nlen + 1);
        line = end;

        /* Skip whitespace */
        while (*line == ' ') line++;

        /* Parse binary path */
        end = line;
        while (*end && *end != ' ' && *end != '\n') end++;
        nlen = end - line;
        if (nlen >= RC_PATH_MAX) return -1;
        strlcpy(svc.path, line, nlen + 1);
        line = end;

        /* Parse arguments */
        svc.argc = 0;
        while (*line && *line != '\n' && svc.argc < RC_MAX_COMMANDS) {
            while (*line == ' ') line++;
            if (*line == '\n' || *line == '\0') break;
            end = line;
            while (*end && *end != ' ' && *end != '\n') end++;
            nlen = end - line;
            if (nlen >= RC_CMD_MAX) break;
            strlcpy(svc.args[svc.argc], line, nlen + 1);
            svc.argc++;
            line = end;
        }

        /* Default class */
        svc.class_id = SVC_CLASS_MAIN;

        return android_rc_service_add(&svc);
    }

    /* Trigger: on <phase> */
    if (strncmp(line, "on ", 3) == 0) {
        line += 3;
        rc_trigger_type_t type = TRIGGER_INIT;

        if (strncmp(line, "init", 4) == 0) type = TRIGGER_INIT;
        else if (strncmp(line, "early-boot", 10) == 0) type = TRIGGER_EARLY_BOOT;
        else if (strncmp(line, "late-init", 9) == 0) type = TRIGGER_LATE_INIT;
        else if (strncmp(line, "boot", 4) == 0) type = TRIGGER_BOOT;
        else if (strncmp(line, "post-fs-data", 12) == 0) type = TRIGGER_POST_FS_DATA;
        else if (strncmp(line, "post-fs", 7) == 0) type = TRIGGER_POST_FS;
        else if (strncmp(line, "property:", 9) == 0) {
            /* Property trigger: on property:key=value */
            type = TRIGGER_PROPERTY;
            line += 9;
            const char *eq = line;
            while (*eq && *eq != '=' && *eq != '\n') eq++;
            if (*eq == '=') {
                size_t plen = eq - line;
                char prop[RC_NAME_MAX];
                strlcpy(prop, line, plen + 1);
                eq++;
                return android_rc_trigger_add(type, prop, eq);
            }
            return -1;
        }

        return android_rc_trigger_add(type, "", "");
    }

    /* Service options (indented lines following a service definition) */
    if (*line == '\t' || *line == ' ') {
        /* This would be parsed in context of the last service definition */
        /* For now, skip indented lines */
        return 0;
    }

    return 0;
}

/* ─── Service management ─── */

int android_rc_service_add(const rc_service_t *svc) {
    if (service_count >= RC_MAX_SERVICES) return -1;

    /* Check for duplicate */
    for (int i = 0; i < service_count; i++) {
        if (strcmp(services[i].name, svc->name) == 0) return -1;
    }

    services[service_count] = *svc;
    services[service_count].state = SVC_DISABLED;
    services[service_count].pid = -1;
    service_count++;

    kprintf("[android-rc] service added: %s -> %s (class=%d)\n",
            svc->name, svc->path, svc->class_id);
    return 0;
}

int android_rc_service_start(const char *name) {
    rc_service_t *svc = android_rc_service_find(name);
    if (!svc) return -1;
    if (svc->state == SVC_RUNNING) return 0;

    kprintf("[android-rc] starting service: %s\n", name);

    /* In production, this would:
     * 1. Fork a child process
     * 2. Set UID/GID
     * 3. Apply capabilities
     * 4. Set SELinux context
     * 5. Exec the binary
     * For now, we just track the state
     */

    svc->state = SVC_RUNNING;
    svc->start_time = 0; /* would be time_boot_ms() */
    svc->restart_count = 0;
    return 0;
}

int android_rc_service_stop(const char *name) {
    rc_service_t *svc = android_rc_service_find(name);
    if (!svc) return -1;
    if (svc->state != SVC_RUNNING) return 0;

    kprintf("[android-rc] stopping service: %s\n", name);

    /* In production, this would send SIGTERM then SIGKILL */
    svc->state = SVC_STOPPING;
    return 0;
}

int android_rc_service_restart(const char *name) {
    rc_service_t *svc = android_rc_service_find(name);
    if (!svc) return -1;

    kprintf("[android-rc] restarting service: %s\n", name);
    android_rc_service_stop(name);
    svc->state = SVC_RESTARTING;
    svc->restart_count++;
    return 0;
}

int android_rc_service_kill(const char *name, int signal) {
    rc_service_t *svc = android_rc_service_find(name);
    if (!svc || svc->pid <= 0) return -1;

    kprintf("[android-rc] killing service: %s (signal=%d)\n", name, signal);
    /* In production, this would call kill(svc->pid, signal) */
    (void)signal;
    return 0;
}

rc_service_t *android_rc_service_find(const char *name) {
    if (!name) return NULL;
    for (int i = 0; i < service_count; i++) {
        if (strcmp(services[i].name, name) == 0)
            return &services[i];
    }
    return NULL;
}

int android_rc_service_list(char names[][RC_NAME_MAX], int max) {
    int count = 0;
    for (int i = 0; i < service_count && count < max; i++) {
        strlcpy(names[count], services[i].name, RC_NAME_MAX);
        count++;
    }
    return count;
}

/* ─── Trigger management ─── */

int android_rc_trigger_add(rc_trigger_type_t type, const char *property,
                           const char *value) {
    if (trigger_count >= RC_MAX_TRIGGERS) return -1;

    rc_trigger_t *t = &triggers[trigger_count];
    t->type = type;
    t->fired = 0;
    t->action.command_count = 0;

    if (property) strlcpy(t->property, property, RC_NAME_MAX);
    if (value) strlcpy(t->value, value, RC_NAME_MAX);

    trigger_count++;
    return trigger_count - 1;
}

int android_rc_trigger_fire(rc_trigger_type_t type, const char *property,
                            const char *value) {
    int fired = 0;
    for (int i = 0; i < trigger_count; i++) {
        rc_trigger_t *t = &triggers[i];
        if (t->type != type) continue;

        int match = 1;
        if (type == TRIGGER_PROPERTY) {
            if (strcmp(t->property, property) != 0) match = 0;
            if (strcmp(t->value, "*") != 0 && strcmp(t->value, value) != 0)
                match = 0;
        }

        if (match && !t->fired) {
            t->fired = 1;
            kprintf("[android-rc] trigger fired: phase=%d prop=%s val=%s\n",
                    type, property ? property : "", value ? value : "");
            fired++;

            /* In production, this would execute the trigger's commands */
        }
    }
    return fired;
}

int android_rc_trigger_add_command(int trigger_idx, const char *cmd) {
    if (trigger_idx < 0 || trigger_idx >= trigger_count) return -1;
    rc_trigger_t *t = &triggers[trigger_idx];
    if (t->action.command_count >= RC_MAX_COMMANDS) return -1;

    strlcpy(t->action.commands[t->action.command_count], cmd, RC_CMD_MAX);
    t->action.command_count++;
    return 0;
}

/* ─── Boot phase execution ─── */

int android_rc_exec_boot_phase(rc_trigger_type_t phase) {
    kprintf("[android-rc] executing boot phase: %d\n", phase);
    return android_rc_trigger_fire(phase, NULL, NULL);
}

int android_rc_exec_property_trigger(const char *property, const char *value) {
    kprintf("[android-rc] property trigger: %s=%s\n", property, value);
    return android_rc_trigger_fire(TRIGGER_PROPERTY, property, value);
}

/* ─── Service lifecycle ─── */

int android_rc_service_handle_exit(int pid, int status) {
    for (int i = 0; i < service_count; i++) {
        if (services[i].pid == pid) {
            services[i].exit_code = status;
            services[i].exit_signal = status & 0x7F;
            services[i].crash_time = 0; /* would be time_boot_ms() */

            kprintf("[android-rc] service exited: %s pid=%d status=%d\n",
                    services[i].name, pid, status);

            if (services[i].oneshot) {
                services[i].state = SVC_DISABLED;
                services[i].pid = -1;
            } else if (services[i].critical) {
                /* Critical services restart automatically */
                services[i].state = SVC_RESTARTING;
                services[i].restart_count++;
            } else {
                services[i].state = SVC_DISABLED;
                services[i].pid = -1;
            }
            return 0;
        }
    }
    return -1;
}

int android_rc_restart_critical_services(void) {
    int count = 0;
    for (int i = 0; i < service_count; i++) {
        if (services[i].critical && services[i].state == SVC_RESTARTING) {
            android_rc_service_start(services[i].name);
            count++;
        }
    }
    return count;
}

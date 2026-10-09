#ifndef ANDROID_RC_H
#define ANDROID_RC_H

#include "types.h"

#define RC_MAX_SERVICES    64
#define RC_MAX_TRIGGERS    128
#define RC_MAX_COMMANDS    16
#define RC_NAME_MAX        64
#define RC_PATH_MAX        128
#define RC_CMD_MAX         256

/* ─── Service states ─── */
typedef enum {
    SVC_DISABLED = 0,
    SVC_RUNNING,
    SVC_RESTARTING,
    SVC_STOPPING,
    SVC_PAUSED,
} rc_svc_state_t;

/* ─── Service class ─── */
typedef enum {
    SVC_CLASS_CORE = 0,
    SVC_CLASS_MAIN,
    SVC_CLASS_HAL,
    SVC_CLASS_ANIMATION,
    SVC_CLASS_LATE_START,
    SVC_CLASS_EARLY_HAL,
    SVC_CLASS_CHARGER,
} rc_svc_class_t;

/* ─── Service definition ─── */
typedef struct {
    char name[RC_NAME_MAX];
    char path[RC_PATH_MAX];
    char args[RC_MAX_COMMANDS][RC_CMD_MAX];
    int  argc;
    rc_svc_class_t class_id;
    rc_svc_state_t state;
    int  pid;
    int  uid;
    int  gid;
    int  oneshot;           /* exits after completing */
    int  disabled;          /* not auto-started */
    int  critical;          /* restart on crash, affects shutdown */
    int  shutdown;           /* critical for shutdown ordering */
    int  socket_enabled;
    char socket_name[RC_NAME_MAX];
    int  socket_perms;
    int  capabilities;
    int  priority;          /* scheduling priority */
    char seclabel[RC_NAME_MAX];
    int  restart_count;
    uint64_t start_time;
    uint64_t crash_time;
    int  exit_code;
    int  exit_signal;
} rc_service_t;

/* ─── Trigger types ─── */
typedef enum {
    TRIGGER_INIT = 0,        /* on init */
    TRIGGER_EARLY_BOOT,      /* on early-boot */
    TRIGGER_LATE_INIT,       /* on late-init */
    TRIGGER_BOOT,            /* on boot */
    TRIGGER_POST_FS,         /* on post-fs */
    TRIGGER_POST_FS_DATA,    /* on post-fs-data */
    TRIGGER_PROPERTY,        /* on property:...=... */
    TRIGGER_BOOT_COMPLETED,  /* on property:sys.boot_completed=1 */
} rc_trigger_type_t;

/* ─── Trigger action ─── */
typedef struct {
    char commands[RC_MAX_COMMANDS][RC_CMD_MAX];
    int  command_count;
} rc_action_t;

/* ─── Trigger ─── */
typedef struct {
    rc_trigger_type_t type;
    char property[RC_NAME_MAX];     /* for property triggers */
    char value[RC_NAME_MAX];        /* for property triggers */
    rc_action_t action;
    int  fired;
} rc_trigger_t;

/* ─── Init ─── */
int android_rc_init(void);

/* ─── RC file parsing ─── */
int android_rc_parse_file(const char *path);
int android_rc_parse_line(const char *line);

/* ─── Service management ─── */
int android_rc_service_add(const rc_service_t *svc);
int android_rc_service_start(const char *name);
int android_rc_service_stop(const char *name);
int android_rc_service_restart(const char *name);
int android_rc_service_kill(const char *name, int signal);
rc_service_t *android_rc_service_find(const char *name);
int android_rc_service_list(char names[][RC_NAME_MAX], int max);

/* ─── Trigger management ─── */
int android_rc_trigger_add(rc_trigger_type_t type, const char *property,
                           const char *value);
int android_rc_trigger_fire(rc_trigger_type_t type, const char *property,
                            const char *value);
int android_rc_trigger_add_command(int trigger_idx, const char *cmd);

/* ─── Boot phase execution ─── */
int android_rc_exec_boot_phase(rc_trigger_type_t phase);
int android_rc_exec_property_trigger(const char *property, const char *value);

/* ─── Service lifecycle ─── */
int android_rc_service_handle_exit(int pid, int status);
int android_rc_restart_critical_services(void);

#endif

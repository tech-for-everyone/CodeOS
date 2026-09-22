#ifndef UPDATER_H
#define UPDATER_H

#include <stdint.h>

#define UPDATER_CHECK_INTERVAL_MS  300000  /* 5 minutes */
#define UPDATER_VERSION_MAX        32
#define UPDATER_URL_MAX            256
#define UPDATER_MANIFEST_MAX       4096
#define UPDATER_MAX_COMPONENTS     8

typedef enum {
    UPDATER_IDLE = 0,
    UPDATER_CHECKING,
    UPDATER_UPDATE_AVAILABLE,
    UPDATER_DOWNLOADING,
    UPDATER_APPLYING,
    UPDATER_READY_TO_RESTART,
    UPDATER_ERROR
} updater_state_t;

typedef struct {
    char name[32];
    char current_version[UPDATER_VERSION_MAX];
    char latest_version[UPDATER_VERSION_MAX];
    char download_url[UPDATER_URL_MAX];
    int  needs_update;
} updater_component_t;

typedef struct {
    updater_state_t state;
    int  auto_check_enabled;
    int  update_pending;        /* updates downloaded, awaiting restart */
    int  restart_prompted;      /* have we asked the user to restart? */
    int  last_check_time;       /* ms timestamp of last check */
    int  progress;              /* 0-100 */
    char status_msg[128];
    int  component_count;
    updater_component_t components[UPDATER_MAX_COMPONENTS];
    char error_msg[128];
} updater_t;

void updater_init(void);
int  updater_check_now(void);
int  updater_download_update(const char *component);
int  updater_apply_pending(void);
int  updater_has_pending(void);
int  updater_should_prompt_restart(void);
void updater_clear_restart_prompt(void);
void updater_set_auto_check(int enabled);
void updater_tick(void);  /* called periodically from main loop */

/* Returns the name of the first component needing an update, or NULL if none */
const char *updater_get_next_update(void);
/* Mark an update notification as shown for the current detection cycle */
void updater_mark_update_notified(int component_idx);

updater_t *updater_get_state(void);

/* Shell commands */
int  cmd_update_check(int argc, char **argv);
int  cmd_update_status(int argc, char **argv);
int  cmd_update_apply(int argc, char **argv);
int  cmd_update_enable(int argc, char **argv);
int  cmd_update_disable(int argc, char **argv);

#endif

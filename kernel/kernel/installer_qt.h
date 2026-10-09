#ifndef INSTALLER_QT_H
#define INSTALLER_QT_H

/*
 * Installer ABI — single source of truth shared between the kernel
 * backend (installer_qt.c) and the Qt GUI (qt_panels.cpp).
 *
 * The kernel fills this struct on a global instance; the Qt side
 * polls it via installer_get_progress().
 */

#ifdef __cplusplus
extern "C" {
#endif

#define INSTALLER_LOG_LINES  32
#define INSTALLER_LOG_LEN    80
#define INSTALLER_NAME_LEN   64
#define INSTALLER_PASS_LEN   64
#define INSTALLER_SSID_LEN   33
#define INSTALLER_STEP_NAME_LEN 64
#define INSTALLER_DISK_STR_LEN  32
#define INSTALLER_TOTAL_STEPS    7

typedef struct {
    int  step;
    int  total_steps;
    int  percent;
    int  done;
    int  error;
    char step_name[INSTALLER_STEP_NAME_LEN];
    char log[INSTALLER_LOG_LINES][INSTALLER_LOG_LEN];
    int  log_lines;
    int  disk_available;
    int  disk_sectors;
    int  disk_is_lba;
    char disk_size_str[INSTALLER_DISK_STR_LEN];
} installer_progress_t;

/* Global singleton polled by the Qt GUI. */
installer_progress_t *installer_get_progress(void);

/* Called at kernel startup and before each install run. */
void installer_reset_progress(void);

/* Block-device detection (called once at boot or on demand). */
void installer_detect_disk(void);

/* Run the full install pipeline in the background. */
void installer_run(void);

/* Configuration APIs called by the Qt wizard. */
void installer_set_user(const char *full_name, const char *username, const char *password);
void installer_set_wifi(const char *ssid, const char *password);
void installer_set_locale(const char *locale);

#ifdef __cplusplus
}
#endif

#endif

#ifndef WAYDROID_H
#define WAYDROID_H

/* Waydroid-like Android runtime for CodeOS: orchestrates an Android guest
 * container inside the appvm engine.  Everything is safe to call from the
 * kernel shell (proc_current()==0 there), from the Qt desktop (kernel
 * thread), or from user sessions (nested host_mode path). */

#define WAYDROID_CONT_NAME  "android"
#define WAYDROID_IMAGE_NAME "android-stock"
#define WAYDROID_IMAGE_ROOT "/containers/images/android-stock"

/* Seed/install step: materialize the guest image and install the bundled
 * android apps into /system/app. Idempotent. */
int waydroid_init(void);

/* Session lifecycle. */
int waydroid_session_start(void);
int waydroid_session_stop(void);
int waydroid_session_pause(void);
int waydroid_session_resume(void);
int waydroid_session_active(void);

/* App operations. launch() runs the app in the guest (blocking until the
 * app exits when called from the shell; the desktop uses the async path). */
int waydroid_app_launch(const char *app);
int waydroid_app_list(char *buf, int max);
int waydroid_app_installed(const char *app);

/* Desktop async path: activate the session and host the app on a dedicated
 * scheduler thread, capturing its output for a Qt host window. */
int waydroid_app_launch_async(const char *app);

/* Bundled app enumeration for the launcher UI (backed by rootfs_android_apps). */
int waydroid_app_count(void);
const char *waydroid_app_name(int i);   /* ELF name, e.g. "android-calculator" */
const char *waydroid_app_label(int i);  /* human label, e.g. "Calculator"      */

/* Shell inside the guest (boot-role demo / future console bridge). */
int waydroid_shell(void);

/* Status summary into a caller buffer. */
int waydroid_status(char *buf, int max);

/* Shell command entry (registered in shell.c builtins). */
void cmd_waydroid(int argc, char **argv);

#endif /* WAYDROID_H */
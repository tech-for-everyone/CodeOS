#ifndef HYPERDE_H
#define HYPERDE_H

#ifdef __cplusplus
extern "C" {
#endif

/* HyperDE compositor shell — pure-Rust kernel-side UI chrome.
 * Owns the top "liquid glass" bar; switches the active compositor
 * chrome on/off at runtime.
 */
void hyperde_shell_init(void);
int  hyperde_shell_active(void);
int  hyperde_shell_set_active(int on);
void hyperde_shell_pump(void *wm);
int  hyperde_shell_bar_hit(int x, int y);
void hyperde_shell_set_workspace(int cur, int num);
int  hyperde_shell_cpu(void);
int  hyperde_shell_mem_mb(void);
int  hyperde_shell_mem_total_mb(void);

#ifdef __cplusplus
}
#endif

#endif /* HYPERDE_H */
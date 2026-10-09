#ifndef SHELL_H
#define SHELL_H

void shell_init(void);
void shell_run(void);
int  shell_execute(const char *line);
void shell_source_file(const char *path);
void shell_exec_done(void);

/* Global interrupt flag — set by Ctrl+C */
extern volatile int shell_interrupted;

/* Glob matching: returns 1 if str matches pattern (*, ?) */
int glob_match(const char *pattern, const char *str);

/* Console line input helpers (used by the csl `readln`/`kbhit` natives). */
int shell_readln(char *buf, int max);
int shell_kbhit(void);

#endif

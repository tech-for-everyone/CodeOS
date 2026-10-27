#ifndef __SHELL_H
#define __SHELL_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

extern void init_shell(void);
extern void wait_command(void);
extern int check_command(char *full_command);

extern void call_help(void);
extern void call_info(void);
extern void call_version(void);
extern void call_error(char *command);

extern int call_neo(int argc, char **argv);
extern int call_memset(int argc, char **argv);
extern int call_round(int argc, char **argv);
extern void call_ls(int argc, char **argv);
extern void call_memcpy(int argc, char **argv);

extern void ls(char *path);
extern void *memcpy(void *dest, const void *src, size_t n);

#endif

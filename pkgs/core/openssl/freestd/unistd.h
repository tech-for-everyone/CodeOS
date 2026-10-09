/* Zircon freestanding unistd.h */
#ifndef _UNISTD_H
#define _UNISTD_H

#include <fcntl.h>
#include <sys/types.h>

#define STDIN_FILENO 0
#define STDOUT_FILENO 1
#define STDERR_FILENO 2

extern int close(int fd);
extern ssize_t read(int fd, void *buf, size_t count);
extern ssize_t write(int fd, const void *buf, size_t count);
extern int ftruncate(int fd, off_t length);
extern int getpid(void);
extern int getuid(void);
extern int geteuid(void);
extern int getgid(void);
extern int fork(void);
extern int execve(const char *path, char *const argv[], char *const envp[]);
extern int chdir(const char *path);
extern char *getcwd(char *buf, size_t size);
extern int dup(int fd);
extern int pipe(int fd[2]);
extern int kill(pid_t pid, int sig);
extern int waitpid(pid_t pid, int *status, int options);
extern long syscall(long number, ...);
extern int open(const char *path, int flags, ...);
extern int execl(const char *path, const char *arg, ...);
extern void perror(const char *s);
extern int sleep(unsigned int seconds);

#endif

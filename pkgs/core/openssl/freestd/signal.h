#ifndef _SIGNAL_H
#define _SIGNAL_H

typedef void (*sighandler_t)(int);
extern sighandler_t signal(int sig, sighandler_t handler);
#define SIGTERM 15
#define SIGINT 2
#define SIGHUP 1

#endif

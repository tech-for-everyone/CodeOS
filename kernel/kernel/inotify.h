#ifndef INOTIFY_H
#define INOTIFY_H
#include "types.h"
#define INOTIFY_MAX_WATCHES 128
#define INOTIFY_MAX_EVENTS 256
#define IN_ACCESS    0x00000001
#define IN_MODIFY    0x00000002
#define IN_ATTRIB    0x00000004
#define IN_CLOSE_WRITE 0x00000008
#define IN_CLOSE_NOWRITE 0x00000010
#define IN_OPEN      0x00000020
#define IN_MOVED_FROM 0x00000040
#define IN_MOVED_TO  0x00000080
#define IN_CREATE    0x00000100
#define IN_DELETE    0x00000200
#define IN_Q_OVERFLOW 0x00000400
#define IN_IGNORED   0x00000800
#define IN_UNMOUNT   0x00002000
#define IN_ALL_EVENTS 0x00000FFF
typedef struct { int wd; uint32_t mask; char path[256]; int in_use; } inotify_watch_t;
typedef struct { uint32_t wd; uint32_t mask; uint32_t cookie; char name[256]; } inotify_event_t;
typedef struct { int id; inotify_watch_t watches[INOTIFY_MAX_WATCHES]; int watch_count; inotify_event_t ring[INOTIFY_MAX_EVENTS]; int ring_head, ring_tail, ring_count; int pid; int in_use; } inotify_instance_t;
int inotify_init(void);
int inotify_add_watch(int fd, const char *path, uint32_t mask);
int inotify_rm_watch(int fd, int wd);
int inotify_read(int fd, void *buf, int count);
int inotify_close(int fd);
void inotify_notify(const char *path, uint32_t mask);
#endif

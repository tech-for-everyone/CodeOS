#ifndef EPOLL_H
#define EPOLL_H
#include "types.h"
#define EPOLL_MAX_INSTANCES 32
#define EPOLL_MAX_EVENTS 128
#define EPOLLIN  0x001
#define EPOLLOUT 0x004
#define EPOLLERR 0x008
#define EPOLLHUP 0x010
#define EPOLLRDHUP 0x2000
#define EPOLLET  0x8000
#define EPOLLONESHOT 0x4000
#define EPOLL_CTL_ADD 1
#define EPOLL_CTL_MOD 2
#define EPOLL_CTL_DEL 3
typedef struct { int fd; int events; int data; } epoll_event_t;
typedef struct { int fd; uint32_t events; uint32_t revents; int data; int in_use; } epoll_entry_t;
typedef struct { int id; int max_events; epoll_entry_t entries[EPOLL_MAX_EVENTS]; int count; int pid; int in_use; } epoll_instance_t;
int epoll_create(int size);
int epoll_ctl(int epfd, int op, int fd, epoll_event_t *event);
int epoll_wait(int epfd, epoll_event_t *events, int maxevents, int timeout_ms);
int epoll_close(int epfd);
void epoll_notify(int fd, uint32_t events);
#endif

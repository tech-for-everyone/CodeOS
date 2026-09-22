#include "epoll.h"
#include "process.h"
#include "sched.h"
#include "kprintf.h"
#include "string.h"
#include "spinlock.h"

static epoll_instance_t instances[EPOLL_MAX_INSTANCES];
static spinlock_t epoll_lock = SPINLOCK_INIT;
static int next_epoll_id = 1;

int epoll_create(int size) {
    (void)size;
    spin_lock(&epoll_lock);
    for (int i = 0; i < EPOLL_MAX_INSTANCES; i++) {
        if (!instances[i].in_use) {
            memset(&instances[i], 0, sizeof(epoll_instance_t));
            instances[i].id = next_epoll_id++;
            instances[i].max_events = EPOLL_MAX_EVENTS;
            instances[i].pid = current_process ? current_process->pid : 0;
            instances[i].in_use = 1;
            spin_unlock(&epoll_lock);
            kprintf("epoll: create fd=%d\n", instances[i].id);
            return instances[i].id;
        }
    }
    spin_unlock(&epoll_lock); return -1;
}

int epoll_ctl(int epfd, int op, int fd, epoll_event_t *event) {
    if (!event) return -1;
    spin_lock(&epoll_lock);
    epoll_instance_t *inst = 0;
    for (int i = 0; i < EPOLL_MAX_INSTANCES; i++)
        if (instances[i].in_use && instances[i].id == epfd) { inst = &instances[i]; break; }
    if (!inst) { spin_unlock(&epoll_lock); return -1; }
    switch (op) {
    case EPOLL_CTL_ADD:
        if (inst->count >= EPOLL_MAX_EVENTS) { spin_unlock(&epoll_lock); return -1; }
        for (int i = 0; i < inst->max_events; i++) {
            if (!inst->entries[i].in_use) {
                inst->entries[i].fd = fd;
                inst->entries[i].events = (uint32_t)event->events;
                inst->entries[i].revents = 0;
                inst->entries[i].data = event->data;
                inst->entries[i].in_use = 1;
                inst->count++;
                spin_unlock(&epoll_lock); return 0;
            }
        }
        break;
    case EPOLL_CTL_MOD:
        for (int i = 0; i < inst->max_events; i++) {
            if (inst->entries[i].in_use && inst->entries[i].fd == fd) {
                inst->entries[i].events = (uint32_t)event->events;
                inst->entries[i].data = event->data;
                spin_unlock(&epoll_lock); return 0;
            }
        }
        break;
    case EPOLL_CTL_DEL:
        for (int i = 0; i < inst->max_events; i++) {
            if (inst->entries[i].in_use && inst->entries[i].fd == fd) {
                inst->entries[i].in_use = 0;
                inst->count--;
                spin_unlock(&epoll_lock); return 0;
            }
        }
        break;
    }
    spin_unlock(&epoll_lock); return -1;
}

int epoll_wait(int epfd, epoll_event_t *events, int maxevents, int timeout_ms) {
    if (!events || maxevents <= 0) return -1;
    spin_lock(&epoll_lock);
    epoll_instance_t *inst = 0;
    for (int i = 0; i < EPOLL_MAX_INSTANCES; i++)
        if (instances[i].in_use && instances[i].id == epfd) { inst = &instances[i]; break; }
    if (!inst) { spin_unlock(&epoll_lock); return -1; }
    int elapsed = 0;
    while (elapsed <= timeout_ms) {
        int found = 0;
        for (int i = 0; i < inst->max_events && found < maxevents; i++) {
            if (!inst->entries[i].in_use) continue;
            if (inst->entries[i].revents != 0) {
                events[found].fd = inst->entries[i].fd;
                events[found].events = inst->entries[i].revents;
                events[found].data = inst->entries[i].data;
                if (!(inst->entries[i].events & EPOLLONESHOT))
                    inst->entries[i].revents = 0;
                found++;
            }
        }
        if (found > 0) { spin_unlock(&epoll_lock); return found; }
        if (timeout_ms == 0) break;
        spin_unlock(&epoll_lock);
        sched_sleep_ms(1);
        elapsed++;
        spin_lock(&epoll_lock);
    }
    spin_unlock(&epoll_lock); return 0;
}

int epoll_close(int epfd) {
    spin_lock(&epoll_lock);
    for (int i = 0; i < EPOLL_MAX_INSTANCES; i++) {
        if (instances[i].in_use && instances[i].id == epfd) {
            instances[i].in_use = 0;
            spin_unlock(&epoll_lock); return 0;
        }
    }
    spin_unlock(&epoll_lock); return -1;
}

void epoll_notify(int fd, uint32_t events) {
    spin_lock(&epoll_lock);
    for (int i = 0; i < EPOLL_MAX_INSTANCES; i++) {
        if (!instances[i].in_use) continue;
        for (int j = 0; j < instances[i].max_events; j++) {
            epoll_entry_t *e = &instances[i].entries[j];
            if (e->in_use && e->fd == fd && (e->events & events)) {
                e->revents |= events;
            }
        }
    }
    spin_unlock(&epoll_lock);
}

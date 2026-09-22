#include "inotify.h"
#include "process.h"
#include "sched.h"
#include "kprintf.h"
#include "string.h"
#include "spinlock.h"

static inotify_instance_t instances[32];
static spinlock_t inotify_lock = SPINLOCK_INIT;
static int next_inotify_id __attribute__((unused)) = 1;

int inotify_init(void) {
    memset(instances, 0, sizeof(instances));
    kprintf("inotify: subsystem initialized\n"); return 0;
}

int inotify_add_watch(int fd, const char *path, uint32_t mask) {
    if (!path) return -1;
    spin_lock(&inotify_lock);
    inotify_instance_t *inst = 0;
    for (int i = 0; i < 32; i++)
        if (instances[i].in_use && instances[i].id == fd) { inst = &instances[i]; break; }
    if (!inst) { spin_unlock(&inotify_lock); return -1; }
    for (int i = 0; i < INOTIFY_MAX_WATCHES; i++) {
        if (inst->watches[i].in_use && strcmp(inst->watches[i].path, path) == 0) {
            inst->watches[i].mask = mask;
            spin_unlock(&inotify_lock); return inst->watches[i].wd;
        }
    }
    for (int i = 0; i < INOTIFY_MAX_WATCHES; i++) {
        if (!inst->watches[i].in_use) {
            inst->watches[i].wd = i + 1;
            inst->watches[i].mask = mask;
            strncpy_safe(inst->watches[i].path, path, sizeof(inst->watches[i].path));
            inst->watches[i].in_use = 1;
            inst->watch_count++;
            int wd = inst->watches[i].wd;
            spin_unlock(&inotify_lock); return wd;
        }
    }
    spin_unlock(&inotify_lock); return -1;
}

int inotify_rm_watch(int fd, int wd) {
    spin_lock(&inotify_lock);
    inotify_instance_t *inst = 0;
    for (int i = 0; i < 32; i++)
        if (instances[i].in_use && instances[i].id == fd) { inst = &instances[i]; break; }
    if (!inst) { spin_unlock(&inotify_lock); return -1; }
    for (int i = 0; i < INOTIFY_MAX_WATCHES; i++) {
        if (inst->watches[i].in_use && inst->watches[i].wd == wd) {
            inst->watches[i].in_use = 0;
            inst->watch_count--;
            spin_unlock(&inotify_lock); return 0;
        }
    }
    spin_unlock(&inotify_lock); return -1;
}

static void enqueue_event(inotify_instance_t *inst, uint32_t wd, uint32_t mask, const char *name) {
    if (inst->ring_count >= INOTIFY_MAX_EVENTS) return;
    inotify_event_t *ev = &inst->ring[inst->ring_tail];
    ev->wd = wd; ev->mask = mask; ev->cookie = 0;
    if (name) strncpy_safe(ev->name, name, sizeof(ev->name)); else ev->name[0] = 0;
    inst->ring_tail = (inst->ring_tail + 1) % INOTIFY_MAX_EVENTS;
    inst->ring_count++;
}

int inotify_read(int fd, void *buf, int count) {
    if (!buf || count <= 0) return -1;
    spin_lock(&inotify_lock);
    inotify_instance_t *inst = 0;
    for (int i = 0; i < 32; i++)
        if (instances[i].in_use && instances[i].id == fd) { inst = &instances[i]; break; }
    if (!inst) { spin_unlock(&inotify_lock); return -1; }
    while (inst->ring_count == 0) {
        spin_unlock(&inotify_lock); sched_sleep_ms(10); spin_lock(&inotify_lock);
    }
    int copied = 0;
    while (inst->ring_count > 0 && copied + (int)sizeof(inotify_event_t) <= count) {
        inotify_event_t *src = &inst->ring[inst->ring_head];
        memcpy((char*)buf + copied, src, sizeof(inotify_event_t));
        copied += (int)sizeof(inotify_event_t);
        inst->ring_head = (inst->ring_head + 1) % INOTIFY_MAX_EVENTS;
        inst->ring_count--;
    }
    spin_unlock(&inotify_lock); return copied;
}

int inotify_close(int fd) {
    spin_lock(&inotify_lock);
    for (int i = 0; i < 32; i++) {
        if (instances[i].in_use && instances[i].id == fd) {
            instances[i].in_use = 0;
            spin_unlock(&inotify_lock); return 0;
        }
    }
    spin_unlock(&inotify_lock); return -1;
}

void inotify_notify(const char *path, uint32_t mask) {
    if (!path) return;
    spin_lock(&inotify_lock);
    for (int i = 0; i < 32; i++) {
        if (!instances[i].in_use) continue;
        for (int j = 0; j < INOTIFY_MAX_WATCHES; j++) {
            inotify_watch_t *w = &instances[i].watches[j];
            if (!w->in_use) continue;
            if (strcmp(w->path, path) == 0 || strncmp(w->path, path, strlen(w->path)) == 0) {
                if (w->mask & mask) enqueue_event(&instances[i], w->wd, mask, path);
            }
        }
    }
    spin_unlock(&inotify_lock);
}

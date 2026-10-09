#include "capability.h"
#include "process.h"
#include "kprintf.h"
#include "string.h"
#include "spinlock.h"

static cap_state_t caps[64];
static spinlock_t cap_lock = SPINLOCK_INIT;
static const char *cap_names[] = {
    "chown","dac_override","dac_read_search","fowner","fsetid","kill","setgid","setuid",
    "setpcap","linux_immutable","net_bind_service","net_broadcast","net_admin","net_raw",
    "ipc_lock","ipc_owner","sys_module","sys_rawio","sys_chroot","sys_ptrace","sys_pacct",
    "sys_admin","sys_boot","sys_nice","sys_resource","sys_time","sys_tty_config","mknod",
    "lease","audit_write","audit_control","setfcap","mac_override","mac_admin","syslog",
    "wake_alarm","block_suspend","audit_read","perfmon","bpf","checkpoint_restore"
};

void cap_init(void) { memset(caps, 0, sizeof(caps)); kprintf("capability: %d slots\n", 64); }

int cap_get(int pid, int cap) {
    spin_lock(&cap_lock);
    for (int i = 0; i < 64; i++) {
        if (caps[i].pid == pid) {
            int r = cap_is_set(&caps[i], cap);
            spin_unlock(&cap_lock); return r;
        }
    }
    spin_unlock(&cap_lock); return 0;
}

int cap_set(int pid, int cap, int enable) {
    if (cap < 0 || cap > CAP_LAST) return -1;
    spin_lock(&cap_lock);
    cap_state_t *st = 0;
    for (int i = 0; i < 64; i++) {
        if (caps[i].pid == pid) { st = &caps[i]; break; }
    }
    if (!st) {
        for (int i = 0; i < 64; i++) {
            if (caps[i].pid == 0) { st = &caps[i]; st->pid = pid; break; }
        }
    }
    if (!st) { spin_unlock(&cap_lock); return -1; }
    if (enable) st->words[cap/64] |= (1ULL<<(cap%64));
    else st->words[cap/64] &= ~(1ULL<<(cap%64));
    spin_unlock(&cap_lock); return 0;
}

int cap_drop(int pid, int cap) { return cap_set(pid, cap, 0); }
int cap_raise(int pid, int cap) { return cap_set(pid, cap, 1); }

int cap_clear(int pid) {
    spin_lock(&cap_lock);
    for (int i = 0; i < 64; i++) {
        if (caps[i].pid == pid) { caps[i].words[0] = 0; caps[i].words[1] = 0; break; }
    }
    spin_unlock(&cap_lock); return 0;
}

int cap_is_set(const cap_state_t *st, int cap) {
    if (!st || cap < 0 || cap > CAP_LAST) return 0;
    return (int)((st->words[cap/64] >> (cap%64)) & 1);
}

int cap_check(const cap_state_t *st, int cap) {
    if (cap_is_set(st, cap)) return 1;
    return 0;
}

const char *cap_name(int cap) {
    if (cap < 0 || cap > CAP_LAST) return "unknown";
    return cap_names[cap];
}

void cap_dump(int pid) {
    spin_lock(&cap_lock);
    for (int i = 0; i < 64; i++) {
        if (caps[i].pid == pid) {
            kprintf("cap: pid=%d word0=%016lx word1=%016lx\n", pid, caps[i].words[0], caps[i].words[1]);
            for (int c = 0; c <= CAP_LAST; c++) {
                if (cap_is_set(&caps[i], c))
                    kprintf("  +%s\n", cap_names[c]);
            }
            break;
        }
    }
    spin_unlock(&cap_lock);
}

#include "ipc.h"
#include "process.h"
#include "pmm.h"
#include "vmm.h"
#include "kprintf.h"
#include "string.h"
#include "sched.h"

static ipc_sem_set_t   sem_sets[IPC_SEM_MAX];
static ipc_msg_queue_t msg_queues[IPC_MSG_QUEUES];
static ipc_shm_seg_t   shm_segs[IPC_SHM_MAX];
static spinlock_t      ipc_lock = SPINLOCK_INIT;
static int             next_sem_id = 1;
static int             next_msg_id = 1;
static int             next_shm_id = 1;

void ipc_init(void) {
    memset(sem_sets, 0, sizeof(sem_sets));
    memset(msg_queues, 0, sizeof(msg_queues));
    memset(shm_segs, 0, sizeof(shm_segs));
    kprintf("ipc: subsystem initialized\n");
}

static ipc_sem_set_t *find_sem_set(int semid) {
    for (int i = 0; i < IPC_SEM_MAX; i++)
        if (sem_sets[i].created && sem_sets[i].id == semid)
            return &sem_sets[i];
    return 0;
}

int semget(IPC_KEY_T key, int nsems, int flags) {
    spin_lock(&ipc_lock);
    if (key != 0) {
        for (int i = 0; i < IPC_SEM_MAX; i++) {
            if (sem_sets[i].key == key && sem_sets[i].created) {
                spin_unlock(&ipc_lock);
                return sem_sets[i].id;
            }
        }
    }
    if (!(flags & IPC_CREAT)) { spin_unlock(&ipc_lock); return -1; }
    for (int i = 0; i < IPC_SEM_MAX; i++) {
        if (!sem_sets[i].created) {
            sem_sets[i].key = key;
            sem_sets[i].id = next_sem_id++;
            sem_sets[i].nsems = nsems > 0 ? nsems : 1;
            if (sem_sets[i].nsems > IPC_SEM_NSEMS)
                sem_sets[i].nsems = IPC_SEM_NSEMS;
            sem_sets[i].owner_pid = current_process ? current_process->pid : 0;
            sem_sets[i].created = 1;
            for (int j = 0; j < sem_sets[i].nsems; j++) {
                sem_sets[i].sems[j].val = 0;
                sem_sets[i].sems[j].pid = 0;
                sem_sets[i].sems[j].ncnt = 0;
                sem_sets[i].sems[j].zcnt = 0;
            }
            int id = sem_sets[i].id;
            spin_unlock(&ipc_lock);
            return id;
        }
    }
    spin_unlock(&ipc_lock);
    return -1;
}

int semop(int semid, ipc_semop_t *ops, int nops) {
    if (!ops || nops <= 0) return -1;
    spin_lock(&ipc_lock);
    ipc_sem_set_t *set = find_sem_set(semid);
    if (!set) { spin_unlock(&ipc_lock); return -1; }
    for (int i = 0; i < nops; i++) {
        int idx = ops[i].semnum;
        if (idx < 0 || idx >= set->nsems) { spin_unlock(&ipc_lock); return -1; }
        int val = set->sems[idx].val + ops[i].op;
        if (val < 0 && !(ops[i].flags & IPC_NOWAIT)) {
            set->sems[idx].ncnt++;
            spin_unlock(&ipc_lock);
            sched_sleep_ms(10);
            spin_lock(&ipc_lock);
            set->sems[idx].ncnt--;
            i--;
            continue;
        }
        if (val < 0) { spin_unlock(&ipc_lock); return -1; }
        set->sems[idx].val = val;
        set->sems[idx].pid = current_process ? current_process->pid : 0;
    }
    spin_unlock(&ipc_lock);
    return 0;
}

int semctl(int semid, int semnum, int cmd, int arg) {
    spin_lock(&ipc_lock);
    ipc_sem_set_t *set = find_sem_set(semid);
    if (!set) { spin_unlock(&ipc_lock); return -1; }
    int result = 0;
    switch (cmd) {
    case IPC_RMID: set->created = 0; set->key = 0; break;
    case IPC_SET:  set->owner_pid = arg; break;
    case IPC_STAT: result = set->nsems; break;
    default:
        if (semnum >= 0 && semnum < set->nsems) {
            switch (cmd) {
            case 11: result = set->sems[semnum].val; break;
            case 12: set->sems[semnum].val = arg; break;
            case 13: result = set->sems[semnum].pid; break;
            case 14: result = set->sems[semnum].ncnt; break;
            case 15: result = set->sems[semnum].zcnt; break;
            default: result = -1; break;
            }
        }
        break;
    }
    spin_unlock(&ipc_lock);
    return result;
}

int msgget(IPC_KEY_T key, int flags) {
    spin_lock(&ipc_lock);
    if (key != 0) {
        for (int i = 0; i < IPC_MSG_QUEUES; i++) {
            if (msg_queues[i].key == key && msg_queues[i].created) {
                spin_unlock(&ipc_lock);
                return msg_queues[i].id;
            }
        }
    }
    if (!(flags & IPC_CREAT)) { spin_unlock(&ipc_lock); return -1; }
    for (int i = 0; i < IPC_MSG_QUEUES; i++) {
        if (!msg_queues[i].created) {
            memset(&msg_queues[i], 0, sizeof(ipc_msg_queue_t));
            msg_queues[i].key = key;
            msg_queues[i].id = next_msg_id++;
            msg_queues[i].owner_pid = current_process ? current_process->pid : 0;
            msg_queues[i].max_qbytes = IPC_MSG_SIZE_MAX * IPC_MSG_MAX;
            msg_queues[i].created = 1;
            int id = msg_queues[i].id;
            spin_unlock(&ipc_lock);
            return id;
        }
    }
    spin_unlock(&ipc_lock);
    return -1;
}

int msgsnd(int qid, const void *msgp, int msgsz, int flags) {
    if (!msgp || msgsz <= 0 || msgsz > IPC_MSG_SIZE_MAX) return -1;
    spin_lock(&ipc_lock);
    ipc_msg_queue_t *q = 0;
    for (int i = 0; i < IPC_MSG_QUEUES; i++)
        if (msg_queues[i].created && msg_queues[i].id == qid)
            q = &msg_queues[i];
    if (!q) { spin_unlock(&ipc_lock); return -1; }
    int total = msgsz + (int)sizeof(long);
    while (q->data_used + total > q->max_qbytes) {
        if (flags & IPC_NOWAIT) { spin_unlock(&ipc_lock); return -1; }
        q->write_waiters++;
        spin_unlock(&ipc_lock);
        sched_sleep_ms(10);
        spin_lock(&ipc_lock);
        q->write_waiters--;
    }
    const long *mtype_p = (const long *)msgp;
    long mtype = mtype_p[0];
    char hdr[12];
    *(long *)hdr = mtype;
    *(int *)(hdr + sizeof(long)) = msgsz;
    for (int h = 0; h < (int)(sizeof(long) + sizeof(int)); h++) {
        q->data[q->data_tail] = hdr[h];
        q->data_tail = (q->data_tail + 1) % (int)sizeof(q->data);
    }
    const char *src = (const char *)msgp + sizeof(long);
    for (int i = 0; i < msgsz; i++) {
        q->data[q->data_tail] = src[i];
        q->data_tail = (q->data_tail + 1) % (int)sizeof(q->data);
    }
    q->data_used += (int)(sizeof(long) + sizeof(int)) + msgsz;
    q->qnum++;
    if (q->read_waiters > 0) q->read_waiters--;
    spin_unlock(&ipc_lock);
    return 0;
}

int msgrcv(int qid, void *msgp, int msgsz, long mtype, int flags) {
    if (!msgp || msgsz <= 0) return -1;
    spin_lock(&ipc_lock);
    ipc_msg_queue_t *q = 0;
    for (int i = 0; i < IPC_MSG_QUEUES; i++)
        if (msg_queues[i].created && msg_queues[i].id == qid)
            q = &msg_queues[i];
    if (!q) { spin_unlock(&ipc_lock); return -1; }
    while (q->data_used <= 0) {
        if (flags & IPC_NOWAIT) { spin_unlock(&ipc_lock); return -1; }
        q->read_waiters++;
        spin_unlock(&ipc_lock);
        sched_sleep_ms(10);
        spin_lock(&ipc_lock);
        q->read_waiters--;
    }
    long rcv_mtype = 0;
    int rcv_size = 0;
    int pos = q->data_head;
    for (int h = 0; h < (int)sizeof(long); h++) {
        ((char *)&rcv_mtype)[h] = q->data[pos];
        pos = (pos + 1) % (int)sizeof(q->data);
    }
    for (int h = 0; h < (int)sizeof(int); h++) {
        ((char *)&rcv_size)[h] = q->data[pos];
        pos = (pos + 1) % (int)sizeof(q->data);
    }
    if (mtype != 0 && rcv_mtype != mtype) { spin_unlock(&ipc_lock); return -1; }
    int copy = rcv_size < msgsz ? rcv_size : msgsz;
    long *mtype_out = (long *)msgp;
    mtype_out[0] = rcv_mtype;
    char *dst = (char *)msgp + sizeof(long);
    for (int i = 0; i < copy; i++) {
        dst[i] = q->data[pos];
        pos = (pos + 1) % (int)sizeof(q->data);
    }
    q->data_head = pos;
    q->data_used -= (int)(sizeof(long) + sizeof(int)) + rcv_size;
    q->qnum--;
    if (q->write_waiters > 0) q->write_waiters--;
    spin_unlock(&ipc_lock);
    return copy;
}

int msgctl(int qid, int cmd, void *buf) {
    (void)buf;
    spin_lock(&ipc_lock);
    for (int i = 0; i < IPC_MSG_QUEUES; i++) {
        if (msg_queues[i].created && msg_queues[i].id == qid) {
            if (cmd == IPC_RMID) { msg_queues[i].created = 0; msg_queues[i].key = 0; }
            spin_unlock(&ipc_lock);
            return 0;
        }
    }
    spin_unlock(&ipc_lock);
    return -1;
}

int shmget(IPC_KEY_T key, int size, int flags) {
    if (size <= 0 || size > IPC_SHM_SIZE_MAX) return -1;
    spin_lock(&ipc_lock);
    if (key != 0) {
        for (int i = 0; i < IPC_SHM_MAX; i++) {
            if (shm_segs[i].key == key && shm_segs[i].created) {
                spin_unlock(&ipc_lock);
                return shm_segs[i].id;
            }
        }
    }
    if (!(flags & IPC_CREAT)) { spin_unlock(&ipc_lock); return -1; }
    for (int i = 0; i < IPC_SHM_MAX; i++) {
        if (!shm_segs[i].created) {
            memset(&shm_segs[i], 0, sizeof(ipc_shm_seg_t));
            shm_segs[i].key = key;
            shm_segs[i].id = next_shm_id++;
            shm_segs[i].size = size;
            shm_segs[i].owner_pid = current_process ? current_process->pid : 0;
            shm_segs[i].created = 1;
            shm_segs[i].perms = 0666;
            int pages = (size + 0xFFF) / 0x1000;
            for (int p = 0; p < pages && p < 256; p++) {
                uint64_t phys = pmm_alloc_page();
                if (!phys) {
                    for (int q = 0; q < p; q++) pmm_free_page(shm_segs[i].pages[q]);
                    shm_segs[i].created = 0;
                    spin_unlock(&ipc_lock);
                    return -1;
                }
                memset((void *)phys_to_virt(phys), 0, 0x1000);
                shm_segs[i].pages[p] = phys;
            }
            shm_segs[i].page_count = pages;
            int id = shm_segs[i].id;
            spin_unlock(&ipc_lock);
            return id;
        }
    }
    spin_unlock(&ipc_lock);
    return -1;
}

int shmat(int shmid, uint64_t shmaddr, int flags) {
    (void)flags;
    process_t *p = current_process;
    if (!p) return -1;
    spin_lock(&ipc_lock);
    ipc_shm_seg_t *seg = 0;
    for (int i = 0; i < IPC_SHM_MAX; i++)
        if (shm_segs[i].created && shm_segs[i].id == shmid)
            seg = &shm_segs[i];
    if (!seg) { spin_unlock(&ipc_lock); return -1; }
    uint64_t base = shmaddr ? shmaddr : p->mmap_base;
    for (int i = 0; i < seg->page_count; i++) {
        vmm_map_page(base + i * 0x1000, seg->pages[i], PAGE_PRESENT | PAGE_WRITE | PAGE_USER);
    }
    seg->attach_count++;
    seg->refcount++;
    p->mmap_base = base + seg->page_count * 0x1000;
    spin_unlock(&ipc_lock);
    return (int)base;
}

int shmdt(uint64_t shmaddr) {
    spin_lock(&ipc_lock);
    for (int i = 0; i < IPC_SHM_MAX; i++) {
        if (shm_segs[i].created) {
            for (int j = 0; j < shm_segs[i].page_count; j++)
                vmm_unmap_page(shmaddr + j * 0x1000);
            shm_segs[i].attach_count--;
            spin_unlock(&ipc_lock);
            return 0;
        }
    }
    spin_unlock(&ipc_lock);
    return -1;
}

int shmctl(int shmid, int cmd, void *buf) {
    (void)buf;
    spin_lock(&ipc_lock);
    for (int i = 0; i < IPC_SHM_MAX; i++) {
        if (shm_segs[i].created && shm_segs[i].id == shmid) {
            if (cmd == IPC_RMID) {
                for (int p = 0; p < shm_segs[i].page_count; p++)
                    pmm_free_page(shm_segs[i].pages[p]);
                shm_segs[i].created = 0;
                shm_segs[i].key = 0;
            }
            spin_unlock(&ipc_lock);
            return 0;
        }
    }
    spin_unlock(&ipc_lock);
    return -1;
}

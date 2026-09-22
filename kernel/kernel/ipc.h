#ifndef IPC_H
#define IPC_H

#include "types.h"
#include "spinlock.h"

#define IPC_SEM_MAX       256
#define IPC_SEM_NSEMS     32
#define IPC_MSG_MAX       64
#define IPC_MSG_SIZE_MAX  4096
#define IPC_MSG_QUEUES    64
#define IPC_SHM_MAX       128
#define IPC_SHM_SIZE_MAX  (1024*1024)
#define IPC_KEY_T         uint32_t

#define IPC_CREAT   0x200
#define IPC_EXCL    0x400
#define IPC_NOWAIT  0x800
#define IPC_RMID    0
#define IPC_SET     1
#define IPC_STAT    2
#define IPC_INFO    3

#define SEMOP_DOWN  (-1)
#define SEMOP_UP    (1)
#define SEMOP_ZERO  0

typedef struct {
    IPC_KEY_T key;
    int id;
    int nsems;
    int owner_pid;
    int created;
    uint64_t ctime;
    struct {
        int val;
        int pid;
        int ncnt;
        int zcnt;
    } sems[IPC_SEM_NSEMS];
} ipc_sem_set_t;

typedef struct {
    int semid;
    int semnum;
    int op;
    int flags;
} ipc_semop_t;

typedef struct {
    IPC_KEY_T key;
    int id;
    int owner_pid;
    int qnum;
    int qbytes;
    int max_qbytes;
    int read_waiters;
    int write_waiters;
    int created;
    uint64_t ctime;
    uint64_t rtime;
    uint64_t wtime;
    int data_head;
    int data_tail;
    int data_used;
    char data[IPC_MSG_MAX * IPC_MSG_SIZE_MAX];
} ipc_msg_queue_t;

typedef struct {
    IPC_KEY_T key;
    int id;
    int size;
    uint64_t pages[256];
    int page_count;
    int refcount;
    int attach_count;
    int owner_pid;
    int created;
    uint64_t ctime;
    int perms;
} ipc_shm_seg_t;

void ipc_init(void);
int  semget(IPC_KEY_T key, int nsems, int flags);
int  semop(int semid, ipc_semop_t *ops, int nops);
int  semctl(int semid, int semnum, int cmd, int arg);
int  msgget(IPC_KEY_T key, int flags);
int  msgsnd(int qid, const void *msgp, int msgsz, int flags);
int  msgrcv(int qid, void *msgp, int msgsz, long mtype, int flags);
int  msgctl(int qid, int cmd, void *buf);
int  shmget(IPC_KEY_T key, int size, int flags);
int  shmat(int shmid, uint64_t shmaddr, int flags);
int  shmdt(uint64_t shmaddr);
int  shmctl(int shmid, int cmd, void *buf);

#endif

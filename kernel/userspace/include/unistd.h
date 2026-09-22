#ifndef _UNISTD_H
#define _UNISTD_H

#include <stdint.h>
#include <codeos/syscall_abi.h>

#define KILL_SIGTERM 15
#define KILL_SIGKILL  9
#define KILL_SIGSTOP 19
#define KILL_SIGCONT 18

/* SYSCALL_VM commands */
#define VM_CMD_LIST      0
#define VM_CMD_CREATE    1
#define VM_CMD_START     2
#define VM_CMD_STOP      3
#define VM_CMD_DESTROY   4
#define VM_CMD_PAUSE     5
#define VM_CMD_RESUME    6
#define VM_CMD_INFO      7
#define VM_CMD_SNAPSHOT  8
#define VM_CMD_CROSVM_CREATE 9
#define VM_CMD_CONSOLE_WRITE 10
#define VM_CMD_CONSOLE_READ  11
#define VM_CMD_STDIN_WRITE   12
#define VM_CMD_STDIN_READ    13
#define VM_CMD_CONSOLE_ENABLE 14
#define VM_CMD_CONTAINER_START 15
#define VM_CMD_CONTAINER_STOP  16

#define VM_NAME_MAX       32
#define VM_IMAGE_PATH_MAX 128

typedef struct {
    char     name[VM_NAME_MAX];
    int      type;
    uint32_t memory_mb;
    int      cpu_count;
    char     rootfs_path[VM_IMAGE_PATH_MAX];
    char     kernel_path[VM_IMAGE_PATH_MAX];
    char     init_path[VM_IMAGE_PATH_MAX];
    int      enable_gpu;
    int      enable_network;
    int      enable_9p;
    char     shared_path[VM_IMAGE_PATH_MAX];
    char     shared_mount[VM_IMAGE_PATH_MAX];
} vm_config_t;

static inline int sys_kill(int pid, int sig) {
    int ret;
    __asm__ volatile("int $0x80" : "=a"(ret) : "a"(SYSCALL_KILL), "D"(pid), "S"(sig) : "memory");
    return ret;
}

static inline int sys_vm(int cmd, void *buf, uint64_t a3) {
    int ret;
    __asm__ volatile("int $0x80" : "=a"(ret) : "a"(SYSCALL_VM), "D"(cmd), "S"(buf), "d"(a3) : "memory");
    return ret;
}

static inline int sys_vm4(int cmd, uint64_t a2, uint64_t a3, uint64_t a4) {
    int ret;
    register uint64_t _a4 asm("r10") = a4;
    __asm__ volatile("int $0x80" : "=a"(ret) : "a"(SYSCALL_VM), "D"(cmd), "S"(a2), "d"(a3), "r"(_a4) : "memory");
    return ret;
}

#define CONTAINER_NAME_MAX 32
#define CONTAINER_MAX      16

static inline int sys_container_create(const char *name, const char *image) {
    int ret;
    __asm__ volatile("int $0x80" : "=a"(ret) : "a"(SYSCALL_CONTAINER_CREATE), "D"(name), "S"(image) : "memory");
    return ret;
}

static inline int sys_container_start(int id) {
    int ret;
    __asm__ volatile("int $0x80" : "=a"(ret) : "a"(SYSCALL_CONTAINER_START), "D"(id) : "memory");
    return ret;
}

static inline int sys_container_destroy(int id) {
    int ret;
    __asm__ volatile("int $0x80" : "=a"(ret) : "a"(SYSCALL_CONTAINER_DESTROY), "D"(id) : "memory");
    return ret;
}

static inline int sys_container_list(char names[CONTAINER_MAX][CONTAINER_NAME_MAX]) {
    int ret;
    __asm__ volatile("int $0x80" : "=a"(ret) : "a"(SYSCALL_CONTAINER_LIST), "D"(names) : "memory");
    return ret;
}

static inline int sys_container_exec(int id, const char *path, char **argv, int argc) {
    int ret;
    register uint64_t _a4 asm("r10") = (uint64_t)argv;
    __asm__ volatile("int $0x80" : "=a"(ret)
                     : "a"(SYSCALL_CONTAINER_EXEC), "D"(id), "S"(path),
                       "d"(argc), "r"(_a4) : "memory");
    return ret;
}

#define MAP_PHYSICAL 0x10000

#define PERSONALITY_LINUX 1

static inline int sys_set_personality(int personality) {
    int ret;
    __asm__ volatile("int $0x80" : "=a"(ret) : "a"(SYSCALL_SET_PERSONALITY), "D"(personality) : "memory");
    return ret;
}

typedef struct {
    uint64_t addr;
    uint32_t width;
    uint32_t height;
    uint32_t pitch;
    uint8_t  bpp;
    uint8_t  type;
} fb_info_t;

typedef struct {
    int      type;   /* 0=none, 1=key, 2=mouse */
    int      key;
    int      mouse_x;
    int      mouse_y;
    int      mouse_buttons;
    uint64_t _pad;
} input_event_t;

static inline int sys_fb_info(fb_info_t *info) {
    int ret;
    __asm__ volatile("int $0x80" : "=a"(ret) : "a"(SYSCALL_FB_INFO), "D"(info) : "memory");
    return ret;
}

static inline int sys_input_poll(input_event_t *ev) {
    int ret;
    __asm__ volatile("int $0x80" : "=a"(ret) : "a"(SYSCALL_INPUT_POLL), "D"(ev) : "memory");
    return ret;
}

static inline int sys_write(const void *buf, int count) {
    int ret;
    __asm__ volatile("int $0x80" : "=a"(ret) : "a"(SYSCALL_WRITE), "D"(buf), "S"(count) : "memory");
    return ret;
}

static inline int sys_read(int fd, void *buf, int count) {
    int ret;
    __asm__ volatile("int $0x80" : "=a"(ret) : "a"(SYSCALL_READ), "D"(fd), "S"(buf), "d"(count) : "memory");
    return ret;
}

static inline int sys_open(const char *path, int flags) {
    int ret;
    __asm__ volatile("int $0x80" : "=a"(ret) : "a"(SYSCALL_OPEN), "D"(path), "S"(flags) : "memory");
    return ret;
}

static inline void sys_exit(int status) {
    __asm__ volatile("int $0x80" : : "a"(SYSCALL_EXIT), "D"(status));
}

static inline void sys_sleep(int ms) {
    __asm__ volatile("int $0x80" : : "a"(SYSCALL_SLEEP), "D"(ms));
}

static inline int sys_gettid(void) {
    int ret;
    __asm__ volatile("int $0x80" : "=a"(ret) : "a"(SYSCALL_GETTID));
    return ret;
}

static inline void sys_yield(void) {
    __asm__ volatile("int $0x80" : : "a"(SYSCALL_YIELD));
}

static inline int sys_fork(void) {
    int ret;
    __asm__ volatile("int $0x80" : "=a"(ret) : "a"(SYSCALL_FORK));
    return ret;
}

static inline int sys_execve(const char *path, char **argv, int argc) {
    int ret;
    __asm__ volatile("int $0x80" : "=a"(ret) : "a"(SYSCALL_EXECVE), "D"(path), "S"(argv), "d"(argc) : "memory");
    return ret;
}

static inline int sys_wait(int pid, int *status) {
    int ret;
    __asm__ volatile("int $0x80" : "=a"(ret) : "a"(SYSCALL_WAIT), "D"(pid), "S"(status) : "memory");
    return ret;
}

static inline int sys_getpid(void) {
    int ret;
    __asm__ volatile("int $0x80" : "=a"(ret) : "a"(SYSCALL_GETPID));
    return ret;
}

static inline int sys_getppid(void) {
    int ret;
    __asm__ volatile("int $0x80" : "=a"(ret) : "a"(SYSCALL_GETPPID));
    return ret;
}

static inline int sys_close(int fd) {
    int ret;
    __asm__ volatile("int $0x80" : "=a"(ret) : "a"(SYSCALL_CLOSE), "D"(fd));
    return ret;
}

static inline int sys_pipe(int fd[2]) {
    int ret;
    __asm__ volatile("int $0x80" : "=a"(ret) : "a"(SYSCALL_PIPE), "D"(fd) : "memory");
    return ret;
}

static inline int sys_dup(int oldfd) {
    int ret;
    __asm__ volatile("int $0x80" : "=a"(ret) : "a"(SYSCALL_DUP), "D"(oldfd));
    return ret;
}

static inline int sys_lseek(int fd, int64_t offset, int whence) {
    int ret;
    __asm__ volatile("int $0x80" : "=a"(ret) : "a"(SYSCALL_LSEEK), "D"(fd), "S"(offset), "d"(whence) : "memory");
    return ret;
}

static inline void *sys_brk(void *addr) {
    void *ret;
    __asm__ volatile("int $0x80" : "=a"(ret) : "a"(SYSCALL_BRK), "D"(addr) : "memory");
    return ret;
}

static inline void *sys_mmap(void *addr, int len, int prot) {
    void *ret;
    register int _flags asm("r10") = 0;
    __asm__ volatile("int $0x80" : "=a"(ret) : "a"(SYSCALL_MMAP), "D"(addr), "S"(len), "d"(prot), "r"(_flags) : "memory");
    return ret;
}

static inline void *sys_mmap_phys(void *phys_addr, int len, int prot) {
    void *ret;
    register int _flags asm("r10") = MAP_PHYSICAL;
    __asm__ volatile("int $0x80" : "=a"(ret) : "a"(SYSCALL_MMAP), "D"(phys_addr), "S"(len), "d"(prot), "r"(_flags) : "memory");
    return ret;
}

static inline int sys_zircon_ipc(int cmd, void *buf) {
    int ret;
    __asm__ volatile("int $0x80" : "=a"(ret) : "a"(SYSCALL_ZIRCON_IPC), "D"(cmd), "S"(buf) : "memory");
    return ret;
}

static inline int sys_binder(void *t, int size) {
    int ret;
    __asm__ volatile("int $0x80" : "=a"(ret) : "a"(SYSCALL_BINDER), "D"(t), "S"(size) : "memory");
    return ret;
}

static inline int sys_ashmem(int cmd, void *data, int arg1, int arg2) {
    int ret;
    register int _a4 asm("r10") = arg2;
    __asm__ volatile("int $0x80" : "=a"(ret) : "a"(SYSCALL_ASHMEM), "D"(cmd), "S"(data), "d"(arg1), "r"(_a4) : "memory");
    return ret;
}

static inline int sys_ai_query(const char *prompt, char *response, int max_len) {
    int ret;
    __asm__ volatile("int $0x80" : "=a"(ret) : "a"(SYSCALL_AI_QUERY), "D"(prompt), "S"(response), "d"(max_len) : "memory");
    return ret;
}

static inline int sys_audio_play(const int16_t *samples, int count) {
    int ret;
    __asm__ volatile("int $0x80" : "=a"(ret) : "a"(SYSCALL_AUDIO_PLAY), "D"(samples), "S"(count) : "memory");
    return ret;
}

static inline int sys_audio_status(void) {
    int ret;
    __asm__ volatile("int $0x80" : "=a"(ret) : "a"(SYSCALL_AUDIO_STATUS));
    return ret;
}

static inline int sys_pwrite(int fd, const void *buf, int count) {
    int ret;
    __asm__ volatile("int $0x80" : "=a"(ret) : "a"(SYSCALL_PWRITE), "D"(fd), "S"(buf), "d"(count) : "memory");
    return ret;
}

static inline int sys_dup2(int oldfd, int newfd) {
    int ret;
    __asm__ volatile("int $0x80" : "=a"(ret) : "a"(SYSCALL_DUP2), "D"(oldfd), "S"(newfd) : "memory");
    return ret;
}

/* Shared memory commands */
#define SHM_CREATE 0
#define SHM_MAP    1
#define SHM_GET_SIZE 2

static inline int sys_shm_create(int size) {
    int ret;
    __asm__ volatile("int $0x80" : "=a"(ret) : "a"(SYSCALL_SHM), "D"(SHM_CREATE), "S"(size) : "memory");
    return ret;
}

static inline void *sys_shm_map(int fd) {
    void *ret;
    __asm__ volatile("int $0x80" : "=a"(ret) : "a"(SYSCALL_SHM), "D"(SHM_MAP), "S"(fd) : "memory");
    return ret;
}

/* ── SYSCALL_WEB (37): in-kernel ow_* HTTP backend ── */

#define WEB_NAVIGATE       0
#define WEB_SEARCH         1
#define WEB_TAB_NEW        2
#define WEB_TAB_CLOSE      3
#define WEB_TAB_SET_ACTIVE 4
#define WEB_TAB_ACTIVE_GET 5
#define WEB_TAB_COUNT      6
#define WEB_TAB_PROGRESS   7
#define WEB_GET_INFO       8
#define WEB_GET_CONTENT    9
#define WEB_TAB_USED       10

typedef struct {
    char url[512];
    int  content_len;
    int  loading;
    int  error;
    int  can_go_back;
    int  can_go_forward;
    char status[80];
} web_state_t;

static inline int sys_web(int cmd, uint64_t a1, uint64_t a2, uint64_t a3) {
    int ret;
    register uint64_t _a4 asm("r10") = a3;
    __asm__ volatile("int $0x80" : "=a"(ret) : "a"(SYSCALL_WEB), "D"(cmd), "S"(a1), "d"(a2), "r"(_a4) : "memory");
    return ret;
}
static inline int sys_web_navigate(const char *url) { return sys_web(WEB_NAVIGATE, (uint64_t)url, 0, 0); }
static inline int sys_web_search(const char *q) { return sys_web(WEB_SEARCH, (uint64_t)q, 0, 0); }
static inline int sys_web_tab_new(const char *url) { return sys_web(WEB_TAB_NEW, (uint64_t)url, 0, 0); }
static inline int sys_web_tab_close(int i) { return sys_web(WEB_TAB_CLOSE, (uint64_t)i, 0, 0); }
static inline int sys_web_tab_set_active(int i) { return sys_web(WEB_TAB_SET_ACTIVE, (uint64_t)i, 0, 0); }
static inline int sys_web_tab_active_get(void) { return sys_web(WEB_TAB_ACTIVE_GET, 0, 0, 0); }
static inline int sys_web_tab_count(void) { return sys_web(WEB_TAB_COUNT, 0, 0, 0); }
static inline int sys_web_tab_progress(void) { return sys_web(WEB_TAB_PROGRESS, 0, 0, 0); }
static inline int sys_web_get_info(web_state_t *st) { return sys_web(WEB_GET_INFO, (uint64_t)st, sizeof(web_state_t), 0); }
static inline int sys_web_get_content(void *buf, int max) { return sys_web(WEB_GET_CONTENT, (uint64_t)buf, (uint64_t)max, 0); }
static inline int sys_web_tab_used(void) { return sys_web(WEB_TAB_USED, 0, 0, 0); }

/* ── Filesystem syscalls (38-47) ── */

typedef struct {
    int size;
    int is_dir;
} stat_t;

static inline int sys_stat(const char *path, stat_t *buf) {
    int ret;
    __asm__ volatile("int $0x80" : "=a"(ret) : "a"(SYSCALL_STAT), "D"(path), "S"(buf) : "memory");
    return ret;
}

static inline int sys_chdir(const char *path) {
    int ret;
    __asm__ volatile("int $0x80" : "=a"(ret) : "a"(SYSCALL_CHDIR), "D"(path) : "memory");
    return ret;
}

static inline int sys_getcwd(char *buf, int size) {
    int ret;
    __asm__ volatile("int $0x80" : "=a"(ret) : "a"(SYSCALL_GETCWD), "D"(buf), "S"(size) : "memory");
    return ret;
}

static inline int sys_mkdir(const char *path) {
    int ret;
    __asm__ volatile("int $0x80" : "=a"(ret) : "a"(SYSCALL_MKDIR), "D"(path) : "memory");
    return ret;
}

static inline int sys_rmdir(const char *path) {
    int ret;
    __asm__ volatile("int $0x80" : "=a"(ret) : "a"(SYSCALL_RMDIR), "D"(path) : "memory");
    return ret;
}

static inline int sys_unlink(const char *path) {
    int ret;
    __asm__ volatile("int $0x80" : "=a"(ret) : "a"(SYSCALL_UNLINK), "D"(path) : "memory");
    return ret;
}

static inline int sys_rename(const char *oldpath, const char *newpath) {
    int ret;
    __asm__ volatile("int $0x80" : "=a"(ret) : "a"(SYSCALL_RENAME), "D"(oldpath), "S"(newpath) : "memory");
    return ret;
}

static inline int sys_readdir(const char *path, char *names, int max_entries) {
    int ret;
    __asm__ volatile("int $0x80" : "=a"(ret) : "a"(SYSCALL_READDIR), "D"(path), "S"(names), "d"(max_entries) : "memory");
    return ret;
}

static inline int sys_chmod(const char *path, int mode) {
    int ret;
    __asm__ volatile("int $0x80" : "=a"(ret) : "a"(SYSCALL_CHMOD), "D"(path), "S"(mode) : "memory");
    return ret;
}

static inline int sys_access(const char *path, int mode) {
    int ret;
    __asm__ volatile("int $0x80" : "=a"(ret) : "a"(SYSCALL_ACCESS), "D"(path), "S"(mode) : "memory");
    return ret;
}

/* open(2) flags (mirror Linux x86_64) */
#define O_RDONLY     0x0
#define O_WRONLY     0x1
#define O_RDWR       0x2
#define O_CREAT      0x40
#define O_TRUNC      0x200
#define O_APPEND     0x400

/* lseek(2) whence */
#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2

typedef struct {
    int64_t tv_sec;
    int64_t tv_usec;
} timeval_t;

static inline int64_t sys_time(void) {
    int64_t ret;
    __asm__ volatile("int $0x80" : "=a"(ret) : "a"(SYSCALL_TIME));
    return ret;
}

static inline int sys_gettimeofday(timeval_t *tv) {
    int ret;
    __asm__ volatile("int $0x80" : "=a"(ret) : "a"(SYSCALL_GETTIMEOFDAY), "D"(tv) : "memory");
    return ret;
}

static inline int sys_munmap(void *addr, int len) {
    int ret;
    __asm__ volatile("int $0x80" : "=a"(ret) : "a"(SYSCALL_MUNMAP), "D"(addr), "S"(len) : "memory");
    return ret;
}

static inline void *sys_sbrk(int inc) {
    void *ret;
    __asm__ volatile("int $0x80" : "=a"(ret) : "a"(SYSCALL_SBRK), "D"(inc) : "memory");
    return ret;
}

/* GET_INFO commands */
#define INFO_KERNEL_VERSION 0
#define INFO_KERNEL_NAME    1
#define INFO_APPHOST        2 /* returns 1 when the app runs under the async apphost (Qt dock) */

static inline int sys_get_info(int cmd, char *buf, int max_len) {
    int ret;
    __asm__ volatile("int $0x80" : "=a"(ret) : "a"(SYSCALL_GET_INFO), "D"(cmd), "S"(buf), "d"(max_len) : "memory");
    return ret;
}

/* ── X11 syscalls (48-50) ── */
typedef struct {
    uint8_t type;
    uint8_t detail;
    uint16_t sequence;
    uint32_t time;
    uint32_t root;
    uint32_t event;
    uint32_t child;
    int16_t root_x, root_y;
    int16_t event_x, event_y;
    uint16_t state;
    uint8_t same_screen;
    uint8_t pad[31];
} x11_event_t;

static inline int sys_x11_connect(int *out_client_id) {
    int ret;
    __asm__ volatile("int $0x80" : "=a"(ret), "=D"(*out_client_id) : "a"(SYSCALL_X11_CONNECT) : "memory");
    return ret;
}

static inline int sys_x11_request(int client_id, const void *req, int req_len, void *reply, int *reply_len) {
    int ret;
    __asm__ volatile("int $0x80" : "=a"(ret), "=D"(*reply_len) : "a"(SYSCALL_X11_REQUEST), "D"(client_id), "S"(req), "d"(req_len), "r"(reply) : "memory");
    return ret;
}

static inline int sys_x11_poll(int client_id, x11_event_t *event) {
    int ret;
    __asm__ volatile("int $0x80" : "=a"(ret) : "a"(SYSCALL_X11_POLL), "D"(client_id), "S"(event) : "memory");
    return ret;
}

#endif

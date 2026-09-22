#ifndef CONTAINER_H
#define CONTAINER_H

#include "types.h"
#include "fs.h"
#include "namespace.h"

#define CONTAINER_MAX         16
#define CONTAINER_NAME_MAX    32
#define CONTAINER_MAX_PROCS   32
#define CONTAINER_MAX_PORTS   8
#define CONTAINER_MAX_VOLUMES 8
#define CONTAINER_MAX_ENV     16
#define CONTAINER_LOG_SIZE    4096
#define IMAGE_NAME_MAX        64
#define CONTAINER_IP_BASE     0x0A000001UL

#define MS_BIND 4096

typedef enum {
    CONTAINER_CREATED = 0,
    CONTAINER_RUNNING,
    CONTAINER_PAUSED,
    CONTAINER_STOPPED,
} container_state_t;

typedef struct {
    uint16_t host_port;
    uint16_t container_port;
    uint8_t  protocol;
} port_map_t;

typedef struct {
    char host_path[FS_PATH_MAX];
    char container_path[FS_PATH_MAX];
    int  read_only;
} volume_mount_t;

typedef struct {
    int id;
    char name[CONTAINER_NAME_MAX];
    char image[IMAGE_NAME_MAX];
    char root_path[FS_PATH_MAX];
    container_state_t state;
    int process_count;
    int pids[CONTAINER_MAX_PROCS];
    int pid_1;
    port_map_t ports[CONTAINER_MAX_PORTS];
    int port_count;
    volume_mount_t volumes[CONTAINER_MAX_VOLUMES];
    int volume_count;
    char env[CONTAINER_MAX_ENV][128];
    int env_count;
    uint32_t ip;
    uint64_t start_time;
    int exit_code;
    char log[CONTAINER_LOG_SIZE];
    int log_head;
    int log_count;
    uint64_t mem_used;
    uint64_t cpu_ticks;

    /* Namespace isolation */
    int ns_mount;   /* mount namespace ID */
    int ns_pid;     /* PID namespace ID */
    int ns_net;     /* network namespace ID */
    int ns_user;    /* user namespace ID */
    int ns_uts;     /* UTS namespace ID */

    /* Cgroup resource limits */
    int cgroup_id;

    /* Console I/O ring buffers (host ↔ container bridge) */
    uint8_t  console_out[64 * 1024];   /* container stdout → host reads */
    volatile int console_out_head;
    volatile int console_out_tail;
    uint8_t  console_in[4 * 1024];     /* host writes → container reads as stdin */
    volatile int console_in_head;
    volatile int console_in_tail;
    int console_active;                /* 1 if console I/O is enabled */
} container_t;

int container_init(void);
int container_create_image(const char *name, const char *root_path);
int container_create(const char *name, const char *image);
int container_create_with_limits(const char *name, const char *image, uint64_t memory_mb, int cpu_shares);
int container_start(int id);
int container_stop(int id);
int container_restart(int id);
int container_destroy(int id);
int container_mark_running(int id);
int container_exec(int id, const char *path, int argc, char **argv, char **envp);
int container_list(char out[][CONTAINER_NAME_MAX], int max);
int container_list_all(char out[][CONTAINER_NAME_MAX], int max);
int container_stats(int id, uint64_t *mem, uint64_t *cpu);
int container_get_limits(int id, uint64_t *mem_limit, int *cpu_shares, int *cpu_quota_us, int *pids_max);
int container_logs(int id, char *buf, int max);
int container_inspect(int id, char *buf, int max);
int container_add_port(container_t *c, uint16_t host_port, uint16_t container_port, uint8_t proto);
int container_add_volume(container_t *c, const char *host_path, const char *container_path, int ro);
container_t *container_get(int id);
container_t *container_find(const char *name);
container_t *container_get_by_index(int idx);
int container_get_state(int id);
void container_set_state(int id, container_state_t state);

int container_console_write(int id, const uint8_t *data, int len);
int container_console_read(int id, uint8_t *buf, int max);
int container_stdin_write(int id, const uint8_t *data, int len);
int container_stdin_read(int id, uint8_t *buf, int max);
void container_console_enable(int id);
int container_launch_console(int id);

#endif

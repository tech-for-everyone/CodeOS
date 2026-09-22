#ifndef WINE_SERVER_H
#define WINE_SERVER_H

#include <stdint.h>

/* Wine prefix structure:
 * /wineserver/
 *   default/
 *     system.reg       — simulated Windows registry
 *     dosdevices/      — drive letter mappings (c: -> /wineserver/default/drive_c)
 *     drive_c/         — simulated C: drive
 *       windows/
 *       program files/
 *   bottles/
 *     <name>/          — per-app prefix
 */

#define WINE_PREFIX_MAX 256
#define WINE_MAX_PROCS  64
#define WINE_MAX_DLLS   128
#define WINE_DLL_PATH_MAX 512

/* Wine process descriptor */
typedef struct {
    int pid;
    int active;
    char prefix[WINE_PREFIX_MAX];
    char exe_path[WINE_DLL_PATH_MAX];
    uint64_t image_base;
    uint64_t entry;
    uint64_t stack;
} wine_proc_t;

/* Wine DLL descriptor */
typedef struct {
    int active;
    char name[128];
    char path[WINE_DLL_PATH_MAX];
    uint64_t base;
    uint64_t size;
} wine_dll_t;

/* Server state */
typedef struct {
    wine_proc_t procs[WINE_MAX_PROCS];
    wine_dll_t dlls[WINE_MAX_DLLS];
    int num_procs;
    int num_dlls;
    char default_prefix[WINE_PREFIX_MAX];
    int initialized;
} wine_server_state_t;

/* IPC request types */
#define WINE_REQ_CREATE_PREFIX   1
#define WINE_REQ_DELETE_PREFIX   2
#define WINE_REQ_LIST_PREFIXES   3
#define WINE_REQ_LAUNCH_EXE      4
#define WINE_REQ_GET_STATUS      5
#define WINE_REQ_MAP_DLL         6
#define WINE_REQ_RESOLVE_DLL     7
#define WINE_REQ_CREATE_PROCESS  8
#define WINE_REQ_KILL_PROCESS    9
#define WINE_REQ_LIST_PROCS     10
#define WINE_REQ_LIST_DLLS      11

typedef struct {
    int type;
    int pid;
    char prefix[WINE_PREFIX_MAX];
    char path[WINE_DLL_PATH_MAX];
    int flags;
} wine_req_t;

typedef struct {
    int status;
    int pid;
    int count;
    char data[WINE_DLL_PATH_MAX];
} wine_reply_t;

/* API */
int  wineserver_init(void);
int  wine_create_prefix(const char *name);
int  wine_delete_prefix(const char *name);
int  wine_launch_exe(const char *prefix, const char *exe_path);
int  wine_get_status(void);
int  wine_kill_process(int pid);
void wine_list_prefixes(void);
void wine_list_processes(void);

#endif

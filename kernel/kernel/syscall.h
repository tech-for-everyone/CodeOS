#ifndef SYSCALL_H
#define SYSCALL_H

#include "types.h"
#include <codeos/syscall_abi.h>

/* SYSCALL_KILL signals (subset that maps to process states) */
#define KILL_SIGTERM 15
#define KILL_SIGKILL  9
#define KILL_SIGSTOP 19
#define KILL_SIGCONT 18

/**
 * enum vm_commands - VM_CMD_* — sub-commands for SYSCALL_VM.
 *
 * @VM_CMD_LIST:     List running VMs.
 * @VM_CMD_CREATE:   Allocate a new VM slot.
 * @VM_CMD_START:    Begin execution of a VM.
 * @VM_CMD_STOP:     Halt a running VM.
 * @VM_CMD_DESTROY:  Release a VM slot.
 * @VM_CMD_PAUSE:    Suspend a running VM.
 * @VM_CMD_RESUME:   Resume a paused VM.
 * @VM_CMD_INFO:     Query VM metadata (pid, state, stats).
 * @VM_CMD_SNAPSHOT: Save VM state to disk.
 * @VM_CMD_CROSVM_CREATE: Create a crosvm-style VM.
 * @VM_CMD_CONSOLE_WRITE: Write bytes to a VM console.
 * @VM_CMD_CONSOLE_READ:  Read bytes from a VM console.
 * @VM_CMD_STDIN_WRITE:   Write bytes to a VM stdin.
 * @VM_CMD_STDIN_READ:    Read bytes from a VM stdin.
 * @VM_CMD_CONSOLE_ENABLE: Enable the VM console channel.
 * @VM_CMD_CONTAINER_START: Start a container inside a VM.
 * @VM_CMD_CONTAINER_STOP:  Stop a container inside a VM.
 */
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

/**
 * enum web_commands - WEB_CMD_* — sub-commands for SYSCALL_WEB.
 *
 * @WEB_NAVIGATE:       Load a URL in the active tab.
 * @WEB_SEARCH:         Perform a search query in the active tab.
 * @WEB_TAB_NEW:        Open a new tab.
 * @WEB_TAB_CLOSE:      Close a tab by index.
 * @WEB_TAB_SET_ACTIVE: Switch to a different tab.
 * @WEB_TAB_ACTIVE_GET: Get the active tab index.
 * @WEB_TAB_COUNT:      Get total tab count.
 * @WEB_TAB_PROGRESS:   Get page load progress (0-100).
 * @WEB_GET_INFO:       Get the active-tab state snapshot.
 * @WEB_GET_CONTENT:    Copy tab content buffer to userspace.
 * @WEB_TAB_USED:       Count non-empty tabs.
 */
/* SYSCALL_WEB commands (drives the in-kernel ow_* HTTP backend) */
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

/* Active-tab state snapshot for WEB_GET_INFO (ABI shared with userspace) */
typedef struct {
    char url[512];
    int  content_len;
    int  loading;
    int  error;
    int  can_go_back;
    int  can_go_forward;
    char status[80];
} web_state_t;

/* mmap flags */
#define MAP_PHYSICAL 0x10000

/* Personality flags for syscall translation layers */
#define PERSONALITY_LINUX  1  /* translate Linux x86_64 syscall numbers */

void syscall_init(void);
int64_t syscall_handler(uint64_t n, uint64_t a1, uint64_t a2, uint64_t a3,
                        uint64_t a4, uint64_t a5, uint64_t a6);
void linux_fd_fork_update(int parent_pid, int child_pid);

/* Kernel-internal I/O helpers (used by apphost and other subsystems). */
int64_t kernel_read(int fd, void *buf, int count);
int64_t kernel_write(int fd, const void *buf, int count);

#endif

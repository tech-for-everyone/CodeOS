#include "umode.h"
#include "kprintf.h"

static volatile int umode_running;
static volatile int umode_exit_status;
volatile int user_mode_exit_flag;
void (*user_mode_return_override)(void);

void user_mode_set_return(void (*fn)(void)) {
    user_mode_return_override = fn;
}

#ifdef __aarch64__
/* Implemented in arch/arm64/umode_entry.S */
void arm64_enter_usermode(uint64_t entry, uint64_t stack_top);

void user_mode_enter(uint64_t entry, uint64_t stack_top) {
    arm64_enter_usermode(entry, stack_top);
}

void user_mode_force_return(void) {
    /* ARM64: trigger an interrupt or set a flag to force return */
    user_mode_exit_flag = 1;
}
#else
/* Implemented in arch/x86_64/umode_entry.S */
void user_mode_enter(uint64_t entry, uint64_t stack_top);
void user_mode_force_return(void);
#endif

/* The x86_64 umode state globals (arch/umode_entry.S). Nested user-mode
 * sessions (e.g. container start/exec invoked from inside a syscall) must
 * preserve + restore these so the enclosing user program (shell) still
 * resumes correctly on its own exit. */
extern uint64_t user_mode_return_rip;
extern uint64_t user_mode_kernel_rsp;
extern uint64_t saved_user_rsp;
extern uint64_t umode_saved_rbx;
extern uint64_t umode_saved_rbp;
extern uint64_t umode_saved_r12;
extern uint64_t umode_saved_r13;
extern uint64_t umode_saved_r14;
extern uint64_t umode_saved_r15;

static uint64_t saved_return_rip;
static uint64_t saved_kernel_rsp;
static uint64_t saved_override;
static uint64_t saved_user_rsp_saved;
static uint64_t saved_rbx, saved_rbp, saved_r12, saved_r13, saved_r14, saved_r15;

void user_mode_preserve(void) {
    saved_return_rip = user_mode_return_rip;
    saved_kernel_rsp = user_mode_kernel_rsp;
    saved_override = (uint64_t)user_mode_return_override;
    saved_user_rsp_saved = saved_user_rsp;
    saved_rbx = umode_saved_rbx;
    saved_rbp = umode_saved_rbp;
    saved_r12 = umode_saved_r12;
    saved_r13 = umode_saved_r13;
    saved_r14 = umode_saved_r14;
    saved_r15 = umode_saved_r15;
}

void user_mode_restore(void) {
    user_mode_return_rip = saved_return_rip;
    user_mode_kernel_rsp = saved_kernel_rsp;
    user_mode_return_override = (void (*)(void))saved_override;
    saved_user_rsp = saved_user_rsp_saved;
    umode_saved_rbx = saved_rbx;
    umode_saved_rbp = saved_rbp;
    umode_saved_r12 = saved_r12;
    umode_saved_r13 = saved_r13;
    umode_saved_r14 = saved_r14;
    umode_saved_r15 = saved_r15;
}

int user_mode_active(void) { return umode_running; }
int user_mode_last_exit_status(void) { return umode_exit_status; }

void user_mode_begin(void) {
    umode_running = 1;
    umode_exit_status = 0;
    user_mode_exit_flag = 0;
}

void user_mode_end_from_exit(int status) {
    umode_exit_status = status;
    umode_running = 0;
    user_mode_exit_flag = 1;
}

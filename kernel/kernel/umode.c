#include "umode.h"
#include "kprintf.h"
#include "sched.h"
#include "process.h"
#include "pmm.h"
#include "vmm.h"
#include "gdt.h"

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

/* Returns non-zero if there was a live enclosing session to preserve, i.e. if
 * a caller that then re-enters user mode has something to unwind back to.
 * A caller with no enclosing session (first entry, nothing to return to) gets
 * 0 and must not try to re-enter the enclosing leaf. */
int user_mode_preserve(void) {
    int had_session = (user_mode_return_rip != 0);
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
    return had_session;
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

/* ── Per-thread user-mode state ──
 *
 * The entry/exit machinery above is written against globals, which was
 * correct while exactly one thread could be inside a user session at a time.
 * A fork()ed child runs concurrently with its (blocked) parent, so the
 * globals are treated as "the running thread's session" and sched_yield()
 * swaps them through the thread_t umode_* fields on every context switch. */

uint64_t umode_rsp0_alloc(void) {
    thread_t *t = sched_current();
    if (!t) return 0;
    if (!t->rsp0_top) {
        /* Allocate once per thread and reuse: the old code allocated 16 fresh
         * pages on every user_mode_enter() and leaked the previous ones. */
        uint64_t phys = (uint64_t)pmm_alloc_pages(UMODE_KSTACK_SIZE / PAGE_SIZE);
        if (!phys) return 0;
        t->rsp0_top = phys_to_virt(phys) + UMODE_KSTACK_SIZE;
    }
    gdt_set_rsp0(t->rsp0_top);
    return t->rsp0_top;
}

void umode_note_entry(void) {
    thread_t *t = sched_current();
    if (t) t->proc = current_process;
}

void umode_child_set_return(void (*leaf)(void), uint64_t kernel_stack_top) {
    user_mode_return_rip = (uint64_t)leaf;
    user_mode_kernel_rsp = kernel_stack_top;
    user_mode_return_override = 0;
}

void umode_switch_save(thread_t *t) {
    t->umode_return_rip = user_mode_return_rip;
    t->umode_kernel_rsp = user_mode_kernel_rsp;
    t->umode_return_override = (uint64_t)user_mode_return_override;
    t->umode_saved_rbx = umode_saved_rbx;
    t->umode_saved_rbp = umode_saved_rbp;
    t->umode_saved_r12 = umode_saved_r12;
    t->umode_saved_r13 = umode_saved_r13;
    t->umode_saved_r14 = umode_saved_r14;
    t->umode_saved_r15 = umode_saved_r15;
    t->umode_saved_user_rsp = saved_user_rsp;
    t->umode_running = umode_running;
    t->umode_exit_status = umode_exit_status;
    t->umode_exit_flag = user_mode_exit_flag;
}

void umode_switch_load(thread_t *t) {
    user_mode_return_rip = t->umode_return_rip;
    user_mode_kernel_rsp = t->umode_kernel_rsp;
    user_mode_return_override = (void (*)(void))t->umode_return_override;
    umode_saved_rbx = t->umode_saved_rbx;
    umode_saved_rbp = t->umode_saved_rbp;
    umode_saved_r12 = t->umode_saved_r12;
    umode_saved_r13 = t->umode_saved_r13;
    umode_saved_r14 = t->umode_saved_r14;
    umode_saved_r15 = t->umode_saved_r15;
    saved_user_rsp = t->umode_saved_user_rsp;
    umode_running = t->umode_running;
    umode_exit_status = t->umode_exit_status;
    user_mode_exit_flag = t->umode_exit_flag;
}

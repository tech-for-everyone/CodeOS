#ifndef UMODE_H
#define UMODE_H

#include "types.h"

void user_mode_begin(void);
void user_mode_set_return(void (*fn)(void));
void user_mode_end_from_exit(int status);
int  user_mode_preserve(void);
void user_mode_restore(void);
/* Re-enter the leaf named by user_mode_set_return(), with the kernel stack that
 * user_mode_enter() recorded.  Does not return: it is a jump, so the leaf it
 * lands on must itself not return unless the frame beneath it is still live. */
void user_mode_force_return(void);
extern volatile int user_mode_exit_flag;
void user_mode_enter(uint64_t entry, uint64_t stack_top);
/* Resume a saved user context (proc_ctx_t, process.h) on the current thread.
 * Used to start a fork()ed child; never returns. */
struct proc_ctx;
void user_mode_enter_ctx(const struct proc_ctx *ctx);
int  user_mode_active(void);
int  user_mode_last_exit_status(void);

/* This thread's TSS.RSP0 stack (UMODE_KSTACK_SIZE bytes, allocated on first
 * entry and reused).  The scheduler points TSS.RSP0 at the running thread's
 * stack on every switch, so an interrupt taken from user mode always lands on
 * that thread's own stack.  Returns the virtual stack top, or 0 on failure. */
#define UMODE_KSTACK_SIZE (16 * 4096)
uint64_t umode_rsp0_alloc(void);
/* Attribute the current thread to current_process (called on user-mode
 * entry), so a context switch restores current_process with the thread. */
void umode_note_entry(void);

/* Name the leaf a fork()ed child's user mode returns through, plus the kernel
 * stack force_return jumps to.  A child enters through user_mode_enter_ctx()
 * (which does not consult user_mode_return_override), so this writes the
 * recorded return directly instead of going through user_mode_set_return(). */
void umode_child_set_return(void (*leaf)(void), uint64_t kernel_stack_top);

/* Per-thread session state, swapped by the scheduler on every context switch
 * (see thread_t's umode_* fields).  Without it a forked child and its parent
 * would share one "current session" record and each other's exits. */
struct thread;
void umode_switch_save(struct thread *t);
void umode_switch_load(struct thread *t);

#endif

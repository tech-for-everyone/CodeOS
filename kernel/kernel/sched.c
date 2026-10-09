#include "sched.h"
#include "pmm.h"
#include "vmm.h"
#include "kprintf.h"
#include "string.h"
#include "../drivers/timer.h"
#include "idt.h"
#include "process.h"
#include "umode.h"
#include "spinlock.h"

static thread_t threads[THREAD_MAX] __attribute__((aligned(64)));
static thread_t *current_thread;
static thread_t *idle_thread;
static uint64_t g_sched_busy_ticks;
static int next_tid = 1;
static int thread_count = 0;
static int preemption_enabled;
static int sched_in_yield;
static spinlock_t sched_lock = SPINLOCK_INIT;
volatile int need_resched;

void sched_preempt_disable(void) { preemption_enabled = 0; }
void sched_preempt_enable(void)  { preemption_enabled = 1; }

static void idle_thread_entry(void) {
    while (1) {
        __asm__ volatile("sti; hlt; cli");
        sched_yield();
    }
}

/* ── Per-priority ready queues for O(1) scheduling ── */
static thread_t *ready_head[SCHED_PRIO_COUNT];
static thread_t *ready_tail[SCHED_PRIO_COUNT];

static void ready_queue_push(thread_t *t) {
    int p = t->priority;
    if (p < 0) p = 0;
    if (p >= SCHED_PRIO_COUNT) p = SCHED_PRIO_COUNT - 1;
    t->ready_next = 0;
    if (!ready_head[p]) {
        ready_head[p] = t;
        ready_tail[p] = t;
    } else {
        ready_tail[p]->ready_next = t;
        ready_tail[p] = t;
    }
}

static thread_t *ready_queue_pop(int prio) {
    thread_t *t = ready_head[prio];
    if (t) {
        ready_head[prio] = t->ready_next;
        if (!ready_head[prio]) ready_tail[prio] = 0;
        t->ready_next = 0;
    }
    return t;
}

static void ready_queue_remove(thread_t *t) {
    int p = t->priority;
    if (p < 0) p = 0;
    if (p >= SCHED_PRIO_COUNT) p = SCHED_PRIO_COUNT - 1;
    thread_t **cur = &ready_head[p];
    while (*cur) {
        if (*cur == t) {
            *cur = t->ready_next;
            if (ready_tail[p] == t) {
                if (t->ready_next != 0) {
                    /* t was not the last node: tail unchanged */
                } else if (!ready_head[p]) {
                    ready_tail[p] = 0;
                } else {
                    /* walk to the new last node */
                    thread_t *last = ready_head[p];
                    while (last->ready_next) last = last->ready_next;
                    ready_tail[p] = last;
                }
            }
            t->ready_next = 0;
            return;
        }
        cur = &(*cur)->ready_next;
    }
}

static uint64_t *alloc_stack(void) {
    uint64_t phys = (uint64_t)pmm_alloc_pages(STACK_SIZE / PAGE_SIZE);
    if (!phys) return 0;
    uint64_t *stack = (uint64_t *)phys_to_virt(phys);
    memset(stack, 0, STACK_SIZE);
    return (uint64_t *)((uint64_t)stack + STACK_SIZE);
}

static uint64_t *alloc_syscall_stack(void) {
    uint64_t phys = (uint64_t)pmm_alloc_pages(SYSCALL_STACK_SIZE / PAGE_SIZE);
    if (!phys) return 0;
    uint64_t *page = (uint64_t *)phys_to_virt(phys);
    memset(page, 0, SYSCALL_STACK_SIZE);
    return (uint64_t *)((uint64_t)page + SYSCALL_STACK_SIZE);
}

void sched_init(void) {
    memset(threads, 0, sizeof(threads));
    for (int i = 0; i < SCHED_PRIO_COUNT; i++)
        ready_head[i] = ready_tail[i] = 0;

    idle_thread = &threads[0];
    idle_thread->state = THREAD_READY;
    idle_thread->tid = 0;
    idle_thread->priority = SCHED_PRIO_IDLE;
    idle_thread->kernel_stack_top = alloc_stack();
    idle_thread->syscall_stack_top = alloc_syscall_stack();
    if (!idle_thread->kernel_stack_top || !idle_thread->syscall_stack_top) {
        kprintf("sched: FATAL: cannot allocate idle stacks\n");
        while (1) asm volatile("cli; hlt");
    }
    idle_thread->next = idle_thread;
    strlcpy(idle_thread->name, "idle", sizeof(idle_thread->name));

    /* Set up idle thread's stack with proper entry point */
    uint64_t *sp = idle_thread->kernel_stack_top;
    sp--; *sp = (uint64_t)idle_thread_entry;
    for (int i = 0; i < 15; i++) { sp--; *sp = 0; }
    idle_thread->stack = sp;

    current_thread = idle_thread;
    thread_count = 1;
    preemption_enabled = 1;
    sched_in_yield = 0;

    /* Scheduler initialized */
}

static void reap_zombies(void) {
    for (int i = 0; i < THREAD_MAX; i++) {
        thread_t *t = &threads[i];
        if (t->state == THREAD_ZOMBIE && t != idle_thread && t != current_thread) {
            thread_t *prev = idle_thread;
            while (prev->next != t) {
                prev = prev->next;
                if (prev == idle_thread) break;
            }
            if (prev->next == t) prev->next = t->next;
            if (t->kernel_stack_top)
                pmm_free_pages((uint64_t)virt_to_phys((uint64_t)t->kernel_stack_top - STACK_SIZE),
                               STACK_SIZE / PAGE_SIZE);
            if (t->syscall_stack_top)
                pmm_free_pages((uint64_t)virt_to_phys((uint64_t)t->syscall_stack_top - SYSCALL_STACK_SIZE),
                               SYSCALL_STACK_SIZE / PAGE_SIZE);
            memset(t, 0, sizeof(thread_t));
            thread_count--;
        }
    }
}

int sched_create_thread(const char *name, void (*entry)(void)) {
    return sched_create_thread_prio(name, entry, SCHED_PRIO_NORMAL);
}

int sched_create_thread_prio(const char *name, void (*entry)(void), int priority) {
    if (thread_count >= THREAD_MAX) return -1;

    reap_zombies();

    int tid = next_tid++;
    thread_t *t = 0;
    for (int i = 0; i < THREAD_MAX; i++) {
        /* THREAD_READY == 0, so idle (slot 0, state=READY) would match the
         * `state == 0` free-slot test and get reused. Always keep idle. */
        if (&threads[i] == idle_thread) continue;
        if (threads[i].state == 0 && threads[i].tid == 0) {
            t = &threads[i];
            break;
        }
    }
    if (!t) return -1;

    memset(t, 0, sizeof(thread_t));
    strncpy_safe(t->name, name, THREAD_NAME_MAX);
    t->state = THREAD_READY;
    t->tid = tid;
    t->priority = priority;
    t->cpu_time = 0;
    t->kernel_stack_top = alloc_stack();
    if (!t->kernel_stack_top) return -1;
    t->syscall_stack_top = alloc_syscall_stack();
    if (!t->syscall_stack_top) {
        pmm_free_pages((uint64_t)virt_to_phys((uint64_t)t->kernel_stack_top - STACK_SIZE),
                       STACK_SIZE / PAGE_SIZE);
        return -1;
    }

    t->next = idle_thread->next;
    idle_thread->next = t;

    uint64_t *sp = t->kernel_stack_top;
    sp--; *sp = (uint64_t)entry;
    for (int i = 0; i < 15; i++) { sp--; *sp = 0; }
    t->stack = sp;

    ready_queue_push(t);
    thread_count++;
    return tid;
}

extern uint64_t syscall_kernel_rsp;

static inline void fpu_save(thread_t *t) {
    __asm__ volatile("fxsave64 %0" : "=m"(*t->fpu_state) : : "memory");
}

static inline void fpu_restore(thread_t *t) {
    __asm__ volatile("fxrstor64 %0" : : "m"(*t->fpu_state) : "memory");
}

static volatile int fpu_initialized;

void fpu_init_early(void) {
    uint64_t cr0;
    __asm__ volatile("mov %%cr0, %0" : "=r"(cr0));
    cr0 &= ~(1ULL << 2);
    cr0 |= (1ULL << 1);
    __asm__ volatile("mov %0, %%cr0" : : "r"(cr0));

    uint64_t cr4;
    __asm__ volatile("mov %%cr4, %0" : "=r"(cr4));
    cr4 |= (1 << 9) | (1 << 10);
    __asm__ volatile("mov %0, %%cr4" : : "r"(cr4));

    __asm__ volatile("fninit");
    fpu_initialized = 1;
}

/* Naked leaf context switch. Must have NO stack frame of its own so the
 * 15-register save sits directly above the return address pushed by the
 * `call ctx_switch` in sched_yield. Saves 15 GPRs onto the old stack, stores
 * the resulting SP through old_sp, restores SP from new_sp, pops the 15 GPRs
 * (in reverse of the push order) and returns. Restore order must be the exact
 * reverse of the save order (pop rax first since rax is the lowest slot). */
__attribute__((naked)) static void ctx_switch(uint64_t **old_sp __attribute__((unused)),
                                              uint64_t *new_sp __attribute__((unused))) {
    __asm__ volatile(
        "push %r15\n"
        "push %r14\n"
        "push %r13\n"
        "push %r12\n"
        "push %r11\n"
        "push %r10\n"
        "push %r9\n"
        "push %r8\n"
        "push %rbp\n"
        "push %rdi\n"
        "push %rsi\n"
        "push %rdx\n"
        "push %rcx\n"
        "push %rbx\n"
        "push %rax\n"
        "mov %rsp, (%rdi)\n"
        "mov %rsi, %rsp\n"
        "pop %rax\n"
        "pop %rbx\n"
        "pop %rcx\n"
        "pop %rdx\n"
        "pop %rsi\n"
        "pop %rdi\n"
        "pop %rbp\n"
        "pop %r8\n"
        "pop %r9\n"
        "pop %r10\n"
        "pop %r11\n"
        "pop %r12\n"
        "pop %r13\n"
        "pop %r14\n"
        "pop %r15\n"
        "sti\n"
        "ret\n"
    );
}

void sched_yield(void) {
    __asm__ volatile("cli" : : : "memory");

    spin_lock(&sched_lock);

    if (sched_in_yield) {
        spin_unlock(&sched_lock);
        __asm__ volatile("sti" : : : "memory");
        return;
    }
    sched_in_yield = 1;

    reap_zombies();

    /* Pick highest-priority ready thread — O(SCHED_PRIO_COUNT) = O(1) */
    thread_t *next = 0;
    for (int p = SCHED_PRIO_COUNT - 1; p >= 0; p--) {
        next = ready_queue_pop(p);
        if (next) break;
    }

    if (!next) next = idle_thread;

    thread_t *prev = current_thread;
    if (next->state == THREAD_READY || next == idle_thread) {
        if (prev->state == THREAD_RUNNING) {
            prev->state = THREAD_READY;
            ready_queue_push(prev);
        }
        next->state = THREAD_RUNNING;

        if (fpu_initialized) fpu_save(prev);

        if (prev != next) {
            current_thread = next;
            syscall_kernel_rsp = (uint64_t)next->syscall_stack_top;

            sched_in_yield = 0;
            spin_unlock(&sched_lock);

            if (fpu_initialized) fpu_restore(next);

            ctx_switch(&prev->stack, next->stack);

            /* Resumed here when this thread is scheduled again. The lock was
             * released before the switch; just fall through to the cleanup. */
        }
    }

    sched_in_yield = 0;
    spin_unlock(&sched_lock);
    __asm__ volatile("sti" : : : "memory");
}

void sched_exit(int code) {
    (void)code;
    current_thread->state = THREAD_ZOMBIE;
    sched_yield();
    while (1) asm volatile("cli; hlt");
}

void sched_sleep_ms(uint64_t ms) {
    uint32_t hz = timer_get_frequency();
    if (hz == 0) hz = 100;
    uint64_t wake = timer_get_ticks() + (ms * (uint64_t)hz) / 1000ULL;

    spin_lock(&sched_lock);
    current_thread->state = THREAD_BLOCKED;
    current_thread->sleep_until = wake;
    spin_unlock(&sched_lock);

    while (current_thread->state == THREAD_BLOCKED) {
        __asm__ volatile("sti; hlt; cli" : : : "memory");
        sched_yield();
    }
}

thread_t *sched_current(void) { return current_thread; }

int sched_thread_count(void) { return thread_count; }

/* ── CPU utilization sampling ────────────────────────────────────────────
   Delta-based busy percentage between calls: busy scheduler ticks over
   total timer ticks since the previous sample.  Returns -1 on the first
   call (no delta yet). */
uint64_t sched_busy_ticks(void) { return g_sched_busy_ticks; }

void sched_set_priority(int tid, int priority) {
    if (priority < 0 || priority >= SCHED_PRIO_COUNT) return;
    for (int i = 0; i < THREAD_MAX; i++) {
        if (threads[i].tid == tid) {
            /* Remove from old ready queue if currently ready */
            if (threads[i].state == THREAD_READY)
                ready_queue_remove(&threads[i]);
            threads[i].priority = priority;
            /* Re-add to new ready queue */
            if (threads[i].state == THREAD_READY)
                ready_queue_push(&threads[i]);
            return;
        }
    }
}

void sched_timer_tick(int_frame_t *frame) {
    (void)frame;
    uint64_t now = timer_get_ticks();
    if (!spin_trylock(&sched_lock)) {
        return;
    }
    for (int i = 0; i < THREAD_MAX; i++) {
        thread_t *t = &threads[i];
        if (t->state == THREAD_BLOCKED && now >= t->sleep_until) {
            t->state = THREAD_READY;
            ready_queue_push(t);
        }
    }

    if (preemption_enabled && current_thread && current_thread != idle_thread) {
        current_thread->cpu_time++;
        g_sched_busy_ticks++;
        if (current_thread->state == THREAD_RUNNING) {
            current_thread->state = THREAD_READY;
            ready_queue_push(current_thread);
            need_resched = 1;
        }
    }

    if (user_mode_active() && current_process && need_resched) {
        current_process->preempt_ticks++;
        /* Increase time slice from 3 to 10 ticks for faster app execution */
        if (current_process->preempt_ticks >= 10) {
            current_process->state = PROC_READY;
            current_process = 0;
        }
    }
    spin_unlock(&sched_lock);
}

void sched_switch_to_process(process_t *p) {
    if (!p || p->state != PROC_READY) return;
    current_process = p;
    p->state = PROC_RUNNING;
    p->preempt_ticks = 0;
    vmm_switch_pml4(p->pml4);
}

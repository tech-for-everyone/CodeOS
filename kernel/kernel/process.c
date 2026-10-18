#include "process.h"
#include "pmm.h"
#include "kprintf.h"
#include "string.h"
#include "sched.h"
#include "vmm.h"
#include "elf.h"
#include "fs.h"
#include "ext2.h"
#include "syscall.h"

process_t proc_table[PROC_MAX];
process_t *current_process;
int next_pid = 1;

static void setup_fds(process_t *p) {
    for (int i = 0; i < PROC_FD_MAX; i++) {
        p->fds[i].type = 0;
        p->fds[i].inode = 0;
        p->fds[i].pos = 0;
        p->fds[i].flags = 0;
        p->fds[i].pipe = 0;
    }
}

static int find_proc_slot(void) {
    for (int i = 0; i < PROC_MAX; i++)
        if (proc_table[i].state == PROC_DEAD || proc_table[i].state == 0)
            return i;
    return -1;
}

int proc_init(void) {
    memset(proc_table, 0, sizeof(proc_table));
    current_process = 0;
    return 0;
}

int proc_create(const char *name, uint64_t entry, uint64_t stack_top,
                proc_level_t level) {
    int slot = find_proc_slot();
    if (slot < 0) return -1;

    process_t *p = &proc_table[slot];
    memset(p, 0, sizeof(process_t));
    p->pid = next_pid++;
    p->ppid = 0;
    strncpy_safe(p->name, name, PROC_NAME_MAX);
    p->state = PROC_READY;
    p->entry = entry;
    p->stack_top = stack_top;
    p->pml4 = vmm_current_pml4();
    p->kernel_rsp = 0;
    p->preempt_ticks = 0;
    p->exit_status = 0;
    p->brk = PROC_BRK_BASE;
    p->mmap_base = PROC_MMAP_BASE;
    p->parent = 0;
    p->children = 0;
    p->sibling = 0;
    p->next = 0;
    p->level = level;
    setup_fds(p);

    if (!current_process)
        current_process = p;

    return p->pid;
}

int proc_create_level(const char *name, uint64_t entry, uint64_t stack_top,
                proc_level_t level) {
    return proc_create(name, entry, stack_top, level);
}

int proc_fork(void) {
    process_t *parent = current_process;
    if (!parent) return -1;

    int slot = find_proc_slot();
    if (slot < 0) return -1;

    process_t *child = &proc_table[slot];
    memcpy(child, parent, sizeof(process_t));
    child->pid = next_pid++;
    child->ppid = parent->pid;
    child->state = PROC_READY;
    child->parent = parent;
    child->children = 0;
    child->sibling = 0;
    child->next = 0;
    child->preempt_ticks = 0;

    strncpy_safe(child->name, parent->name, PROC_NAME_MAX);

    /* Clone page tables with COW.
     *
     * Every table allocated here is a *physical* address from pmm_alloc_page(),
     * and process_t::pml4 is likewise physical -- it is the CR3 value:
     * vmm_current_pml4() returns read_page_table_base() and vmm_switch_pml4()
     * writes its argument straight into CR3. A physical address is not
     * dereferenceable, and at this point CR3 is not even our own: the low half
     * of the running address space is the user program, whose text is mapped at
     * 0x400000 (elf.c). So a raw dereference of pmm_alloc_page()'s result reads
     * user text as if it were a page table, and the write back into it lands on
     * a read-only user page:
     *     !!! PF at RIP=proc_fork+0x3bb CR2=0x400128 err=3
     * err=3 is present+write+supervisor, i.e. the "page table" at 0x400000 was
     * the process image. Reach every table through the HHDM (phys_to_virt) and
     * store physical addresses into the entries; never run virt_to_phys() over
     * a value that is already physical (it walks the current page tables). */
    uint64_t new_pml4_phys = pmm_alloc_page();
    if (!new_pml4_phys) { return -1; }
    uint64_t *new_pml4 = (uint64_t *)phys_to_virt(new_pml4_phys);
    uint64_t *old_pml4 = (uint64_t *)phys_to_virt((uint64_t)child->pml4);
    memset(new_pml4, 0, 0x1000);

    /* Share the kernel half instead of copying it.
     *
     * The top half of the address space belongs to the kernel: the direct map
     * at phys_to_virt_base, the kernel image, and the identity map. It used to
     * be deep-copied and copy-on-written exactly like user pages, and that is
     * why fork() reset the machine. The COW pass wrote
     *     old_pt[l] = (old_pt[l] & ~PAGE_WRITE) | PAGE_COW
     * into the *parent's* tables, and for the kernel half those are the live
     * tables the CPU is using at that instant -- 0xffffffff80000000, the
     * kernel image, is PML4[256] -> PDPT[0] -> PD[0] -> PT[0]. So fork cleared
     * PAGE_WRITE on the running kernel's own code and data, the next write
     * faulted, and the fault handler's stack was in that same read-only memory:
     * double fault into triple fault, i.e. a silent reset with not one
     * "PF at RIP" line reaching the serial port.
     *
     * Confirmed by instrumenting proc_fork: it walked all of PML4[0..255] and
     * died entering i=256 j=1, with 209941 pages still free. Sharing is also
     * what the rest of the kernel assumes -- vmm_kernel_pml4() exists so kernel
     * threads run on one shared kernel half rather than a private copy. */
    const int kernel_half = (int)((phys_to_virt_base >> 39) & 0x1FF); /* 39 == PML4_SHIFT, private to vmm.c */

    int fork_err = 0;
    for (int i = 0; i < kernel_half; i++) {
        if (!(old_pml4[i] & PAGE_PRESENT)) continue;

        uint64_t *old_pdp = (uint64_t *)phys_to_virt(old_pml4[i] & ~0xFFF);

        uint64_t new_pdp_phys = pmm_alloc_page();
        if (!new_pdp_phys) { fork_err = 1; break; }
        uint64_t *new_pdp = (uint64_t *)phys_to_virt(new_pdp_phys);
        memset(new_pdp, 0, 0x1000);

        for (int j = 0; j < 512; j++) {
            if (!(old_pdp[j] & PAGE_PRESENT)) continue;
            uint64_t *old_pd = (uint64_t *)phys_to_virt(old_pdp[j] & ~0xFFF);

            uint64_t new_pd_phys = pmm_alloc_page();
            if (!new_pd_phys) { fork_err = 1; break; }
            uint64_t *new_pd = (uint64_t *)phys_to_virt(new_pd_phys);
            memset(new_pd, 0, 0x1000);

            for (int k = 0; k < 512; k++) {
                if (!(old_pd[k] & PAGE_PRESENT)) continue;

                if (old_pd[k] & PAGE_HUGE) {
                    new_pd[k] = old_pd[k];
                    continue;
                }

                uint64_t *old_pt = (uint64_t *)phys_to_virt(old_pd[k] & ~0xFFF);

                uint64_t new_pt_phys = pmm_alloc_page();
                if (!new_pt_phys) { fork_err = 1; break; }
                uint64_t *new_pt = (uint64_t *)phys_to_virt(new_pt_phys);
                memcpy(new_pt, old_pt, 0x1000);

                for (int l = 0; l < 512; l++) {
                    if (!(new_pt[l] & PAGE_PRESENT)) continue;
                    if (new_pt[l] & PAGE_WRITE) {
                        new_pt[l] = (new_pt[l] & ~PAGE_WRITE) | PAGE_COW;
                    }
                    if (new_pt[l] & PAGE_COW) {
                        old_pt[l] = (old_pt[l] & ~PAGE_WRITE) | PAGE_COW;
                    }
                }

                new_pd[k] = new_pt_phys | (old_pd[k] & 0xFFF);
            }
            if (fork_err) { pmm_free_page(new_pd_phys); break; }

            new_pdp[j] = new_pd_phys | (old_pdp[j] & (PAGE_PRESENT | PAGE_WRITE | PAGE_USER));
        }
        if (fork_err) { pmm_free_page(new_pdp_phys); break; }

        new_pml4[i] = new_pdp_phys | (old_pml4[i] & (PAGE_PRESENT | PAGE_WRITE | PAGE_USER));
    }

    /* Kernel half is shared verbatim, never cloned or COW'd: those entries are
     * the live page tables the CPU is executing on (the direct map, the kernel
     * image). proc_wait() stops at kernel_half for the same reason. */
    for (int i = kernel_half; i < 512; i++)
        new_pml4[i] = old_pml4[i];

    if (fork_err) {
        /* Walk and free everything we allocated so far (user half only: the
         * kernel half is shared, not ours to free). */
        for (int i = 0; i < kernel_half; i++) {
            if (!(new_pml4[i] & PAGE_PRESENT)) continue;
            uint64_t *pdp = (uint64_t *)phys_to_virt(new_pml4[i] & ~0xFFF);
            for (int j = 0; j < 512; j++) {
                if (!(pdp[j] & PAGE_PRESENT)) continue;
                uint64_t *pd = (uint64_t *)phys_to_virt(pdp[j] & ~0xFFF);
                for (int k = 0; k < 512; k++) {
                    if (!(pd[k] & PAGE_PRESENT)) continue;
                    if (pd[k] & PAGE_HUGE) continue;
                    pmm_free_page(pd[k] & ~0xFFF);
                }
                pmm_free_page(pdp[j] & ~0xFFF);
            }
            pmm_free_page(new_pml4[i] & ~0xFFF);
        }
        pmm_free_page(new_pml4_phys);
        return -1;
    }

    child->pml4 = (uint64_t *)new_pml4_phys;

    /* Share pipe endpoints with the child -- never copy the buffer.
     *
     * fork() duplicates each descriptor, so parent and child must reach the
     * *same* pipe or a byte written in one is invisible to the other.  The
     * old deep copy also gave every descriptor a private pipe, which made
     * the readers/writers counts meaningless and let either side's close
     * tear down a buffer the other still held.  Bump the endpoint refcount
     * exactly as proc_fd_dup2() does; pipe_close() is refcounted on
     * readers/writers and only frees at zero. */
    for (int i = 0; i < PROC_FD_MAX; i++) {
        pipe_t *pp = child->fds[i].pipe;
        if (!pp) continue;
        if (child->fds[i].type == 1) pp->readers++;
        else if (child->fds[i].type == 2) pp->writers++;
    }

    /* Add as child of parent */
    child->sibling = parent->children;
    parent->children = child;

    /* Inherit parent's linux file descriptors */
    linux_fd_fork_update(parent->pid, child->pid);

    return child->pid;
}

uint64_t proc_exec(uint64_t entry, uint64_t stack_top, int argc, char **argv, char **envp, elf_auxv_info_t *auxv) {
    process_t *p = current_process;
    if (!p) return 0;

    int envc = 0;
    if (envp) {
        while (envp[envc] && envc < 64) envc++;
    }

    uint64_t rsp = elf_setup_stack(stack_top, entry, argc, argv, envc, envp, auxv);

    p->entry = entry;
    p->stack_top = stack_top;
    p->state = PROC_READY;
    strncpy_safe(p->name, argv && argv[0] ? argv[0] : "process", PROC_NAME_MAX);
    return rsp;
}

int proc_exit(int status) {
    process_t *p = current_process;
    if (!p) return -1;

    p->state = PROC_ZOMBIE;
    p->exit_status = status;

    /* Wake parent if waiting */
    if (p->parent && p->parent->state == PROC_SLEEPING) {
        p->parent->state = PROC_READY;
    }

    sched_yield();
    return 0;
}

/* Send a signal to a process. Returns 0 on success.
 * SIGSTOP -> PROC_SLEEPING (suspended), SIGCONT -> PROC_READY,
 * SIGTERM/SIGKILL -> PROC_ZOMBIE (reaped by parent). */
int proc_kill(int pid, int sig) {
    if (pid <= 0) return -1;
    process_t *target = NULL;
    for (int i = 0; i < PROC_MAX; i++) {
        if (proc_table[i].pid == pid && proc_table[i].state != PROC_DEAD) {
            target = &proc_table[i];
            break;
        }
    }
    if (!target) return -1;
    if (target == current_process) return proc_exit(0);

    switch (sig) {
    case KILL_SIGSTOP:
        target->state = PROC_SLEEPING;
        break;
    case KILL_SIGCONT:
        if (target->state == PROC_SLEEPING)
            target->state = PROC_READY;
        break;
    case KILL_SIGTERM:
    case KILL_SIGKILL:
    default:
        target->state = PROC_ZOMBIE;
        target->exit_status = 0;
        if (target->parent && target->parent->state == PROC_SLEEPING)
            target->parent->state = PROC_READY;
        break;
    }
    return 0;
}

int proc_wait(int pid, int *status) {
    process_t *p = current_process;
    if (!p) return -1;

    for (;;) {
        for (int i = 0; i < PROC_MAX; i++) {
            process_t *child = &proc_table[i];
            if (child->state == PROC_DEAD) continue;
            if (child->ppid == p->pid && child->state == PROC_ZOMBIE) {
                if (pid > 0 && child->pid != pid) continue;
                if (status) *status = child->exit_status;
                int cpid = child->pid;

                if (child->pml4) {
                    /* Walk and free page tables without vmm_lock (single-threaded).
                     * The kernel half is shared with the parent since fork()
                     * stopped copying it, so freeing it here would pull the
                     * running kernel's own page tables out from under it. */
                    const int kernel_half = (int)((phys_to_virt_base >> 39) & 0x1FF);
                    uint64_t *pml4 = (uint64_t *)phys_to_virt((uint64_t)child->pml4);
                    for (int pt = 0; pt < kernel_half; pt++) {
                        if (!(pml4[pt] & PAGE_PRESENT)) continue;
                        uint64_t *pdp = (uint64_t *)phys_to_virt(pml4[pt] & ~0xFFF);
                        for (int pd = 0; pd < 512; pd++) {
                            if (!(pdp[pd] & PAGE_PRESENT)) continue;
                            uint64_t *pd_t = (uint64_t *)phys_to_virt(pdp[pd] & ~0xFFF);
                            for (int pt2 = 0; pt2 < 512; pt2++) {
                                if (!(pd_t[pt2] & PAGE_PRESENT)) continue;
                                if (pd_t[pt2] & (1ULL << 7)) continue;
                                uint64_t *pt_t = (uint64_t *)phys_to_virt(pd_t[pt2] & ~0xFFF);
                                for (int l = 0; l < 512; l++) {
                                    if (pt_t[l] & PAGE_PRESENT) {
                                        /* Skip COW pages — sibling may still reference them */
                                        if (pt_t[l] & PAGE_COW) continue;
                                        uint64_t leaf_phys = pt_t[l] & ~0xFFF;
                                        pmm_free_page(leaf_phys);
                                    }
                                }
                                pmm_free_page(virt_to_phys((uint64_t)pt_t));
                            }
                            pmm_free_page(virt_to_phys((uint64_t)pd_t));
                        }
                        pmm_free_page(virt_to_phys((uint64_t)pdp));
                    }
                    pmm_free_page((uint64_t)child->pml4);
                }
                memset(child, 0, sizeof(process_t));
                return cpid;
            }
        }
        /* Block until a child exits - use THREAD_BLOCKED to allow sched to skip us */
        {
            extern thread_t *sched_current(void);
            thread_t *cur = sched_current();
            if (cur) {
                cur->state = THREAD_BLOCKED;
                cur->sleep_until = (uint64_t)-1;
            }
        }
        p->state = PROC_SLEEPING;
        sched_yield();
    }
}

void proc_reap(void) {
    for (int i = 0; i < PROC_MAX; i++) {
        if (proc_table[i].state == PROC_ZOMBIE) {
            proc_table[i].state = PROC_DEAD;
        }
    }
}

process_t *proc_current(void) { return current_process; }
int proc_getpid(void) { return current_process ? current_process->pid : 0; }
int proc_getppid(void) { return current_process ? current_process->ppid : 0; }

process_t *proc_get(int pid) {
    for (int i = 0; i < PROC_MAX; i++)
        if (proc_table[i].pid == pid && proc_table[i].state != PROC_DEAD)
            return &proc_table[i];
    return 0;
}

int proc_fd_alloc(void) {
    process_t *p = current_process;
    if (!p) return -1;
    /* fds 0-2 are the console by convention: sys_read() treats fd 0 as the
     * tty and sys_write() prints, both without consulting this table.  A
     * pipe that lands there can never be read -- the probe in the shell
     * measured exactly that as "sys_pipe read returns 1 garbage byte" --
     * so allocation starts at 3, the way POSIX reserves 0-2. */
    for (int i = 3; i < PROC_FD_MAX; i++) {
        if (p->fds[i].type == 0 && p->fds[i].pipe == 0 && p->fds[i].inode == 0) {
            p->fds[i].type = 1;
            return i;
        }
    }
    return -1;
}

int proc_fd_close(int fd) {
    process_t *p = current_process;
    if (!p || fd < 0 || fd >= PROC_FD_MAX) return -1;
    if (p->fds[fd].pipe) {
        /* Only real pipe endpoints (types 1/2) go through pipe_close.  shm
         * descriptors (type 3) hold a shm_segment_t in the same field; a
         * pipe_close() there decrements "readers" 0x1000 bytes past the
         * 16-byte header -- inside the mapped data -- and can free a page
         * another process is still using. */
        if (p->fds[fd].type == 1 || p->fds[fd].type == 2)
            pipe_close(p->fds[fd].pipe, p->fds[fd].type == 2);
        p->fds[fd].pipe = 0;
    }
    p->fds[fd].type = 0;
    p->fds[fd].inode = 0;
    p->fds[fd].pos = 0;
    return 0;
}

proc_fd_t *proc_fd_get(int fd) {
    process_t *p = current_process;
    if (!p || fd < 0 || fd >= PROC_FD_MAX) return 0;
    if (p->fds[fd].type == 0 && !p->fds[fd].pipe && p->fds[fd].inode == 0) return 0;
    return &p->fds[fd];
}

int proc_fd_dup(int oldfd) {
    int newfd = proc_fd_alloc();
    if (newfd < 0) return -1;
    current_process->fds[newfd] = current_process->fds[oldfd];
    pipe_t *p = current_process->fds[newfd].pipe;
    if (p) {
        if (current_process->fds[newfd].type == 1) p->readers++;
        if (current_process->fds[newfd].type == 2) p->writers++;
    }
    return newfd;
}

int proc_fd_dup2(int oldfd, int newfd) {
    if (oldfd == newfd) return newfd;
    if (newfd < 0 || newfd >= PROC_FD_MAX) return -1;
    /* Close the target fd first if open */
    proc_fd_close(newfd);
    current_process->fds[newfd] = current_process->fds[oldfd];
    pipe_t *p = current_process->fds[newfd].pipe;
    if (p) {
        if (current_process->fds[newfd].type == 1) p->readers++;
        if (current_process->fds[newfd].type == 2) p->writers++;
    }
    return newfd;
}

/* ── Shared memory ── */

#define FD_SHM 3

typedef struct shm_segment {
    uint64_t phys;    /* physical address of first data page */
    int pages;        /* number of 4K pages */
    int refcount;
} __attribute__((packed)) shm_segment_t;

int shm_create(int size) {
    if (size <= 0 || size > 1024 * 1024) return -1;
    int pages = (size + 0xFFF) / 0x1000;

    uint64_t seg_phys = pmm_alloc_page();
    if (!seg_phys) return -1;
    shm_segment_t *seg = (shm_segment_t *)phys_to_virt(seg_phys);
    memset(seg, 0, sizeof(shm_segment_t));
    seg->pages = pages;
    seg->refcount = 0;
    seg->phys = 0;

    seg->phys = pmm_alloc_pages(pages);
    if (!seg->phys) {
        pmm_free_page(seg_phys);
        return -1;
    }

    int fd = proc_fd_alloc();
    if (fd < 0) {
        pmm_free_pages(seg->phys, pages);
        pmm_free_page(seg_phys);
        return -1;
    }
    current_process->fds[fd].type = FD_SHM;
    current_process->fds[fd].pipe = (pipe_t *)seg;
    return fd;
}

void *shm_map(int fd) {
    process_t *p = current_process;
    if (!p || fd < 0 || fd >= PROC_FD_MAX) return 0;
    if (p->fds[fd].type != FD_SHM) return 0;
    shm_segment_t *seg = (shm_segment_t *)p->fds[fd].pipe;
    if (!seg || !seg->phys) return 0;

    uint64_t base = p->mmap_base;
    uint64_t cur = base;
    for (int i = 0; i < seg->pages; i++) {
        uint64_t page_phys = seg->phys + i * 0x1000;
        /* PAGE_USER matters: vmm_map_page() stores the caller's flags
         * verbatim, so without it the page is supervisor-only and the
         * first user access faults (err=7, CR2 at mmap_base). */
        if (vmm_map_page(cur, page_phys, PAGE_PRESENT | PAGE_WRITE | PAGE_USER) < 0)
            return 0;
        cur += 0x1000;
    }
    seg->refcount++;
    p->mmap_base = cur;
    return (void *)base;
}

/* ── Pipe implementation ── */

int pipe_create(int fd[2]) {
    pipe_t *p = (pipe_t *)pmm_alloc_page();
    if (!p) return -1;
    /* Reach the page through the HHDM: p is a *physical* address and the low
     * half of the address space is the caller's program (see proc_fork's
     * note). Writing p->field directly faults on the user image at 0x400000+
     * with err=present+write, e.g. CR2=0x405000 for the first field past the
     * buffer, which is why sys_pipe() never survived a first call. */
    pipe_t *pipe_virt = (pipe_t *)phys_to_virt((uint64_t)p);
    memset(pipe_virt, 0, sizeof(pipe_t));
    pipe_virt->readers = 1;
    pipe_virt->writers = 1;
    pipe_virt->open = 1;

    int r = proc_fd_alloc();
    int w = proc_fd_alloc();
    if (r < 0 || w < 0) {
        pmm_free_page((uint64_t)p);
        return -1;
    }

    current_process->fds[r].type = 1;
    current_process->fds[r].pipe = pipe_virt;
    current_process->fds[w].type = 2;
    current_process->fds[w].pipe = pipe_virt;

    fd[0] = r;
    fd[1] = w;
    return 0;
}

int pipe_read(pipe_t *p, uint8_t *buf, int len) {
    if (!p || !p->open) return 0;
    int total = 0;
    while (total < len && p->rpos != p->wpos) {
        buf[total++] = p->buf[p->rpos];
        p->rpos = (p->rpos + 1) % PROC_PIPE_BUF;
    }
    return total;
}

int pipe_write(pipe_t *p, const uint8_t *buf, int len) {
    if (!p || !p->open) return -1;
    if (!p->readers) return -1;
    int total = 0;
    while (total < len) {
        int next = (p->wpos + 1) % PROC_PIPE_BUF;
        if (next == p->rpos) {
            sched_yield();
            if (!p->readers) return total;
            continue;
        }
        p->buf[p->wpos] = buf[total++];
        p->wpos = next;
    }
    return total;
}

void pipe_close(pipe_t *p, int writer) {
    if (!p) return;
    if (writer) p->writers--;
    else p->readers--;
    if (p->readers == 0 && p->writers == 0) {
        p->open = 0;
        pmm_free_page(virt_to_phys((uint64_t)p));
    }
}

/* ── Task / security levels ── */

int proc_set_level(int pid, proc_level_t level) {
    if (level < LEVEL_CONTAINER || level > LEVEL_KERNEL) return -1;
    for (int i = 0; i < PROC_MAX; i++) {
        if (proc_table[i].pid == pid && proc_table[i].state != PROC_DEAD) {
            proc_table[i].level = level;
            return 0;
        }
    }
    return -1;
}

proc_level_t proc_get_level(int pid) {
    for (int i = 0; i < PROC_MAX; i++) {
        if (proc_table[i].pid == pid && proc_table[i].state != PROC_DEAD)
            return proc_table[i].level;
    }
    /* Unreachable pid.  Report LEVEL_KERNEL rather than LEVEL_CONTAINER: the
     * authorization check must never be handed a low level for a pid that
     * does not exist, or `kill <any number>` would always be permitted.
     * Denying is the safe direction.  systemm_level_of() checks existence
     * separately and reports -1, so this fallback is only reached by callers
     * that did not. */
    return LEVEL_KERNEL;
}

/* Does a live task with this pid exist?  Needed to tell "no such task" apart
 * from "that task is above your level" -- without it, an unreachable pid
 * reports the fallback level and the error message blames the wrong thing. */
int proc_exists(int pid) {
    for (int i = 0; i < PROC_MAX; i++) {
        if (proc_table[i].pid == pid && proc_table[i].state != PROC_DEAD)
            return 1;
    }
    return 0;
}

/* Look up a task by pid and copy its name into buf.
 * Returns 0 on success, -1 if not found. */
int proc_get_pid_name(int pid, char *buf, int len) {
    for (int i = 0; i < PROC_MAX; i++) {
        if (proc_table[i].pid == pid && proc_table[i].state != PROC_DEAD) {
            strncpy_safe(buf, proc_table[i].name, len);
            return 0;
        }
    }
    return -1;
}

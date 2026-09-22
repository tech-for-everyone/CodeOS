/* CodeOS POSIX Stubs - Pthreads compatibility layer
 * Wraps CodeOS syscalls into POSIX-like pthread API for Qt6 */

#ifndef _CODEOS_PTHREAD_H
#define _CODEOS_PTHREAD_H

#include <stdint.h>

/* Forward declarations */
typedef struct pthread_mutex pthread_mutex_t;
typedef struct pthread_cond pthread_cond_t;
typedef struct pthread_attr pthread_attr_t;

/* Thread handle - a scalar id, matching glibc, so libstdc++'s
 * std::thread::id (== / hash on native_handle_type) works. */
typedef unsigned long pthread_t;

#ifndef __DEFINED_clockid_t
#define __DEFINED_clockid_t
typedef long clockid_t;
#endif

/* Mutex types */
#define PTHREAD_MUTEX_NORMAL     0
#define PTHREAD_MUTEX_RECURSIVE  1
#define PTHREAD_MUTEX_ERRORCHECK 2

/* Once init */
#define PTHREAD_ONCE_INIT 0
typedef int pthread_once_t;

/* Spinlock */
typedef int pthread_spinlock_t;

/* Key-value storage (thread-local storage) */
typedef int pthread_key_t;
typedef void (*pthread_destructor_t)(void *);

/* Barrier */
typedef struct { int count; int current; } pthread_barrier_t;

/* rwlock */
typedef struct { int readers; int writer; } pthread_rwlock_t;

/* Forward declaration - full definition provided by time.h / cxx_compat.h.
 * libstdc++'s gthread layer only passes this through as a pointer. */
struct timespec;

/* Mutex */
struct pthread_mutex {
    int locked;
    int type;
    int owner;
    int count;  /* for recursive mutex */
};

#define PTHREAD_MUTEX_INITIALIZER  {0, PTHREAD_MUTEX_NORMAL, -1, 0}
#define PTHREAD_COND_INITIALIZER   {0}
#define PTHREAD_RWLOCK_INITIALIZER {0, 0}

static inline int pthread_mutex_init(pthread_mutex_t *m, const void *attr) {
    if (!m) return -1;
    m->locked = 0;
    m->type = attr ? PTHREAD_MUTEX_NORMAL : PTHREAD_MUTEX_NORMAL;
    m->owner = -1;
    m->count = 0;
    return 0;
}

static inline int pthread_mutex_destroy(pthread_mutex_t *m) {
    (void)m;
    return 0;
}

static inline int pthread_mutex_lock(pthread_mutex_t *m) {
    if (!m) return -1;
    while (m->locked) {
        /* Busy wait (no futex support yet) */
        __asm__ volatile("pause" ::: "memory");
    }
    m->locked = 1;
    m->owner = 1;
    return 0;
}

static inline int pthread_mutex_unlock(pthread_mutex_t *m) {
    if (!m) return -1;
    m->locked = 0;
    m->owner = -1;
    return 0;
}

static inline int pthread_mutex_trylock(pthread_mutex_t *m) {
    if (!m) return -1;
    if (m->locked) return -1;  /* EBUSY */
    m->locked = 1;
    m->owner = 1;
    return 0;
}

/* Condition variable */
struct pthread_cond {
    int signaled;
};

static inline int pthread_cond_init(pthread_cond_t *c, const void *attr) {
    (void)attr;
    if (!c) return -1;
    c->signaled = 0;
    return 0;
}

static inline int pthread_cond_destroy(pthread_cond_t *c) {
    (void)c;
    return 0;
}

static inline int pthread_cond_wait(pthread_cond_t *c, pthread_mutex_t *m) {
    if (!c || !m) return -1;
    pthread_mutex_unlock(m);
    while (!c->signaled) {
        __asm__ volatile("pause" ::: "memory");
    }
    c->signaled = 0;
    pthread_mutex_lock(m);
    return 0;
}

static inline int pthread_cond_signal(pthread_cond_t *c) {
    if (!c) return -1;
    c->signaled = 1;
    return 0;
}

static inline int pthread_cond_broadcast(pthread_cond_t *c) {
    if (!c) return -1;
    c->signaled = 1;
    return 0;
}

/* Thread */
static inline int pthread_create(pthread_t *t, const pthread_attr_t *attr,
                                  void *(*start_routine)(void *), void *arg) {
    (void)attr;
    (void)start_routine;
    (void)arg;
    if (!t) return -1;
    static pthread_t next_tid = 1;
    *t = next_tid++;
    return 0;
}

static inline int pthread_join(pthread_t t, void **retval) {
    (void)t;
    if (retval) *retval = 0;
    return 0;
}

static inline int pthread_detach(pthread_t t) {
    (void)t;
    return 0;
}

static inline pthread_t pthread_self(void) {
    return 1;  /* single-threaded: always the same thread */
}

static inline int pthread_equal(pthread_t t1, pthread_t t2) {
    return t1 == t2;
}

/* Thread-local storage */
static inline int pthread_key_create(pthread_key_t *key, pthread_destructor_t destructor) {
    (void)destructor;
    if (!key) return -1;
    static int next_key = 0;
    *key = next_key++;
    return 0;
}

static inline int pthread_key_delete(pthread_key_t key) {
    (void)key;
    return 0;
}

static inline int pthread_setspecific(pthread_key_t key, const void *value) {
    (void)key;
    (void)value;
    return 0;
}

static inline void *pthread_getspecific(pthread_key_t key) {
    (void)key;
    return 0;
}

/* Once */
static inline int pthread_once(pthread_once_t *once, void (*init)(void)) {
    if (!once || !init) return -1;
    if (*once == 0) {
        init();
        *once = 1;
    }
    return 0;
}

/* Spinlock */
static inline int pthread_spin_init(pthread_spinlock_t *lock, int pshared) {
    (void)pshared;
    if (!lock) return -1;
    *lock = 0;
    return 0;
}

static inline int pthread_spin_destroy(pthread_spinlock_t *lock) {
    (void)lock;
    return 0;
}

static inline int pthread_spin_lock(pthread_spinlock_t *lock) {
    if (!lock) return -1;
    while (__sync_lock_test_and_set(lock, 1)) {
        __asm__ volatile("pause" ::: "memory");
    }
    return 0;
}

static inline int pthread_spin_unlock(pthread_spinlock_t *lock) {
    if (!lock) return -1;
    __sync_lock_release(lock);
    return 0;
}

/* Barrier */
static inline int pthread_barrier_init(pthread_barrier_t *b, const void *attr, unsigned count) {
    (void)attr;
    if (!b || count == 0) return -1;
    b->count = count;
    b->current = 0;
    return 0;
}

static inline int pthread_barrier_destroy(pthread_barrier_t *b) {
    (void)b;
    return 0;
}

static inline int pthread_barrier_wait(pthread_barrier_t *b) {
    if (!b) return -1;
    b->current++;
    while (b->current < b->count) {
        __asm__ volatile("pause" ::: "memory");
    }
    return 0;
}

/* rwlock */
static inline int pthread_rwlock_init(pthread_rwlock_t *rw, const void *attr) {
    (void)attr;
    if (!rw) return -1;
    rw->readers = 0;
    rw->writer = 0;
    return 0;
}

static inline int pthread_rwlock_destroy(pthread_rwlock_t *rw) {
    (void)rw;
    return 0;
}

static inline int pthread_rwlock_rdlock(pthread_rwlock_t *rw) {
    if (!rw) return -1;
    while (rw->writer) {
        __asm__ volatile("pause" ::: "memory");
    }
    rw->readers++;
    return 0;
}

static inline int pthread_rwlock_unlock(pthread_rwlock_t *rw) {
    if (!rw) return -1;
    if (rw->readers > 0) rw->readers--;
    else rw->writer = 0;
    return 0;
}

static inline int pthread_rwlock_wrlock(pthread_rwlock_t *rw) {
    if (!rw) return -1;
    while (rw->readers > 0 || rw->writer) {
        __asm__ volatile("pause" ::: "memory");
    }
    rw->writer = 1;
    return 0;
}

static inline int pthread_rwlock_tryrdlock(pthread_rwlock_t *rw) {
    if (!rw) return -1;
    if (rw->writer) return -1;  /* EBUSY */
    rw->readers++;
    return 0;
}

static inline int pthread_rwlock_trywrlock(pthread_rwlock_t *rw) {
    if (!rw) return -1;
    if (rw->readers > 0 || rw->writer) return -1;  /* EBUSY */
    rw->writer = 1;
    return 0;
}

/* ── Missing pieces required by libstdc++'s gthr-default.h ── */

/* sched_yield */
static inline int sched_yield(void) {
    __asm__ volatile("pause" ::: "memory");
    return 0;
}

/* Mutex attributes (needed by __gthread_recursive_mutex_init_function) */
typedef struct { int type; } pthread_mutexattr_t;

static inline int pthread_mutexattr_init(pthread_mutexattr_t *attr) {
    if (!attr) return -1;
    attr->type = PTHREAD_MUTEX_NORMAL;
    return 0;
}

static inline int pthread_mutexattr_settype(pthread_mutexattr_t *attr, int type) {
    if (!attr) return -1;
    attr->type = type;
    return 0;
}

static inline int pthread_mutexattr_destroy(pthread_mutexattr_t *attr) {
    (void)attr;
    return 0;
}

/* Timed operations (weakrefs in gthr-default.h) */
static inline int pthread_mutex_timedlock(pthread_mutex_t *m, const struct timespec *abs_timeout) {
    (void)abs_timeout;
    return pthread_mutex_lock(m);
}

static inline int pthread_cond_timedwait(pthread_cond_t *c, pthread_mutex_t *m, const struct timespec *abs_timeout) {
    (void)abs_timeout;
    return pthread_cond_wait(c, m);
}

/* Clock-based operations (libstdc++ std::mutex/condition_variable with
 * _GLIBCXX_USE_PTHREAD_MUTEX_CLOCKLOCK / _GLIBCXX_USE_PTHREAD_COND_CLOCKWAIT) */
static inline int pthread_mutex_clocklock(pthread_mutex_t *m, clockid_t clock, const struct timespec *ts) {
    (void)clock;
    (void)ts;
    return pthread_mutex_lock(m);
}

static inline int pthread_cond_clockwait(pthread_cond_t *c, pthread_mutex_t *m, clockid_t clock, const struct timespec *ts) {
    (void)clock;
    (void)ts;
    return pthread_cond_wait(c, m);
}

/* pthread_cancel */
static inline int pthread_cancel(pthread_t t) {
    (void)t;
    return 0;
}

#endif /* _CODEOS_PTHREAD_H */

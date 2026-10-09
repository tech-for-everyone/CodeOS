#ifndef SPINLOCK_H
#define SPINLOCK_H

#include "types.h"

typedef volatile int spinlock_t;

#define SPINLOCK_INIT 0

static inline void spin_lock(spinlock_t *lock) {
    while (__sync_lock_test_and_set(lock, 1)) {
        while (*lock) __asm__ volatile("pause");
    }
}

static inline void spin_unlock(spinlock_t *lock) {
    __sync_lock_release(lock);
}

static inline int spin_trylock(spinlock_t *lock) {
    return !__sync_lock_test_and_set(lock, 1);
}

#endif

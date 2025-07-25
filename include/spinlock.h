#ifndef SPINLOCK_H
#define SPINLOCK_H
#include <stdint.h>

typedef volatile int spinlock_t;

static inline void spin_lock(spinlock_t* lock) {
    while (__sync_lock_test_and_set(lock, 1)) { }
}

static inline void spin_unlock(spinlock_t* lock) {
    __sync_lock_release(lock);
}

#endif // SPINLOCK_H 
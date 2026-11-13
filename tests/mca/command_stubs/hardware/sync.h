#pragma once
typedef int spin_lock_t;
static inline void spin_lock_unsafe_blocking(spin_lock_t *lock) { (void)lock; }
static inline void spin_unlock_unsafe(spin_lock_t *lock) { (void)lock; }

#pragma once

#include <inttypes.h>
#include "hardware/sync.h"
#include "hardware/timer.h"
#include "pico/platform.h"

#include "util.h"

extern spin_lock_t *ps1_dirty_spin_lock;
extern volatile uint32_t ps1_dirty_lockout;

static inline void __time_critical_func(ps1_dirty_lock)(void) {
    spin_lock_unsafe_blocking(ps1_dirty_spin_lock);
}

static inline void __time_critical_func(ps1_dirty_unlock)(void) {
    spin_unlock_unsafe(ps1_dirty_spin_lock);
}

static inline void __time_critical_func(ps1_dirty_lockout_renew)(void) {
    /* lockout for 100ms, store time in ms */
    ps1_dirty_lockout = (uint32_t)(RAM_time_us_64() / 1000) + 100;
}

static inline int __time_critical_func(ps1_dirty_lockout_expired)(void) {
    return ps1_dirty_lockout < (uint32_t)(RAM_time_us_64() / 1000);
}

void ps1_dirty_init(void);
int ps1_dirty_get_marked(void);
void ps1_dirty_mark(uint32_t sector);
void ps1_dirty_task(void);

extern int ps1_dirty_activity;

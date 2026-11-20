#pragma once

#include <stdint.h>

/* Host tests are single threaded: the USB "interrupt" runs synchronously. */
static inline uint32_t save_and_disable_interrupts(void) { return 0; }
static inline void restore_interrupts(uint32_t status) { (void)status; }

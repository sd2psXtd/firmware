#pragma once

#include <stdbool.h>
#include <stdint.h>

/* Calculate before touching the file: negative positions and signed overflow
   must leave the current position unchanged. The storage seek API is int64_t. */
static inline bool ps1_mmce_fs_seek_target(uint64_t base, int64_t offset, uint64_t *target) {
    uint64_t magnitude;
    uint64_t value = 0U;
    bool valid = base <= INT64_MAX;

    if (valid && offset >= 0) {
        magnitude = (uint64_t)offset;
        valid = magnitude <= (uint64_t)INT64_MAX - base;
        if (valid)
            value = base + magnitude;
    } else if (valid) {
        /* Avoid negating INT64_MIN. */
        magnitude = (uint64_t)(-(offset + 1)) + 1U;
        valid = magnitude <= base;
        if (valid)
            value = base - magnitude;
    }
    if (valid)
        *target = value;
    return valid;
}

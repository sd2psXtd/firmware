#pragma once

#include <stddef.h>
#include <stdint.h>

/* Maximum used by the previous MCA implementation; not yet verified against
 * Sony's driver. The caller owns the buffer and its lifetime. */
#define MCA_RAW_MAX 1152
#define MCA_FRAME_MAX (MCA_RAW_MAX + 4)
#define MCA_PS2_PAGE_RAW 528

typedef enum {
    MCA_FRAME_MORE,
    MCA_FRAME_READY,
    MCA_FRAME_INVALID,
} mca_frame_result_t;

typedef struct {
    uint8_t *data;
    size_t capacity;
    size_t length;
    size_t expected;
    mca_frame_result_t result;
} mca_frame_t;

void mca_frame_init(mca_frame_t *frame, uint8_t *buffer, size_t capacity);
void mca_frame_reset(mca_frame_t *frame);
/* Returns the number consumed. A completed frame remains owned by the caller
 * until reset, so following bytes are not silently consumed. */
size_t mca_frame_feed(mca_frame_t *frame, const uint8_t *data, size_t length);

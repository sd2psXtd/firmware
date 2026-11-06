#pragma once

#include "mca_bridge.h"

#include <stdbool.h>
#include <stdint.h>

#define MCA_PAGE_RAW_SIZE 528u
#define MCA_PAGE_STAGE_OFFSET 600u
#define MCA_PAGE_REPLY_TIMEOUT_US 1000000u

typedef enum {
    MCA_PAGE_IDLE,
    MCA_PAGE_PENDING,
    MCA_PAGE_READ_DONE,
    MCA_PAGE_WRITE_DONE,
    MCA_PAGE_FAILED,
} mca_page_result_t;

/* Core 0/task-side API. `write_data` must point to 528 readable bytes for a
 * write and be NULL for a read. The bytes are copied into bridge RX at offset
 * MCA_PAGE_STAGE_OFFSET before any SIO request is published. */
bool mca_page_start(mca_bridge_t *bridge, bool write, uint32_t page,
                    const uint8_t *write_data, uint64_t now_us);

/* Poll once from the normal task loop; never waits for core 1. On READ_DONE or
 * WRITE_DONE the bridge remains in RESPONSE. The framed USB response uses the
 * bridge TX buffer; release it only after the USB transfer completes. */
mca_page_result_t mca_page_task(mca_bridge_t *bridge, uint64_t now_us);

bool mca_page_is_cancelling(void);

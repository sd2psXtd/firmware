#pragma once

#include <stdbool.h>
#include <stdint.h>

#define MCA_PS2_PAGE_DATA 512u
#define MCA_PS2_PAGE_ECC   16u
#define MCA_PS2_PAGE_RAW  (MCA_PS2_PAGE_DATA + MCA_PS2_PAGE_ECC)
#define MCA_CARD_REPLY_TIMEOUT_US 1000000u

typedef enum {
    MCA_PAGE_IDLE,
    MCA_PAGE_PENDING,
    MCA_PAGE_READ_DONE,
    MCA_PAGE_WRITE_DONE,
    MCA_PAGE_FAILED,
} mca_page_result_t;

/* Core-0 adapter sequencer. NULL write_data selects a read; otherwise copy
 * 528 bytes before returning. All card operations run through core 1. */
bool mca_page_transfer_start(uint32_t page, const uint8_t *write_data, uint64_t now_us);
bool mca_page_transfer_busy(void);
/* Check the deadline without consuming a reply or touching the output buffer. */
bool mca_page_transfer_expired(uint64_t now_us);
/* Advance at most one completed card transaction. read_data needs 528 bytes
 * for reads and is only written when MCA_PAGE_READ_DONE is returned.
 * FAILED stays busy until the owner stops the emulator and calls reset(). */
mca_page_result_t mca_page_transfer_task(uint8_t *read_data, uint64_t now_us);
/* Call only after the emulator is stopped and its transport has been cleared. */
void mca_page_transfer_reset(void);

#pragma once

#include <stdint.h>
#include <stdbool.h>

extern void (*mmceman_callback)(void);
extern int mmceman_transfer_stage;

extern volatile bool mmceman_tx_queued;
extern volatile uint8_t mmceman_tx_byte;

/* NOTE: Used to prevent mcman flushing old cache
 * data to the new memcard after a memcard switch.
 * mcman will invalidate handles and clear cache if
 * it cannot detect the memcard after 5 retries. */
extern volatile uint8_t mmceman_mcman_retry_counter;
extern volatile bool mmceman_op_in_progress;
extern volatile bool mmceman_timeout_detected;
extern volatile bool mmceman_fs_abort_read;

extern volatile uint8_t mmceman_cmd;
extern volatile uint8_t mmceman_mode;
extern volatile uint16_t mmceman_cnum;
extern char mmceman_gameid[251];

void ps2_mmceman_task(void);

void ps2_mmceman_set_cb(void (*cb)(void));
void ps2_mmceman_queue_tx(uint8_t byte);

bool ps2_mmceman_set_gameid(const uint8_t* game_id);
const char* ps2_mmceman_get_gameid(void);

void ps2_mmceman_next_ch(bool delay);
void ps2_mmceman_prev_ch(bool delay);
void ps2_mmceman_next_idx(bool delay);
void ps2_mmceman_prev_idx(bool delay);
void ps2_mmceman_set_bootcard(bool delay);

#pragma once

#include <stdint.h>
#include <stdbool.h>

extern volatile uint8_t sd2psxman_cmd;
extern volatile uint8_t sd2psxman_mode;
extern volatile uint16_t sd2psxman_cnum;
extern char sd2psxman_gameid[251];

void ps2_sd2psxman_task(void);
bool ps2_sd2psxman_set_gameid(const uint8_t* const game_id);
const char* ps2_sd2psxman_get_gameid(void);
void ps2_sd2psxman_next_ch(bool delay);
void ps2_sd2psxman_prev_ch(bool delay);
void ps2_sd2psxman_next_idx(bool delay);
void ps2_sd2psxman_prev_idx(bool delay);

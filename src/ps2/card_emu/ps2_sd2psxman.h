#pragma once

#include <stdint.h>

extern volatile uint8_t sd2psxman_cmd;
extern volatile uint8_t sd2psxman_mode;
extern volatile uint16_t sd2psxman_cnum;
extern char sd2psxman_gameid[251];

void ps2_sd2psxman_task(void);
void ps2_sd2psxman_set_gameid(const char* const game_id);
const char* ps2_sd2psxman_get_gameid(void);
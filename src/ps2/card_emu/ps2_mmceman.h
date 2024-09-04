#pragma once

#include <stdint.h>

extern volatile uint8_t mmceman_cmd;
extern volatile uint8_t mmceman_mode;
extern volatile uint16_t mmceman_cnum;
extern char mmceman_gameid[251];

//Core 0 task
void ps2_mmceman_task(void);
void ps2_mmceman_set_gameid(const char* const game_id);
const char* ps2_mmceman_get_gameid(void);
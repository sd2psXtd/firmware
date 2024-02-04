#pragma once

#include <stdint.h>

#define SD2PSXMAN_PING 0x1
#define SD2PSXMAN_GET_STATUS 0x2
#define SD2PSXMAN_GET_CARD 0x3
#define SD2PSXMAN_SET_CARD 0x4
#define SD2PSXMAN_GET_CHANNEL 0x5
#define SD2PSXMAN_SET_CHANNEL 0x6
#define SD2PSXMAN_GET_GAMEID 0x7
#define SD2PSXMAN_SET_GAMEID 0x8

extern volatile uint8_t sd2psxman_cmd;
extern volatile uint8_t sd2psxman_mode;
extern volatile uint16_t sd2psxman_cnum;
extern char sd2psxman_gameid[251];

void ps2_sd2psxman_task(void);
void ps2_sd2psxman_set_gameid(const char* const game_id);
const char* ps2_sd2psxman_get_gameid(void);
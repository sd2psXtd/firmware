#pragma once


#include <stdint.h>

#define MCP_GAME_ID     (1U)
#define MCP_NXT_CH      (2U)
#define MCP_PRV_CH      (3U)
#define MCP_NXT_CARD    (4U)
#define MCP_PRV_CARD    (5U)
#define MCP_SWITCH_BOOTCARD (6U)


void ps1_memory_card_main(void);
void ps1_memory_card_enter(void);
void ps1_memory_card_exit(void);
void ps1_memory_card_unload(void);

uint8_t ps1_memory_card_get_ode_command(void);
void ps1_memory_card_reset_ode_command(void);
const char* ps1_memory_card_get_game_id(void);
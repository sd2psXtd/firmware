#pragma once


#include <stdint.h>
#include <stdbool.h>

#define MMCE_PS1_GAME_ID     (1U)
#define MMCE_PS1_NXT_CH      (2U)
#define MMCE_PS1_PRV_CH      (3U)
#define MMCE_PS1_NXT_CARD    (4U)
#define MMCE_PS1_PRV_CARD    (5U)
#define MMCE_PS1_SWITCH_BOOTCARD (6U)
#define MMCE_PS1_SWITCH_DEFAULT  (7U)
#define MMCE_PS1_SET_CARD    (8U)
#define MMCE_PS1_SET_CHANNEL (9U)
#define MMCE_PS1_RESET       (10U)


void ps1_memory_card_main(void);
void ps1_memory_card_enter(void);
void ps1_memory_card_exit(void);
void ps1_memory_card_unload(void);
void ps1_memory_card_set_mca_transport(bool enabled);

uint8_t ps1_memory_card_get_ode_command(void);
void ps1_memory_card_reset_ode_command(void);
const char* ps1_memory_card_get_game_id(void);


#pragma once
#include <stdbool.h>
#include <stdint.h>

void ps1_mmce_init(void);
void ps1_mmce_task(void);
void ps1_mmce_next_ch(bool delay);
void ps1_mmce_prev_ch(bool delay);
void ps1_mmce_next_idx(bool delay);
void ps1_mmce_prev_idx(bool delay);
bool ps1_mmce_set_gameid(const uint8_t* const game_id);
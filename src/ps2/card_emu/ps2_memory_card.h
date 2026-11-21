#pragma once

#include <stdbool.h>
#include <stdint.h>


/**
 * @brief Main function for the memory card backend
 */
void ps2_memory_card_main(void);

/**
 * @brief Enter the memory card backend
 *       This is usually called when initializing the card
 */
void ps2_memory_card_enter(void);

/**
 * @brief Exit the memory card backend
 *       This is usually called when switching the card
 */
void ps2_memory_card_exit(void);

/**
 * @brief Store a raw PS1 MMCE Game ID received while in PS2 mode (core 1)
 *       Ignored while a previous Game ID is still pending
 */
void ps2_memory_card_set_ps1_gameid(const uint8_t* const game_id, uint8_t length);

/**
 * @brief Handle a pending PS1 Game ID (core 0)
 *       A valid ID selects the game and switches to PS1 mode
 */
void ps2_memory_card_process_ps1_gameid(void);



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



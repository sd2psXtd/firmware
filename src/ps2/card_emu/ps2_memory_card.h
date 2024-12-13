#pragma once

#include <stdbool.h>
#include <stdint.h>

void ps2_memory_card_main(void);
void ps2_memory_card_enter(void);
void ps2_memory_card_exit(void);
void ps2_memory_card_unload(void);
bool ps2_memory_card_running(void);
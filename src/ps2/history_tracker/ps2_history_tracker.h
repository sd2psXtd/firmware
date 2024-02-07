#pragma once
#include <stdint.h>


void ps2_history_tracker_registerPageWrite(uint32_t page);
void ps2_history_tracker_init();
void ps2_history_tracker_run();
void ps2_history_tracker_card_changed();
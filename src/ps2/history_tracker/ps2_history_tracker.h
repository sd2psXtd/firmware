#pragma once
#include <stdint.h>


void ps2_history_tracker_registerPageWrite(uint32_t page);
void ps2_history_tracker_init(void);
void ps2_history_tracker_task(void);
void ps2_history_tracker_card_changed(void);

void ps2_history_tracker_format();
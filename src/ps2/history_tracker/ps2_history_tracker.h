#pragma once
#include <stdint.h>
#include <stdbool.h>


void ps2_history_tracker_registerPageWrite(uint32_t page);
void ps2_history_tracker_init(void);
void ps2_history_tracker_task(void);
void ps2_history_tracker_card_changed(void);
bool ps2_history_tracker_needs_refresh(void);

void ps2_history_tracker_format();
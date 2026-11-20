#pragma once

#include "tusb.h"

/* Matches TinyUSB 0.18's dcd_eventid_t value. */
#define DCD_EVENT_SETUP_RECEIVED 6

void dcd_edpt_stall(uint8_t rhport, uint8_t endpoint);
void dcd_edpt_clear_stall(uint8_t rhport, uint8_t endpoint);

#pragma once

#include <stdint.h>
#include <config.h>
#include <stdbool.h>

extern uint8_t splash_img[(DISPLAY_HEIGHT * DISPLAY_WIDTH / 8) + 8];
extern bool splash_game_image_available;

extern void splash_init(void);
extern void splash_load_usb(void);
extern void splash_update_current(const char* card_folder, const char* card_base, int chan_idx);

#pragma once

#include <stdbool.h>
#include <stdint.h>

void oled_update_last_action_time(void);
int oled_init(void);
void oled_write_pixels(int x, int y, int width, int height, const uint8_t *pixels);
void oled_show(void);
void oled_show_error(const char *message);
void oled_set_contrast(uint8_t v);
void oled_set_vcomh(uint8_t v);
bool oled_is_powered_on(void);
void oled_flip(bool flip);
void oled_task(void);

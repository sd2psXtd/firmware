#pragma once

#include <stdbool.h>

void oled_update_last_action_time(void);
int oled_init(void);
void oled_clear(void);
void oled_draw_pixel(int x, int y);
void oled_show(void);
void oled_draw_text(const char *s);
bool oled_is_powered_on(void);
void oled_task(void);

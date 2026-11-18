#pragma once

#include "lvgl.h"

void gui_menu_create(lv_obj_t *screen);
void gui_menu_reset(void);
void gui_menu_collect(void);
void gui_menu_refresh_usb(void);

/* Menu actions handled by the GUI session. */
void gui_menu_select_mode(uint8_t mode);
void gui_menu_select_variant(int variant);
const char *gui_menu_deploy_civ(void);
void gui_menu_start_usb_storage(void);

#pragma once

#include "lvgl.h"
#include "ui_menu.h"

#define GUI_SCREEN_IMAGE_TIMEOUT_US   (3 * 1000 * 1000)

void gui_init(void);
void gui_task(void);
void gui_request_refresh(void);
void gui_do_ps1_card_switch(void);
void gui_do_ps2_card_switch(void);

void evt_menu_page(lv_event_t *event);

#define UI_GOTO_SCREEN(scr) do { \
    lv_scr_load(scr); \
    lv_group_focus_obj(scr); \
} while (0);

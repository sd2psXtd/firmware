#pragma once

#include <stdbool.h>
#include <stdint.h>

void settings_init(void);

int settings_get_ps1_card(void);
int settings_get_ps1_channel(void);
void settings_set_ps1_card(int x);
void settings_set_ps1_channel(int x);

int settings_get_ps2_card(void);
int settings_get_ps2_channel(void);
void settings_set_ps2_card(int x);
void settings_set_ps2_channel(int x);

enum {
    MODE_PS1 = 0,
    MODE_PS2 = 1,
};

int settings_get_mode(void);
void settings_set_mode(int mode);
bool settings_get_ps1_autoboot(void);
void settings_set_ps1_autoboot(bool autoboot);
bool settings_get_ps1_game_id(void);
void settings_set_ps1_game_id(bool enabled);
bool settings_get_ps2_autoboot(void);
void settings_set_ps2_autoboot(bool autoboot);
bool settings_get_ps2_game_id(void);
void settings_set_ps2_game_id(bool enabled);

#define IDX_MIN 1
#define IDX_BOOT 0
#define CHAN_MIN 1
#define CHAN_MAX 8

uint8_t settings_get_display_timeout();
uint8_t settings_get_display_contrast();
uint8_t settings_get_display_vcomh();
void settings_set_display_timeout(uint8_t display_timeout);
void settings_set_display_contrast(uint8_t display_contrast);
void settings_set_display_vcomh(uint8_t display_vcomh);

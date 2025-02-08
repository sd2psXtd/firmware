#pragma once

#include <stdbool.h>
#include <stdint.h>

void settings_load_sd(void);
void settings_init(void);

int settings_get_ps1_card(void);
int settings_get_ps1_channel(void);
int settings_get_ps1_boot_channel(void);
void settings_set_ps1_card(int x);
void settings_set_ps1_channel(int x);
void settings_set_ps1_boot_channel(int x);

int settings_get_ps2_card(void);
int settings_get_ps2_channel(void);
int settings_get_ps2_boot_channel(void);
uint8_t settings_get_ps2_cardsize(void);
int settings_get_ps2_variant(void);
void settings_set_ps2_card(int x);
void settings_set_ps2_channel(int x);
void settings_set_ps2_boot_channel(int x);
void settings_set_ps2_cardsize(uint8_t size);
void settings_set_ps2_variant(int x);

enum {
    MODE_PS1 = 0,
    MODE_PS2 = 1,
    MODE_TEMP_PS1 = 2
};

enum {
    PS2_VARIANT_RETAIL  = 0,
    PS2_VARIANT_COH     = 1,
    PS2_VARIANT_PROTO   = 2
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

uint8_t settings_get_display_timeout(void);
uint8_t settings_get_display_contrast(void);
uint8_t settings_get_display_vcomh(void);
bool    settings_get_display_flipped(void);
void settings_set_display_timeout(uint8_t display_timeout);
void settings_set_display_contrast(uint8_t display_contrast);
void settings_set_display_vcomh(uint8_t display_vcomh);
void settings_set_display_flipped(bool flipped);

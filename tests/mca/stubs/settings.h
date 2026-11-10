#pragma once
#include <stdbool.h>
#define MODE_PS1 0
#define MODE_PS2 1
#define MODE_TEMP_PS1 2
#define PS2_VARIANT_RETAIL 0
#define PS2_VARIANT_COH 1
#define PS2_VARIANT_SC2 3
int settings_get_mode(bool temporary);
int settings_get_ps2_variant(void);
bool settings_get_mca_enabled(void);
void settings_set_mca_enabled(bool enabled);
bool settings_get_ps1_controllercombo(void);

#include "settings.h"

#include <stdio.h>
#include <string.h>

#include "debug.h"
#include "wear_leveling/wear_leveling.h"

/* NOTE: for any change to the layout/size of this structure (that gets shipped to users),
   ensure to increase the version magic below -- this will trigger setting reset on next boot */
typedef struct {
    uint32_t version_magic;
    uint16_t ps1_card;
    uint16_t ps2_card;
    uint8_t ps1_channel;
    uint8_t ps2_channel;
    uint8_t ps1_flags; // TODO: single bit options: freepsxboot, pocketstation, freepsxboot slot
    // TODO: more ps1 settings: model for freepsxboot
    uint8_t ps2_flags; // TODO: single bit options: autoboot
    uint8_t sys_flags; // TODO: single bit options: whether ps1 or ps2 mode, etc
    uint8_t display_timeout; // display - auto off, in seconds, 0 - off
    uint8_t display_contrast; // display - contrast, 0-255
    uint8_t display_vcomh; // display - vcomh, valid values are 0x00, 0x20, 0x30 and 0x40
    // TODO: how do we store last used channel for cards that use autodetecting w/ gameid?
} settings_t;

#define SETTINGS_VERSION_MAGIC (0xAACD0001)
#define SETTINGS_PS1_FLAGS_AUTOBOOT (0b0000001)
#define SETTINGS_PS2_FLAGS_AUTOBOOT (0b0000001)

_Static_assert(sizeof(settings_t) == 16, "unexpected padding in the settings structure");

static settings_t settings;

static void settings_reset(void) {
    memset(&settings, 0, sizeof(settings));
    settings.version_magic = SETTINGS_VERSION_MAGIC;
    settings.display_timeout = 0; // off
    settings.display_contrast = 255; // 100%
    settings.display_vcomh = 0x30; // 0.83 x VCC
    if (wear_leveling_write(0, &settings, sizeof(settings)) == WEAR_LEVELING_FAILED)
        fatal("failed to reset settings");
}

void settings_init(void) {
    printf("Settings - init\n");
    if (wear_leveling_init() == WEAR_LEVELING_FAILED) {
        printf("failed to init wear leveling, reset settings\n");
        settings_reset();

        if (wear_leveling_init() == WEAR_LEVELING_FAILED)
            fatal("cannot init eeprom emu");
    }

    wear_leveling_read(0, &settings, sizeof(settings));
    if (settings.version_magic != SETTINGS_VERSION_MAGIC) {
        printf("version magic mismatch, reset settings\n");
        settings_reset();
    }
}

void settings_update(void) {
    wear_leveling_write(0, &settings, sizeof(settings));
}

void settings_update_part(void *settings_ptr, uint32_t sz) {
    wear_leveling_write((uint8_t*)settings_ptr - (uint8_t*)&settings, settings_ptr, sz);
}

#define SETTINGS_UPDATE_FIELD(field) settings_update_part(&settings.field, sizeof(settings.field))

int settings_get_ps2_card(void) {
    if (settings.ps2_card < IDX_MIN)
        return IDX_MIN;
    return settings.ps2_card;
}

int settings_get_ps2_channel(void) {
    if (settings.ps2_channel < CHAN_MIN || settings.ps2_channel > CHAN_MAX)
        return CHAN_MIN;
    return settings.ps2_channel;
}

void settings_set_ps2_card(int card) {
    if (card != settings.ps2_card) {
        settings.ps2_card = card;
        SETTINGS_UPDATE_FIELD(ps2_card);
    }
}

void settings_set_ps2_channel(int chan) {
    if (chan != settings.ps2_channel) {
        settings.ps2_channel = chan;
        SETTINGS_UPDATE_FIELD(ps2_channel);
    }
}

int settings_get_ps1_card(void) {
    if (settings.ps1_card < IDX_MIN)
        return IDX_MIN;
    return settings.ps1_card;
}

int settings_get_ps1_channel(void) {
    if (settings.ps1_channel < CHAN_MIN || settings.ps1_channel > CHAN_MAX)
        return CHAN_MIN;
    return settings.ps1_channel;
}

void settings_set_ps1_card(int card) {
    if (card != settings.ps1_card) {
        settings.ps1_card = card;
        SETTINGS_UPDATE_FIELD(ps1_card);
    }
}

void settings_set_ps1_channel(int chan) {
    if (chan != settings.ps1_channel) {
        settings.ps1_channel = chan;
        SETTINGS_UPDATE_FIELD(ps1_channel);
    }
}

int settings_get_mode(void) {
    return settings.sys_flags & 1;
}

void settings_set_mode(int mode) {
    if (mode != MODE_PS1 && mode != MODE_PS2)
        return;

    if (mode != settings_get_mode()) {
        /* clear old mode, then set what was passed in */
        settings.sys_flags &= ~1;
        settings.sys_flags |= mode;
        SETTINGS_UPDATE_FIELD(sys_flags);
    }
}

bool settings_get_ps1_autoboot(void) {
    return (settings.ps1_flags & SETTINGS_PS1_FLAGS_AUTOBOOT);
}

void settings_set_ps1_autoboot(bool autoboot) {
    if (autoboot != settings_get_ps1_autoboot())
        settings.ps1_flags ^= SETTINGS_PS1_FLAGS_AUTOBOOT;
    SETTINGS_UPDATE_FIELD(ps1_flags);
}

bool settings_get_ps2_autoboot(void) {
    return (settings.ps2_flags & SETTINGS_PS2_FLAGS_AUTOBOOT);
}

void settings_set_ps2_autoboot(bool autoboot) {
    if (autoboot != settings_get_ps2_autoboot())
        settings.ps2_flags ^= SETTINGS_PS2_FLAGS_AUTOBOOT;
    SETTINGS_UPDATE_FIELD(ps2_flags);
}

uint8_t settings_get_display_timeout() {
    return settings.display_timeout;
}

uint8_t settings_get_display_contrast() {
    return settings.display_contrast;
}

uint8_t settings_get_display_vcomh() {
    return settings.display_vcomh;
}

void settings_set_display_timeout(uint8_t display_timeout) {
    settings.display_timeout = display_timeout;
    SETTINGS_UPDATE_FIELD(display_timeout);
}

void settings_set_display_contrast(uint8_t display_contrast) {
    settings.display_contrast = display_contrast;
    SETTINGS_UPDATE_FIELD(display_contrast);
}

void settings_set_display_vcomh(uint8_t display_vcomh) {
    settings.display_vcomh = display_vcomh;
    SETTINGS_UPDATE_FIELD(display_vcomh);
}

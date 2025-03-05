#include "settings.h"

#include <stdio.h>
#include <string.h>

#include "debug.h"
#include "pico/multicore.h"
#include "sd.h"
#include "wear_leveling/wear_leveling.h"

#include "ini.h"

/* NOTE: for any change to the layout/size of this structure (that gets shipped to users),
   ensure to increase the version magic below -- this will trigger setting reset on next boot */
typedef struct {
    uint32_t version_magic;
    uint16_t ps1_card;
    uint16_t ps2_card;
    uint8_t ps1_channel;
    uint8_t ps2_channel;
    uint8_t ps1_boot_channel;
    uint8_t ps2_boot_channel;
    uint8_t ps1_flags; // TODO: single bit options: freepsxboot, pocketstation, freepsxboot slot
    // TODO: more ps1 settings: model for freepsxboot
    uint8_t ps2_flags; // TODO: single bit options: autoboot
    uint8_t sys_flags; // TODO: single bit options: whether ps1 or ps2 mode, etc
    uint8_t display_timeout; // display - auto off, in seconds, 0 - off
    uint8_t display_contrast; // display - contrast, 0-255
    uint8_t display_vcomh; // display - vcomh, valid values are 0x00, 0x20, 0x30 and 0x40
    uint8_t ps2_cardsize;
    // TODO: how do we store last used channel for cards that use autodetecting w/ gameid?
    uint8_t ps2_variant; // Variant for keys
} settings_t;

typedef struct {
    uint8_t ps2_flags;
    uint8_t ps1_flags;
    uint8_t sys_flags;
    uint8_t ps2_cardsize;
    uint8_t ps2_variant; // Variant for keys
} serialized_settings_t;

#define SETTINGS_UPDATE_FIELD(field) settings_update_part(&settings.field, sizeof(settings.field))

#define SETTINGS_VERSION_MAGIC              (0xAACD0006)
#define SETTINGS_PS1_FLAGS_AUTOBOOT         (0b0000001)
#define SETTINGS_PS1_FLAGS_GAME_ID          (0b0000010)
#define SETTINGS_PS2_FLAGS_AUTOBOOT         (0b0000001)
#define SETTINGS_PS2_FLAGS_GAME_ID          (0b0000010)
#define SETTINGS_SYS_FLAGS_PS2_MODE         (0b0000001)
#define SETTINGS_SYS_FLAGS_FLIPPED_DISPLAY  (0b0000010)

_Static_assert(sizeof(settings_t) == 20, "unexpected padding in the settings structure");

static settings_t settings;
static serialized_settings_t serialized_settings;
static int tempmode;
static const char settings_path[] = "/.sd2psx/settings.ini";

static void settings_update_part(void *settings_ptr, uint32_t sz);
static void settings_serialize(void);

static int parse_card_configuration(void *user, const char *section, const char *name, const char *value) {
    serialized_settings_t* _s = user;

    #define MATCH(s, n) strcmp(section, s) == 0 && strcmp(name, n) == 0
    #define DIFFERS(v, s) ((strcmp(v, "ON") == 0) != s)
    if (MATCH("PS1", "Autoboot")
        && DIFFERS(value, ((_s->ps1_flags & SETTINGS_PS1_FLAGS_AUTOBOOT) > 0))) {
        _s->ps1_flags ^= SETTINGS_PS1_FLAGS_AUTOBOOT;
    } else if (MATCH("PS1", "GameID")
        && DIFFERS(value, ((_s->ps1_flags & SETTINGS_PS1_FLAGS_GAME_ID) > 0))) {
        _s->ps1_flags ^= SETTINGS_PS1_FLAGS_GAME_ID;
    } else if (MATCH("PS2", "Autoboot")
        && DIFFERS(value, ((_s->ps2_flags & SETTINGS_PS2_FLAGS_AUTOBOOT) > 0))) {
        _s->ps2_flags ^= SETTINGS_PS2_FLAGS_AUTOBOOT;
    } else if (MATCH("PS2", "GameID")
        && DIFFERS(value, ((_s->ps2_flags & SETTINGS_PS2_FLAGS_GAME_ID) > 0))) {
        _s->ps1_flags ^= SETTINGS_PS2_FLAGS_GAME_ID;
    } else if (MATCH("PS2", "CardSize")) {
        int size = atoi(value);
        switch (size) {
            case 1:
            case 2:
            case 4:
            case 8:
            case 16:
            case 32:
            case 64:
                _s->ps2_cardsize = size;
                break;
            default:
                break;
        }
    }else if (MATCH("PS2", "Variant")) {
        _s->ps2_variant = PS2_VARIANT_RETAIL;
        if (strcmp(value, "PROTO") == 0) {
            _s->ps2_variant = PS2_VARIANT_PROTO;
        } else if (strcmp(value, "ARCADE") == 0) {
            _s->ps2_variant = PS2_VARIANT_COH;
        }
    } else if (MATCH("General", "Mode")
        && (strcmp(value, "PS2") == 0) != ((_s->sys_flags & SETTINGS_SYS_FLAGS_PS2_MODE) > 0)) {
        _s->sys_flags ^= SETTINGS_SYS_FLAGS_PS2_MODE;
    } else if (MATCH("General", "FlippedScreen")
        && DIFFERS(value, ((_s->sys_flags & SETTINGS_SYS_FLAGS_FLIPPED_DISPLAY) > 0))) {
        _s->sys_flags ^= SETTINGS_SYS_FLAGS_FLIPPED_DISPLAY;
    }
    #undef MATCH
    return 1;
}

static void settings_deserialize(void) {
    int fd;

    fd = sd_open(settings_path, O_RDONLY);
    if (fd >= 0) {

        serialized_settings_t newSettings = {.ps2_flags = settings.ps2_flags,
                                             .sys_flags = settings.sys_flags,
                                             .ps2_cardsize = settings.ps2_cardsize,
                                             .ps2_variant = settings.ps2_variant,
                                             .ps1_flags = settings.ps1_flags};
        serialized_settings = newSettings;
        ini_parse_sd_file(fd, parse_card_configuration, &newSettings);
        sd_close(fd);
        if (memcmp(&newSettings, &serialized_settings, sizeof(serialized_settings))) {
            printf("Updating settings from ini\n");
            serialized_settings = newSettings;
            settings.sys_flags       = newSettings.sys_flags;
            settings.ps2_flags       = newSettings.ps2_flags;
            settings.ps2_cardsize    = newSettings.ps2_cardsize;
            settings.ps2_variant     = newSettings.ps2_variant;
            settings.ps1_flags       = newSettings.ps1_flags;

            wear_leveling_write(0, &settings, sizeof(settings));
        }
    }
}

static void settings_serialize(void) {
    int fd;
    // Only serialize if required
    if (serialized_settings.ps2_cardsize == settings.ps2_cardsize &&
        serialized_settings.ps2_flags == settings.ps2_flags &&
        serialized_settings.sys_flags == settings.sys_flags &&
        serialized_settings.ps2_variant == settings.ps2_variant &&
        serialized_settings.ps1_flags == settings.ps1_flags) {
        return;
    }

    if (!sd_exists("/.sd2psx/")) {
        sd_mkdir("/.sd2psx/");
    }
    fd = sd_open(settings_path, O_RDWR | O_CREAT);
    if (fd >= 0) {
        printf("Serializing Settings\n");
        char line_buffer[256] = { 0x0 };
        int written = snprintf(line_buffer, 256, "[General]\n");
        sd_write(fd, line_buffer, written);
        written = snprintf(line_buffer, 256, "Mode=%s\n", ((settings.sys_flags & SETTINGS_SYS_FLAGS_PS2_MODE) > 0) ? "PS2" : "PS1");
        sd_write(fd, line_buffer, written);
        written = snprintf(line_buffer, 256, "FlippedScreen=%s\n", ((settings.sys_flags & SETTINGS_SYS_FLAGS_FLIPPED_DISPLAY) > 0) ? "ON" : "OFF");
        sd_write(fd, line_buffer, written);
        written = snprintf(line_buffer, 256, "[PS1]\n");
        sd_write(fd, line_buffer, written);
        written = snprintf(line_buffer, 256, "Autoboot=%s\n", ((settings.ps1_flags & SETTINGS_PS1_FLAGS_AUTOBOOT) > 0) ? "ON" : "OFF");
        sd_write(fd, line_buffer, written);
        written = snprintf(line_buffer, 256, "GameID=%s\n", ((settings.ps1_flags & SETTINGS_PS1_FLAGS_GAME_ID) > 0) ? "ON" : "OFF");
        sd_write(fd, line_buffer, written);
        written = snprintf(line_buffer, 256, "[PS2]\n");
        sd_write(fd, line_buffer, written);
        written = snprintf(line_buffer, 256, "Autoboot=%s\n", ((settings.ps2_flags & SETTINGS_PS2_FLAGS_AUTOBOOT) > 0) ? "ON" : "OFF");
        sd_write(fd, line_buffer, written);
        written = snprintf(line_buffer, 256, "GameID=%s\n", ((settings.ps2_flags & SETTINGS_PS2_FLAGS_GAME_ID) > 0) ? "ON" : "OFF");
        sd_write(fd, line_buffer, written);
        written = snprintf(line_buffer, 256, "CardSize=%u\n", settings.ps2_cardsize);
        sd_write(fd, line_buffer, written);
        switch (settings.ps2_variant) {
            case PS2_VARIANT_PROTO:
                written = snprintf(line_buffer, 256, "Variant=PROTO\n" );
                break;
            case PS2_VARIANT_COH:
                written = snprintf(line_buffer, 256, "Variant=ARCADE\n" );
                break;
            case PS2_VARIANT_RETAIL:
            default:
                written = snprintf(line_buffer, 256, "Variant=RETAIL\n" );
                break;

        }
        sd_write(fd, line_buffer, written);

        sd_close(fd);
    }
    serialized_settings.sys_flags       = settings.sys_flags;
    serialized_settings.ps2_flags       = settings.ps2_flags;
    serialized_settings.ps2_cardsize    = settings.ps2_cardsize;
    serialized_settings.ps2_variant     = settings.ps2_variant;
    serialized_settings.ps1_flags       = settings.ps1_flags;
}

static void settings_reset(void) {
    memset(&settings, 0, sizeof(settings));
    settings.version_magic = SETTINGS_VERSION_MAGIC;
    settings.display_timeout = 0; // off
    settings.display_contrast = 255; // 100%
    settings.display_vcomh = 0x30; // 0.83 x VCC
    settings.ps1_flags = SETTINGS_PS1_FLAGS_GAME_ID;
    settings.ps2_flags = SETTINGS_PS2_FLAGS_GAME_ID;
    settings.ps2_cardsize = 8;
    settings.ps2_variant = PS2_VARIANT_RETAIL;
    if (wear_leveling_write(0, &settings, sizeof(settings)) == WEAR_LEVELING_FAILED)
        fatal("failed to reset settings");
}

void settings_load_sd(void) {
    sd_init();
    if (sd_exists(settings_path)) {
        printf("Reading settings from %s\n", settings_path);
        settings_deserialize();
    } else {
        settings_serialize();
    }
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


    tempmode = settings.sys_flags & SETTINGS_SYS_FLAGS_PS2_MODE;
}

static void settings_update_part(void *settings_ptr, uint32_t sz) {
    if (multicore_lockout_victim_is_initialized(1))
       multicore_lockout_start_blocking();
    wear_leveling_write((uint8_t*)settings_ptr - (uint8_t*)&settings, settings_ptr, sz);
    if (multicore_lockout_victim_is_initialized(1))
        multicore_lockout_end_blocking();
    settings_serialize();
}


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

int settings_get_ps2_boot_channel(void) {
    if (settings.ps2_boot_channel < CHAN_MIN || settings.ps2_boot_channel > CHAN_MAX)
        return CHAN_MIN;
    return settings.ps2_boot_channel;
}

uint8_t settings_get_ps2_cardsize(void) {
#ifdef FEAT_PS2_CARDSIZE
    return settings.ps2_cardsize;
#else
    return 8;
#endif
}

int settings_get_ps2_variant(void) {
    return settings.ps2_variant;
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

void settings_set_ps2_boot_channel(int chan) {
    if (chan != settings.ps2_boot_channel) {
        settings.ps2_boot_channel = chan;
        SETTINGS_UPDATE_FIELD(ps2_boot_channel);
    }
}

void settings_set_ps2_cardsize(uint8_t size) {
    if (size != settings.ps2_cardsize) {
        settings.ps2_cardsize = size;
        SETTINGS_UPDATE_FIELD(ps2_cardsize);
    }
}

void settings_set_ps2_variant(int x) {
    if (settings.ps2_variant != x) {
        settings.ps2_variant = x;
        SETTINGS_UPDATE_FIELD(ps2_variant);
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

int settings_get_ps1_boot_channel(void) {
    if (settings.ps1_boot_channel < CHAN_MIN || settings.ps1_boot_channel > CHAN_MAX)
        return CHAN_MIN;
    return settings.ps1_boot_channel;
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

void settings_set_ps1_boot_channel(int chan) {
    if (chan != settings.ps1_boot_channel) {
        settings.ps1_boot_channel = chan;
        SETTINGS_UPDATE_FIELD(ps1_boot_channel);
    }
}

int settings_get_mode(void) {
    if (tempmode == MODE_USB)
        return MODE_USB;
    else if ((settings.sys_flags & SETTINGS_SYS_FLAGS_PS2_MODE) != tempmode)
        return MODE_PS1;
    else
        return settings.sys_flags & SETTINGS_SYS_FLAGS_PS2_MODE;
}

void settings_set_mode(int mode) {
    if (mode == MODE_TEMP_PS1) {
        tempmode = MODE_TEMP_PS1;
        DPRINTF("Setting PS1 Tempmode\n");
        return;
    } else if (mode == MODE_USB) {
        tempmode = MODE_USB;
        DPRINTF("Setting USB Tempmode\n");
        return;
    } else if (mode != MODE_PS1 && mode != MODE_PS2)
        return;

    if (mode != settings_get_mode()) {
        /* clear old mode, then set what was passed in */
        settings.sys_flags &= ~SETTINGS_SYS_FLAGS_PS2_MODE;
        settings.sys_flags |= mode;
        SETTINGS_UPDATE_FIELD(sys_flags);
        tempmode = settings.sys_flags & SETTINGS_SYS_FLAGS_PS2_MODE;
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

bool settings_get_ps1_game_id(void) {
    return (settings.ps1_flags & SETTINGS_PS1_FLAGS_GAME_ID);
}

void settings_set_ps1_game_id(bool enabled) {
    if (enabled != settings_get_ps1_game_id())
        settings.ps1_flags ^= SETTINGS_PS1_FLAGS_GAME_ID;
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

bool settings_get_ps2_game_id(void) {
    return (settings.ps2_flags & SETTINGS_PS2_FLAGS_GAME_ID);
}

void settings_set_ps2_game_id(bool enabled) {
    if (enabled != settings_get_ps2_game_id())
        settings.ps2_flags ^= SETTINGS_PS2_FLAGS_GAME_ID;
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

bool settings_get_display_flipped() {
    return (settings.sys_flags & SETTINGS_SYS_FLAGS_FLIPPED_DISPLAY);
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

void settings_set_display_flipped(bool flipped) {
    if (flipped != settings_get_display_flipped())
        settings.sys_flags ^= SETTINGS_SYS_FLAGS_FLIPPED_DISPLAY;
    SETTINGS_UPDATE_FIELD(sys_flags);
}
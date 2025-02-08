#include "card_config.h"
#include "settings.h"
#include "sd.h"
#include "debug.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ini.h"


#if LOG_LEVEL_CARD_CONF == 0
    #define log(x...)
#else
    #define log(level, fmt, x...) LOG_PRINT(LOG_LEVEL_CARD_CONF, level, fmt, ##x)
#endif

#define MAX_CFG_PATH_LENGTH         (64)
#define CUSTOM_CARDS_CONFIG_PATH    (".sd2psx/Game2Folder.ini")

typedef struct {
    const char *channel_number;
    char *channel_name;
    size_t channel_name_max_len;
    uint8_t card_size;
    uint8_t max_channels;
} parse_card_config_t;

typedef struct {
    const char *game_id;
    const char *mode;
    char *card_folder;
    size_t card_folder_max_len;
} parse_custom_card_folder_t;

static int parse_custom_card_folder(void *user, const char *section, const char *name, const char *value) {
    parse_custom_card_folder_t *ctx = user;

    #define MATCH(s, n) strcmp(section, s) == 0 && strcmp(name, n) == 0
    if (MATCH(ctx->mode, ctx->game_id)) {
        if (strlen(value) <= ctx->card_folder_max_len) {
            strlcpy(ctx->card_folder, value, ctx->card_folder_max_len);
        }
    }
    #undef MATCH

    log(LOG_TRACE, "s=%s n=%s card_folder=%s\n", section, name, ctx->card_folder);

    return 1;
}

static int parse_card_configuration(void *user, const char *section, const char *name, const char *value) {
    parse_card_config_t *ctx = user;

    #define MATCH(s, n) strcmp(section, s) == 0 && strcmp(name, n) == 0
    if (MATCH("ChannelName", ctx->channel_number)) {
        if (strlen(value) <= ctx->channel_name_max_len) {
            strcpy(ctx->channel_name, value);
        }
    } else if (MATCH("Settings", "CardSize")) {
        int size = atoi(value);
        switch (size) {
            case 1:
            case 2:
            case 4:
            case 8:
            case 16:
            case 32:
            case 64:
                ctx->card_size = size;
                break;
            default:
                break;
        }
    } else if (MATCH("Settings", "MaxChannels")) {
        int max_channels = atoi(value);
        if (max_channels > 0 && max_channels <= UINT8_MAX) {
            ctx->max_channels = max_channels;
        }
    }
    #undef MATCH

    return 1;
}

static void card_config_get_ini_name(const char* card_folder, const char* card_base, char* config_path) {
    if (settings_get_mode() == MODE_PS1) {
        snprintf(config_path, MAX_CFG_PATH_LENGTH, "MemoryCards/PS1/%s/%s.ini", card_folder, card_base);
    } else {
        switch (settings_get_ps2_variant()) {
            case PS2_VARIANT_PROTO:
                snprintf(config_path, MAX_CFG_PATH_LENGTH, "MemoryCards/PROT/%s/%s.ini", card_folder, card_base);
            break;
            case PS2_VARIANT_COH:
                snprintf(config_path, MAX_CFG_PATH_LENGTH, "MemoryCards/COH/%s/%s.ini", card_folder, card_base);
            break;
            case PS2_VARIANT_RETAIL:
            default:
                snprintf(config_path, MAX_CFG_PATH_LENGTH, "MemoryCards/PS2/%s/%s.ini", card_folder, card_base);
            break;
        }
    }
    log(LOG_TRACE, "config_path=%s\n", config_path);

}

void card_config_read_channel_name(const char* card_folder, const char* card_base, const char* channel_number, char* name, size_t name_max_len) {
    char config_path[64];
    int fd;

    card_config_get_ini_name(card_folder, card_base, config_path);

    fd = sd_open(config_path, O_RDONLY);
    if (fd >= 0) {
        parse_card_config_t ctx = {
            .channel_number = channel_number,
            .channel_name = name,
            .channel_name_max_len = name_max_len,
            .card_size = 0,
            .max_channels = 8
        };
        ini_parse_sd_file(fd, parse_card_configuration, &ctx);
        sd_close(fd);
    }
}

uint8_t card_config_get_ps2_cardsize(const char* card_folder, const char* card_base) {
    char config_path[64];
    int fd;
    parse_card_config_t ctx = {
        .channel_number = NULL,
        .channel_name = NULL,
        .channel_name_max_len = 0,
        .card_size = 0,
        .max_channels = 8
    };

    card_config_get_ini_name(card_folder, card_base, config_path);

    fd = sd_open(config_path, O_RDONLY);
    if (fd >= 0) {
        ini_parse_sd_file(fd, parse_card_configuration, &ctx);
        sd_close(fd);
    }
    return ctx.card_size;
}

uint8_t card_config_get_max_channels(const char* card_folder, const char* card_base) {
    char config_path[MAX_CFG_PATH_LENGTH];
    int fd;
    parse_card_config_t ctx = {
        .channel_number = NULL,
        .channel_name = NULL,
        .channel_name_max_len = 0,
        .card_size = 0,
        .max_channels = 8
    };

    card_config_get_ini_name(card_folder, card_base, config_path);

    fd = sd_open(config_path, O_RDONLY);
    if (fd >= 0) {
        log(LOG_TRACE, "config_path=%s\n", config_path);
        ini_parse_sd_file(fd, parse_card_configuration, &ctx);
        sd_close(fd);
    }
    log(LOG_TRACE, "max_channels=%d\n", ctx.max_channels);
    return ctx.max_channels;
}


void card_config_get_card_folder(const char* game_id, char* card_folder, size_t card_folder_max_len) {
    char mode[6];
    parse_custom_card_folder_t ctx = {
        .game_id = game_id,
        .mode = mode,
        .card_folder = card_folder,
        .card_folder_max_len = card_folder_max_len
    };

    if (settings_get_mode() == MODE_PS1) {
        snprintf(mode, sizeof(mode), "PS1");
    } else {
        switch (settings_get_ps2_variant()) {
            case PS2_VARIANT_PROTO:
                snprintf(mode, sizeof(mode), "PROT");
            break;
            case PS2_VARIANT_COH:
                snprintf(mode, sizeof(mode), "COH");
            break;
            case PS2_VARIANT_RETAIL:
            default:
                snprintf(mode, sizeof(mode), "PS2");
            break;
        }
    }
    int fd = sd_open(CUSTOM_CARDS_CONFIG_PATH, O_RDONLY);
    log(LOG_TRACE, "Looking for game_id=%s mode=%s \n", game_id, ctx.mode);

    if (fd >= 0) {
        ini_parse_sd_file(fd, parse_custom_card_folder, &ctx);
        sd_close(fd);
    }

    log(LOG_TRACE, "found card_folder=%s\n", card_folder);
}

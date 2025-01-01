#include "card_config.h"
#include "settings.h"
#include "sd.h"

#include <stdio.h>
#include <string.h>

#include "ini.h"

typedef struct {
    const char *channel_number;
    char *channel_name;
    size_t channel_name_max_len;
} parse_ctx_t;

static int parse_card_configuration(void *user, const char *section, const char *name, const char *value) {
    parse_ctx_t *ctx = user;

    #define MATCH(s, n) strcmp(section, s) == 0 && strcmp(name, n) == 0
    if (MATCH("ChannelName", ctx->channel_number)) {
        if (strlen(value) <= ctx->channel_name_max_len) {
            strcpy(ctx->channel_name, value);
        }
    }
    #undef MATCH

    return 1;
}

void card_config_read_channel_name(const char* card_folder, const char* card_base, const char* channel_number, char* name, size_t name_max_len) {
    char config_path[64];
    int fd;

    if (settings_get_mode() == MODE_PS1) {
        snprintf(config_path, sizeof(config_path), "MemoryCards/PS1/%s/%s.ini", card_folder, card_base);
    } else {
        snprintf(config_path, sizeof(config_path), "MemoryCards/PS2/%s/%s.ini", card_folder, card_base);
    }

    fd = sd_open(config_path, O_RDONLY);
    if (fd >= 0) {
        parse_ctx_t ctx = {
            .channel_number = channel_number,
            .channel_name = name,
            .channel_name_max_len = name_max_len
        };
        ini_parse_sd_file(fd, parse_card_configuration, &ctx);
        sd_close(fd);
    }
}


#include "game_db.h"

#include <ctype.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "pico/platform.h"

#include "debug.h"
#include "sd.h"
#include "settings.h"

#define MAX_GAME_NAME_LENGTH (127)
#define MAX_PREFIX_LENGTH    (5)
#define MAX_STRING_ID_LENGTH (10)
#define MAX_PATH_LENGTH      (64)

extern const char _binary_gamedbps1_dat_start, _binary_gamedbps1_dat_size;
extern const char _binary_gamedbps2_dat_start, _binary_gamedbps2_dat_size;
extern const char _binary_gamedbcoh_dat_start, _binary_gamedbcoh_dat_size;

typedef struct {
    size_t offset;
    uint32_t game_id;
    uint32_t parent_id;
    int mode;
    int id_length;
    const char* name;
    char prefix[MAX_PREFIX_LENGTH];
} game_lookup;

static game_lookup current_game;

bool __time_critical_func(game_db_sanity_check_title_id)(const char* const title_id) {
    uint8_t i = 0U;

    if ((settings_get_mode(true) == MODE_PS2) && (settings_get_ps2_variant() == PS2_VARIANT_COH)) {
        if ((title_id[0] != 'N') || (title_id[1] != 'M')) {
            return false;
        } else {
            i = 2;
            while (title_id[i] != 0x00) {
                if (!isdigit((int)title_id[i])) {
                    return false;
                }
                i++;
            }

        }
    } else {
        char splittable_game_id[MAX_GAME_ID_LENGTH];
        strlcpy(splittable_game_id, title_id, MAX_GAME_ID_LENGTH);
        char* prefix = strtok(splittable_game_id, "-");
        char* id = strtok(NULL, "-");

        while (prefix[i] != 0x00) {
            if (!isalpha((int)prefix[i])) {
                return false;
            }
            i++;
        }
        if (i == 0) {
            return false;
        } else {
            i = 0;
        }

        while (id[i] != 0x00) {
            if (!isdigit((int)id[i])) {
                return false;
            }
            i++;
        }
    }

    return (i > 0);
}

#pragma GCC diagnostic ignored "-Warray-bounds"
static uint32_t game_db_char_array_to_uint32(const char in[4]) {
    char inter[4] = {in[3], in[2], in[1], in[0]};
    uint32_t tgt;
    memcpy((void*)&tgt, (void*)inter, sizeof(tgt));
    return tgt;
}
#pragma GCC diagnostic pop

static uint32_t game_db_find_prefix_offset(uint32_t numericPrefix, const char* const db_start) {
    uint32_t offset = UINT32_MAX;

    const char* pointer = db_start;

    while (offset == UINT32_MAX) {
        uint32_t currentprefix = game_db_char_array_to_uint32(pointer), currentoffset = game_db_char_array_to_uint32(&pointer[4]);

        if (currentprefix == numericPrefix) {
            offset = currentoffset;
        }
        if ((currentprefix == 0U) && (currentoffset == 0U)) {
            break;
        }
        pointer += 8;
    }

    return offset;
}

static game_lookup build_game_lookup(const char* const db_start, const size_t db_size, const size_t offset) {
    game_lookup game = {};
    size_t name_offset;
    game.game_id = game_db_char_array_to_uint32(&(db_start)[offset]);
    game.offset = offset;
    game.parent_id = game_db_char_array_to_uint32(&(db_start)[offset + 8]);
    name_offset = game_db_char_array_to_uint32(&(db_start)[offset + 4]);
    if ((name_offset < db_size) && ((db_start)[name_offset] != 0x00))
        game.name = &((db_start)[name_offset]);
    else
        game.name = NULL;

    return game;
}

static game_lookup build_arcade_lookup(const char* const db_start, const size_t db_size, const size_t offset) {
    game_lookup game = {};
    size_t name_offset;
    game.game_id = game_db_char_array_to_uint32(&(db_start)[offset]);
    game.offset = offset;
    game.parent_id = game.game_id;
    name_offset = game_db_char_array_to_uint32(&(db_start)[offset + 4]);
    if ((name_offset < db_size) && ((db_start)[name_offset] != 0x00))
        game.name = &((db_start)[name_offset]);
    else
        game.name = NULL;

    return game;
}

static game_lookup find_game_lookup(const char* game_id, int mode) {
    char prefixString[MAX_PREFIX_LENGTH] = {};
    char idString[10] = {};
    uint32_t numeric_id = 0, numeric_prefix = 0;

    const char* const db_start = mode == MODE_PS1 ? &_binary_gamedbps1_dat_start : &_binary_gamedbps2_dat_start;
    const char* const db_size = mode == MODE_PS1 ? &_binary_gamedbps1_dat_size : &_binary_gamedbps2_dat_size;

    uint32_t prefixOffset = 0;
    game_lookup ret = {
        .game_id = 0U,
        .parent_id = 0U,
        .mode = -1,
        .id_length = 0,
        .name = NULL,
        .prefix = {}
    };


    if (game_id != NULL && game_id[0]) {
        char* copy = strdup(game_id);
        char* split = strtok(copy, "-");

        if (strlen(split) > 0) {
            strlcpy(prefixString, split, MAX_PREFIX_LENGTH);
            for (uint8_t i = 0; i < MAX_PREFIX_LENGTH - 1; i++) {
                prefixString[i] = toupper((unsigned char)prefixString[i]);
            }
        }

        split = strtok(NULL, "-");

        if (strlen(split) > 0) {
            strlcpy(idString, split, 11);
            numeric_id = atoi(idString);
        }

        free(copy);
    }

    numeric_prefix = game_db_char_array_to_uint32(prefixString);

    if (numeric_id != 0) {

        prefixOffset = game_db_find_prefix_offset(numeric_prefix, db_start);

        if (prefixOffset < (size_t)db_size) {
            uint32_t offset = prefixOffset;
            game_lookup game;
            do {
                game = build_game_lookup(db_start, (size_t)db_size, offset);

                if (game.game_id == numeric_id) {
                    ret = game;
                    DPRINTF("Found ID - Name Offset: %d, Parent ID: %d\n", (int)game.name, game.parent_id);
                    DPRINTF("Name:%s\n", game.name);
                    ret.mode = mode;
                    ret.id_length = strlen(idString);
                    memcpy(ret.prefix, prefixString, 4);
                }
                offset += 12;
            } while ((game.game_id != 0) && (offset < (size_t)db_size) && (ret.game_id == 0));
        }
    }

    return ret;
}


static game_lookup find_arcade_lookup(const char* game_id) {
    char idString[10] = {};
    uint32_t numeric_id = 0;

    const char* const db_start = &_binary_gamedbcoh_dat_start;
    const char* const db_size = &_binary_gamedbcoh_dat_size;

    game_lookup ret = {
        .game_id = 0U,
        .parent_id = 0U,
        .mode = -1,
        .id_length = 0,
        .name = NULL,
        .prefix = {}
    };


    if (game_id != NULL && game_id[0] == 'N' && game_id[1] == 'M') {
        strlcpy(idString, &game_id[2], 10);
        numeric_id = atoi(idString);
    }

    if (numeric_id != 0) {

        uint32_t offset = 0;
        game_lookup game;
        do {
            game = build_arcade_lookup(db_start, (size_t)db_size, offset);

            if (game.game_id == numeric_id) {
                ret = game;
                DPRINTF("Found ID - Name Offset: %d, Parent ID: %d\n", (int)game.name, game.parent_id);
                DPRINTF("Name:%s\n", game.name);
                ret.mode = MODE_PS2;
                ret.id_length = strlen(idString);
                memcpy(ret.prefix, "NM", 2);
            } else {
                DPRINTF("Game ID: %d - %s\n", game.game_id, game.name);
            }
            offset += 8;
        } while ((game.game_id != 0) && (offset < (size_t)db_size) && (ret.game_id == 0));

    }

    return ret;
}

void __time_critical_func(game_db_extract_title_id)(const uint8_t* const in_title_id, char* const out_title_id, const size_t in_title_id_length, const size_t out_buffer_size) {
    uint16_t idx_in_title = 0, idx_out_title = 0;
    uint8_t prefix_count = 0;

    while ( (in_title_id[idx_in_title] != 0x00)
            && (idx_in_title < in_title_id_length)
            && (idx_out_title < out_buffer_size) ) {
        if ((in_title_id[idx_in_title] == ';') || (in_title_id[idx_in_title] == 0x00)) {
            out_title_id[idx_out_title++] = 0x00;
            break;
        } else if ((in_title_id[idx_in_title] == '\\') || (in_title_id[idx_in_title] == '/') || (in_title_id[idx_in_title] == ':')) {
            idx_out_title = 0;
            prefix_count = 0;
        } else if (in_title_id[idx_in_title] == '_') {
            out_title_id[idx_out_title++] = '-';
        } else if (isalpha((unsigned char)in_title_id[idx_in_title])) {
            if (prefix_count++ < 4)
                out_title_id[idx_out_title++] = in_title_id[idx_in_title];
            else if((unsigned char)in_title_id[idx_in_title] == 'P')
                out_title_id[idx_out_title++] = '-';
        } else if (in_title_id[idx_in_title] != '.') {
            out_title_id[idx_out_title++] = in_title_id[idx_in_title];
        } else {
        }
        idx_in_title++;
    }
}

void game_db_get_current_name(char* const game_name) {
    strlcpy(game_name, "", MAX_GAME_NAME_LENGTH);

    if ((current_game.name != NULL) && (current_game.name[0] != 0)) {
        strlcpy(game_name, current_game.name, MAX_GAME_NAME_LENGTH);
    }
}

int game_db_get_current_parent(char* const parent_id) {

    if ((settings_get_mode(true) == MODE_PS1)
        && (current_game.mode == MODE_PS2)) {
        game_db_init();
        return -1;
    }
    if (current_game.parent_id != 0)
        snprintf(parent_id, MAX_GAME_ID_LENGTH, "%s-%0*d", current_game.prefix, current_game.id_length, (int)current_game.parent_id);

    DPRINTF("Parent ID: %s\n", parent_id);

    return current_game.mode;
}

int game_db_update_game(const char* const game_id) {
    int mode = settings_get_mode(true);

    current_game = find_game_lookup(game_id, mode);

    if ((current_game.game_id == 0) && (mode == MODE_PS2)) {
        current_game = find_game_lookup(game_id, MODE_PS1);
    }

    if (current_game.name == NULL)
    {
        current_game.parent_id = current_game.game_id;
    }

    return current_game.mode;
}

int game_db_update_arcade(const char* const game_id) {

    current_game = find_arcade_lookup(game_id);

    if (current_game.name == NULL)
    {
        current_game.parent_id = current_game.game_id;
    }

    return current_game.mode;
}

void game_db_get_game_name(const char* game_id, char* game_name) {
    if (!game_db_sanity_check_title_id(game_id))
        return;

    if ((settings_get_mode(true) == MODE_PS2) && (settings_get_ps2_variant() == PS2_VARIANT_COH)) {
        game_lookup lookup = find_arcade_lookup(game_id);
        if (lookup.name && lookup.name[0])
            strlcpy(game_name, lookup.name, MAX_GAME_NAME_LENGTH);
    } else {
        game_lookup lookup = find_game_lookup(game_id, settings_get_mode(true));
        if (lookup.name && lookup.name[0])
            strlcpy(game_name, lookup.name, MAX_GAME_NAME_LENGTH);
    }
}

void game_db_init(void) {
    current_game.game_id = 0U;
    current_game.parent_id = 0U;
    current_game.mode = -1;
    current_game.id_length = 0;
    current_game.name = NULL;
    memset(current_game.prefix, 0x00, MAX_PREFIX_LENGTH);
}
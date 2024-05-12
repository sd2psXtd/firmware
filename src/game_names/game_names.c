
#include "game_names.h"

#include <ctype.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "pico/platform.h"


#include "../debug.h"
#include "../sd.h"
#include "../settings.h"

#define MAX_GAME_NAME_LENGTH (127)
#define MAX_PREFIX_LENGTH    (5)
#define MAX_GAME_ID_LENGTH   (16)
#define MAX_STRING_ID_LENGTH (10)
#define MAX_PATH_LENGTH      (64)

extern const char _binary_gamedbps1_dat_start, _binary_gamedbps1_dat_size;
extern const char _binary_gamedbps2_dat_start, _binary_gamedbps2_dat_size;

typedef struct {
    size_t offset;
    uint32_t game_id;
    uint32_t parent_id;
    const char* name;
} game_lookup;

bool __time_critical_func(game_names_sanity_check_title_id)(const char* const title_id) {
    uint8_t i = 0U;

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

    while (prefix[i] != 0x00) {
        if (!isdigit((int)id[i])) {
            return false;
        }
        i++;
    }

    return (i > 0);
}

#pragma GCC diagnostic ignored "-Warray-bounds"
static uint32_t game_names_char_array_to_uint32(const char in[4]) {
    char inter[4] = {in[3], in[2], in[1], in[0]};
    uint32_t tgt;
    memcpy((void*)&tgt, (void*)inter, sizeof(tgt));
    return tgt;
}
#pragma GCC diagnostic pop

static uint32_t game_names_find_prefix_offset(uint32_t numericPrefix, const char* const db_start) {
    uint32_t offset = UINT32_MAX;

    const char* pointer = db_start;

    while (offset == UINT32_MAX) {
        uint32_t currentprefix = game_names_char_array_to_uint32(pointer), currentoffset = game_names_char_array_to_uint32(&pointer[4]);

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
    game.game_id = game_names_char_array_to_uint32(&(db_start)[offset]);
    game.offset = offset;
    game.parent_id = game_names_char_array_to_uint32(&(db_start)[offset + 8]);
    name_offset = game_names_char_array_to_uint32(&(db_start)[offset + 4]);
    if ((name_offset < db_size) && ((db_start)[name_offset] != 0x00))
        game.name = &((db_start)[name_offset]);
    else
        game.name = NULL;

    return game;
}

static bool file_has_extension(const char* const file, const char* const extension) {
    int file_index = strlen(file) - 1;
    int ext_index = strlen(extension) - 1;
    while ((file_index >= 0) && (ext_index >= 0))
        if (file[file_index--] != extension[ext_index--])
            return false;

    return true;
}

static void file_remove_extension(char* const file) {
    int file_index = strlen(file) - 1;
    while ((file_index >= 0) && (file[file_index] != '.')) {
        file[file_index--] = 0x00;
    }
    if (file_index >= 0)
        file[file_index] = 0x00;
}

static game_lookup find_game_lookup(const char* game_id) {
    char prefixString[MAX_PREFIX_LENGTH] = {};
    char idString[10] = {};
    uint32_t numeric_id = 0, numeric_prefix = 0;

    const char* const db_start = settings_get_mode() == MODE_PS1 ? &_binary_gamedbps1_dat_start : &_binary_gamedbps2_dat_start;
    const char* const db_size = settings_get_mode() == MODE_PS1 ? &_binary_gamedbps1_dat_size : &_binary_gamedbps2_dat_size;

    uint32_t prefixOffset = 0;
    game_lookup ret = {
        .game_id = 0U,
        .parent_id = 0U,
        .name = NULL
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
    }

    numeric_prefix = game_names_char_array_to_uint32(prefixString);

    if (numeric_id != 0) {
        
        prefixOffset = game_names_find_prefix_offset(numeric_prefix, db_start);

        if (prefixOffset < (size_t)db_size) {
            uint32_t offset = prefixOffset;
            game_lookup game;
            do {
                game = build_game_lookup(db_start, (size_t)db_size, offset);

                if (game.game_id == numeric_id) {
                    ret = game;
                    debug_printf("Found ID - Name Offset: %d, Parent ID: %d\n", (int)game.name, game.parent_id);
                    debug_printf("Name:%s\n", game.name);

                }
                offset += 12;
            } while ((game.game_id != 0) && (offset < (size_t)db_size) && (ret.game_id == 0));
        }
    }

    return ret;
}

void __time_critical_func(game_names_extract_title_id)(const uint8_t* const in_title_id, char* const out_title_id, const size_t in_title_id_length, const size_t out_buffer_size) {
    uint16_t idx_in_title = 0, idx_out_title = 0;

    while ( (in_title_id[idx_in_title] != 0x00) 
            && (idx_in_title < in_title_id_length)
            && (idx_out_title < out_buffer_size) ) {
        if ((in_title_id[idx_in_title] == ';') || (in_title_id[idx_in_title] == 0x00)) {
            out_title_id[idx_out_title++] = 0x00;
            break;
        } else if ((in_title_id[idx_in_title] == '\\') || (in_title_id[idx_in_title] == '/') || (in_title_id[idx_in_title] == ':')) {
            idx_out_title = 0;
        } else if (in_title_id[idx_in_title] == '_') {
            out_title_id[idx_out_title++] = '-';
        } else if (in_title_id[idx_in_title] != '.') {
            out_title_id[idx_out_title++] = in_title_id[idx_in_title];
        } else {
        }
        idx_in_title++;
    }
}

void game_names_get_name_by_folder(const char* const folder, char* const game_name) {
    strlcpy(game_name, "", MAX_GAME_NAME_LENGTH);    
    if (game_names_sanity_check_title_id(folder)) {
        game_lookup game;

        game = find_game_lookup(folder);

        if (game.game_id != 0) {
            strlcpy(game_name, game.name, MAX_GAME_NAME_LENGTH);
        }
    } else {
        int dir_fd, it_fd = -1;
        char filename[MAX_GAME_NAME_LENGTH] = {};
        char dir[64];
        if (settings_get_mode() == MODE_PS1) {
            snprintf(dir, sizeof(dir), "MemoryCards/PS1/%s", folder);
        } else {
            snprintf(dir, sizeof(dir), "MemoryCards/PS2/%s", folder);
        }

        dir_fd = sd_open(dir, O_RDONLY);
        if (dir_fd >= 0) {
            it_fd = sd_iterate_dir(dir_fd, it_fd);

            while (it_fd != -1) {
                sd_get_name(it_fd, filename, MAX_GAME_NAME_LENGTH);

                if (file_has_extension(filename, ".txt")) {
                    file_remove_extension(filename);
                    strlcpy(game_name, filename, MAX_GAME_NAME_LENGTH);
                    break;
                }
                it_fd = sd_iterate_dir(dir_fd, it_fd);
            }
            if (it_fd != -1)
                sd_close(it_fd);
            sd_close(dir_fd);
        }
    }
}

void game_names_get_parent(const char* const game_id, char* const parent_id) {
    char prefixString[MAX_PREFIX_LENGTH] = {};
    char idString[10] = {};
    game_lookup game;

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
        }
    }

    game = find_game_lookup(game_id);

    if (game.game_id != 0) {
        snprintf(parent_id, MAX_GAME_ID_LENGTH, "%s-%0*d", prefixString, (int)strlen(idString), (int)game.parent_id);

        debug_printf("Parent ID: %s\n", parent_id);
    } else {
        strlcpy(parent_id, game_id, MAX_GAME_ID_LENGTH);
    }
}


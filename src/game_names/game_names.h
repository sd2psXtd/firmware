#pragma once

#include <stddef.h>
#include <stdint.h>
#include <pico/platform.h>

void game_names_extract_title_id(const uint8_t* const in_title_id, char* const out_title_id, const size_t in_title_id_length, const size_t out_buffer_size);
bool game_names_sanity_check_title_id(const char* const title_id);

void game_names_get_name_by_folder(const char* const folder, char* const game_name);
void game_names_get_parent(const char* const game_id, char* const parent_id);

void game_names_update_card_dir(const char* const dir);
void game_names_init(void);

void game_names_set_game_id(const char* const game_id);
const char* game_names_get_game_id(void);

const char* game_names_get_game_name(void);
const char* game_names_name_from_file(char* path, int channel);

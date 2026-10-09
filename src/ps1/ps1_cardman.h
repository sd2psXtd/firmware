#pragma once

#include <stdint.h>
#include <stdbool.h>

#define PS1_CARD_IDX_SPECIAL 0

#define PS1_PAGE_SIZE 128
#define SD_BLOCK_SIZE 512

typedef enum {
    PS1_CM_STATE_NAMED,
    PS1_CM_STATE_BOOT,
    PS1_CM_STATE_GAMEID,
    PS1_CM_STATE_NORMAL
} ps1_cardman_state_t;

void ps1_cardman_init(void);
int ps1_cardman_read_sector(int sector, void *buf128);
int ps1_cardman_write_sd_blocks(void *buffer, int sd_blocks_count, int first_sd_block);
int ps1_cardman_write_isolated_sd_block(void *buffer, int sd_block);
void ps1_cardman_flush(void);
void ps1_cardman_open(void);
bool ps1_cardman_needs_update(void);
void ps1_cardman_close(void);
int ps1_cardman_get_idx(void);
int ps1_cardman_get_channel(void);
const char* ps1_cardman_get_folder_name(void);
ps1_cardman_state_t ps1_cardman_get_state(void);

void ps1_cardman_next_channel(void);
void ps1_cardman_prev_channel(void);
void ps1_cardman_next_idx(void);
void ps1_cardman_prev_idx(void);
void ps1_cardman_set_game_id(const char* card_game_id);
void ps1_cardman_switch_bootcard(void);
void ps1_cardman_switch_default(void);
void ps1_cardman_set_idx(int idx);
void ps1_cardman_set_channel(int channel);

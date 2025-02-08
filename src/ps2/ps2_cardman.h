#pragma once

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>


#define PS2_CARD_SIZE_128M      (128 * 1024 * 1024)
#define PS2_CARD_SIZE_64M       (64 * 1024 * 1024)
#define PS2_CARD_SIZE_32M       (32 * 1024 * 1024)
#define PS2_CARD_SIZE_16M       (16 * 1024 * 1024)
#define PS2_CARD_SIZE_8M        (8 * 1024 * 1024)
#define PS2_CARD_SIZE_4M        (4 * 1024 * 1024)
#define PS2_CARD_SIZE_2M        (2 * 1024 * 1024)
#define PS2_CARD_SIZE_1M        (1024 * 1024)
#define PS2_CARD_SIZE_512K      (512 * 1024)

#define PS2_CARD_IDX_SPECIAL 0

typedef enum  {
    PS2_CM_STATE_NAMED,
    PS2_CM_STATE_BOOT,
    PS2_CM_STATE_GAMEID,
    PS2_CM_STATE_NORMAL
} ps2_cardman_state_t;

extern int cardman_fd;

void ps2_cardman_init(void);
void ps2_cardman_task(void);
int ps2_cardman_read_sector(int sector, void *buf512);
int ps2_cardman_write_sector(int sector, void *buf512);
bool ps2_cardman_is_sector_available(int sector);
void ps2_cardman_mark_sector_available(int sector);
void ps2_cardman_set_priority_sector(int page_idx);
void ps2_cardman_flush(void);
void ps2_cardman_open(void);
void ps2_cardman_close(void);
int ps2_cardman_get_idx(void);
int ps2_cardman_get_channel(void);
uint32_t ps2_cardman_get_card_size(void);

void ps2_cardman_set_channel(uint16_t num);
void ps2_cardman_next_channel(void);
void ps2_cardman_prev_channel(void);

void ps2_cardman_switch_bootcard(void);

void ps2_cardman_set_idx(uint16_t num);
void ps2_cardman_next_idx(void);
void ps2_cardman_prev_idx(void);

typedef void (*cardman_cb_t)(int, bool);

void ps2_cardman_set_progress_cb(cardman_cb_t func);
char *ps2_cardman_get_progress_text(void);

void ps2_cardman_set_gameid(const char* game_id);
const char* ps2_cardman_get_folder_name(void);
ps2_cardman_state_t ps2_cardman_get_state(void);

void ps2_cardman_set_variant(int variant);

bool ps2_cardman_needs_update(void);
bool ps2_cardman_is_accessible(void);
bool ps2_cardman_is_idle(void);

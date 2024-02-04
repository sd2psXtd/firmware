#pragma once

#include <stdint.h>

#define PS2_CARD_SIZE_8M        (8 * 1024 * 1024)
#define PS2_CARD_SIZE_4M        (4 * 1024 * 1024)
#define PS2_CARD_SIZE_2M        (2 * 1024 * 1024)
#define PS2_CARD_SIZE_1M        (1024 * 1024)
#define PS2_CARD_SIZE_512K      (512 * 1024)

void ps2_cardman_init(void);
int ps2_cardman_write_sector(int sector, void *buf512);
void ps2_cardman_flush(void);
void ps2_cardman_open(void);
void ps2_cardman_close(void);
int ps2_cardman_get_idx(void);
int ps2_cardman_get_channel(void);
uint32_t ps2_cardman_get_card_size(void);

void ps2_cardman_next_channel(void);
void ps2_cardman_prev_channel(void);
void ps2_cardman_next_idx(void);
void ps2_cardman_prev_idx(void);

typedef void (*cardman_cb_t)(int);

void ps2_cardman_set_progress_cb(cardman_cb_t func);
char *ps2_cardman_get_progress_text(void);
#pragma once

#include "../ps2_dirty.h"
#include "psram/psram.h"
#include "debug.h"
#include "ps2_card_backend.h"

#include <pico/platform.h>
#include <stdint.h>


#define ERASE_SECTORS 16
#define CARD_SIZE     (8 * 1024 * 1024)

#define XOR8(a) (a[0] ^ a[1] ^ a[2] ^ a[3] ^ a[4] ^ a[5] ^ a[6] ^ a[7])
#define ARG8(a) a[0], a[1], a[2], a[3], a[4], a[5], a[6], a[7]

extern uint8_t term;
extern uint32_t read_sector, write_sector, erase_sector;
extern uint8_t writetmp[528];
extern int is_write, is_dma_read;
extern uint32_t readptr, writeptr;
extern uint8_t *eccptr;

extern const uint8_t EccTable[];

extern ps2_memory_card_backend_t* ps2_card_backend;

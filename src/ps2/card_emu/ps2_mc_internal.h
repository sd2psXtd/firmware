#pragma once

#include "../ps2_dirty.h"
#include "psram/psram.h"


#include <pico/platform.h>
#include <stdint.h>


#define ERASE_SECTORS 16
#define CARD_SIZE     (8 * 1024 * 1024)

#define XOR8(a) (a[0] ^ a[1] ^ a[2] ^ a[3] ^ a[4] ^ a[5] ^ a[6] ^ a[7])
#define ARG8(a) a[0], a[1], a[2], a[3], a[4], a[5], a[6], a[7]

enum { RECEIVE_RESET, RECEIVE_EXIT, RECEIVE_OK };

extern uint8_t term;
extern uint32_t read_sector, write_sector, erase_sector;
extern uint8_t writetmp[528];
extern int is_write, is_dma_read;
extern uint32_t readptr, writeptr;
extern uint8_t *eccptr;

extern uint8_t EccTable[];

extern uint8_t receive(uint8_t *cmd);
extern uint8_t receiveFirst(uint8_t *cmd);
extern void __time_critical_func(mc_respond)(uint8_t ch);
extern void __time_critical_func(read_mc)(uint32_t addr, void *buf, size_t sz, void (*cb)(void));
extern void __time_critical_func(write_mc)(uint32_t addr, void *buf, size_t sz);

#define receiveOrNextCmd(cmd)          \
    if (receive(cmd) == RECEIVE_RESET) \
    return

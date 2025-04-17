#include <ps2/card_emu/ps2_mc_data_interface.h>
#include <stdbool.h>
#include <string.h>


#include "pico/multicore.h"
#include "pico/time.h"
#include "ps1_mc_data_interface.h"
#include "bigmem.h"
#if WITH_PSRAM
#include "psram.h"
#endif
#include "ps1_cardman.h"
#include "ps1_dirty.h"

#include "debug.h"


#define QPRINTF(fmt, x...) printf(fmt, ##x)

#define PAGE_CACHE_SIZE 40
#define MAX_READ_AHEAD 0
#define PS1_CARD_SIZE 128 * 1024

static bool dma_in_progress = false;
static bool write_occured = false;


#define card cache


#if WITH_PSRAM
static void __time_critical_func(ps1_mc_data_interface_rx_done)() {
    dma_in_progress = false;

    ps1_dirty_unlock();
}

void __time_critical_func(ps1_mc_data_interface_start_dma)(uint32_t page) {
    ps1_dirty_lockout_renew();
    /* the spinlock will be unlocked by the DMA irq once all data is tx'd */
    ps1_dirty_lock();
    dma_in_progress = true;
    psram_read_dma(page * PS1_PAGE_SIZE, card, PS1_PAGE_SIZE, ps1_mc_data_interface_rx_done);
}
#endif



void __time_critical_func(ps1_mc_data_interface_setup_read_page)(uint32_t page) {
#if WITH_PSRAM
        ps1_mc_data_interface_start_dma(page);
#endif
}

uint8_t* __time_critical_func(ps1_mc_data_interface_get_page)(uint32_t page) {
    uint8_t* ret = NULL;

#ifdef WITH_PSRAM
    ret = card;
#else
    ret = &card[page*PS1_PAGE_SIZE];
#endif

    return ret;
}

void __time_critical_func(ps1_mc_data_interface_write_byte)(uint32_t address, uint8_t byte) {
#if WITH_PSRAM
    card[address%PS1_PAGE_SIZE] = byte;
    ps1_dirty_lockout_renew();
    ps1_dirty_lock();
    psram_write_dma(address, &card[address%PS1_PAGE_SIZE], 1, NULL);
    psram_wait_for_dma();

    ps1_dirty_unlock();
#else
    card[address] = byte;
#endif
    write_occured = true;
}

void __time_critical_func(ps1_mc_data_interface_write_mc)(uint32_t page) {
    ps1_dirty_mark(page);
}

void __time_critical_func(ps1_mc_data_interface_wait_for_byte)(uint32_t offset) {
#if WITH_PSRAM
    while (dma_in_progress && psram_read_dma_remaining() >= (PS1_PAGE_SIZE - offset)) {};
#endif
}

// Core 0

void ps1_mc_data_interface_card_changed(void) {
#if WITH_PSRAM != 1
    QPRINTF("Card changed\n");

    for (int i = 0; i < 1024; i++) {
        if (ps1_cardman_read_sector(i, &card[i*PS1_PAGE_SIZE]) < 0)
            fatal("Sector %i not read!!!\n", i);
    }
#endif
}

bool ps1_mc_data_interface_write_occured(void) {
    return write_occured;
}

void ps1_mc_data_interface_task(void) {
    write_occured = false;

    ps1_dirty_task();
}

void ps1_mc_data_interface_flush(void) {
    while ( ps1_dirty_activity > 0
    ) {
        ps1_mc_data_interface_task();
    }
}
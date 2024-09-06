#pragma once

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#define PS2_PAGE_SIZE   512


typedef struct {
    size_t page;
    enum {
        PAGE_EMPTY = 0,
        PAGE_READ_REQ = 1,
        PAGE_READ_AHEAD_REQ = 2,
        PAGE_WRITE_REQ = 3,
        PAGE_ERASE_REQ = 4,
        PAGE_DATA_AVAILABLE = 5,
        PAGE_READ_AHEAD_AVAILABLE = 6,
    } page_state;
    uint8_t* data;
} ps2_mcdi_page_t;


// Core 1

void ps2_mc_data_interface_setup_read_page(uint32_t page, bool readahead, bool wait);
void ps2_mc_data_interface_write_mc(uint32_t page, void *buf);
void ps2_mc_data_interface_erase(uint32_t page);
ps2_mcdi_page_t* ps2_mc_data_interface_get_page(uint32_t page);
void ps2_mc_data_interface_commit_write(uint32_t page, uint8_t *buf);
bool ps2_mc_data_interface_write_busy(void);
void ps2_mc_data_interface_invalidate_readahead(void);
void ps2_mc_data_interface_invalidate_read(uint32_t page);
void ps2_mc_data_interface_wait_for_byte(uint32_t offset);

// Core 0

void ps2_mc_data_interface_card_changed(void);
void ps2_mc_data_interface_read_core0(uint32_t page, void* buff512);
bool ps2_mc_data_interface_write_occured(void);
bool ps2_mc_data_interface_busy(void);
void ps2_mc_data_interface_set_sdmode(bool mode);
bool ps2_mc_data_interface_get_sdmode(void);
void ps2_mc_data_interface_task(void);

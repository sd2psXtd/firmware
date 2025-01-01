#pragma once

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#define PS1_PAGE_SIZE   128

// Core 1

void ps1_mc_data_interface_setup_read_page(uint32_t page, bool readahead);
void ps1_mc_data_interface_write_byte(uint32_t address, uint8_t byte);
void ps1_mc_data_interface_write_mc(uint32_t page);
void ps1_mc_data_interface_erase(uint32_t page);
uint8_t* ps1_mc_data_interface_get_page(uint32_t page);
void ps1_mc_data_interface_commit_write(uint32_t page, uint8_t *buf);
void ps1_mc_data_interface_wait_for_byte(uint32_t offset);

// Core 0

void ps1_mc_data_interface_card_changed(void);
bool ps1_mc_data_interface_write_occured(void);
void ps1_mc_data_interface_task(void);

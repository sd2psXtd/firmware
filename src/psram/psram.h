#pragma once

#include <inttypes.h>
#include <stddef.h>

void psram_init(void);
void psram_read(uint32_t addr, void *buf, size_t sz);
void psram_write(uint32_t addr, void *buf, size_t sz);
void psram_read_dma(uint32_t addr, void *buf, size_t sz, void (*cb)(void));
void psram_write_dma(uint32_t addr, void *buf, size_t sz, void (*cb)(void));
uint32_t psram_write_dma_remaining();
uint32_t psram_read_dma_remaining();
void psram_wait_for_dma();

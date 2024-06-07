#pragma once

#include <inttypes.h>
#include <stddef.h>

void psram_init(void);
void psram_read(uint32_t addr, void *buf, size_t sz);
void psram_write(uint32_t addr, void *buf, size_t sz);
// buffer is assumed to have 4 bytes of leading padding for wait cycles
void psram_read_dma_ps2(uint32_t addr, void *buf, size_t sz);

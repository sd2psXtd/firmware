/**
 * Copyright (c) 2020 Raspberry Pi (Trading) Ltd.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */
#ifndef _PIO_SPI_H
#define _PIO_SPI_H

#include "hardware/pio.h"
#include "qspi.pio.h"

extern int PIO_SPI_DMA_TX_CMD_CHAN;
extern int PIO_SPI_DMA_RX_CMD_CHAN;
extern int PIO_SPI_DMA_TX_DATA_CHAN;
extern int PIO_SPI_DMA_RX_DATA_CHAN;

typedef struct pio_spi_inst {
    PIO pio;
    uint sm;
    uint cs_pin;
} pio_spi_inst_t;

void pio_spi_write8_blocking(const pio_spi_inst_t *spi, const uint8_t *src, size_t len);

void pio_spi_read8_blocking(const pio_spi_inst_t *spi, uint8_t *dst, size_t len);

void pio_spi_write8_read8_blocking(const pio_spi_inst_t *spi, uint8_t *src, size_t srclen, uint8_t *dst, size_t dstlen);

void pio_qspi_write8_read8_blocking(const pio_spi_inst_t *spi, uint8_t *cmd, uint8_t *src, size_t srclen, uint8_t *dst, size_t dstlen);

void pio_qspi_write8_dma(const pio_spi_inst_t *spi, uint32_t addr, uint8_t *dst, size_t dstlen, void (*cb)(void));

void pio_qspi_read8_dma(const pio_spi_inst_t *spi, uint32_t addr, uint8_t *src, size_t srclen, void (*cb)(void));

void pio_qspi_dma_init(const pio_spi_inst_t *spi);

#define pio_qspi_write8_blocking(spi, addr, buf, buf_len) do { \
    uint8_t cmd_write[4] = { 0x38, (addr & 0xFF0000) >> 16, (addr & 0xFF00) >> 8, (addr & 0xFF) }; \
    pio_qspi_write8_read8_blocking(spi, cmd_write, buf, buf_len, NULL, 0); \
} while (0);

#define pio_qspi_read8_blocking(spi, addr, buf, buf_len) do { \
    uint8_t cmd_read[4] = { 0xEB, (addr & 0xFF0000) >> 16, (addr & 0xFF00) >> 8, (addr & 0xFF) }; \
    pio_qspi_write8_read8_blocking(spi, cmd_read, NULL, 0, buf, buf_len); \
} while (0);

bool pio_qspi_dma_active();

#endif

/**
 * Copyright (c) 2020 Raspberry Pi (Trading) Ltd.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */
#include <stdio.h>

#include "pico/time.h"
#include "pio_qspi.h"
#include "config.h"
#include "hardware/dma.h"

#define QSPI_DAT_MASK ((1 << (PSRAM_DAT+0)) | (1 << (PSRAM_DAT+1)) | (1 << (PSRAM_DAT+2)) | (1 << (PSRAM_DAT+3)))
#define WAIT_CYCLES (4)

void __time_critical_func(pio_spi_write8_read8_blocking)(const pio_spi_inst_t *spi, uint8_t *src, size_t srclen, uint8_t *dst,
                                                         size_t dstlen) {
    io_rw_8 *txfifo = (io_rw_8 *) &spi->pio->txf[spi->sm];
    io_rw_8 *rxfifo = (io_rw_8 *) &spi->pio->rxf[spi->sm];

    while (srclen) {
        if (!pio_sm_is_tx_fifo_full(spi->pio, spi->sm)) {
            *txfifo = *src++;
            (void) *rxfifo;
            --srclen;
        }
    }

    while (dstlen) {
        if (!pio_sm_is_rx_fifo_empty(spi->pio, spi->sm)) {
            *txfifo = 0;
            *dst++ = *rxfifo;
            --dstlen;
        }
    }
}

void __time_critical_func(pio_qspi_write8_read8_blocking)(const pio_spi_inst_t *spi, uint8_t *cmd,
                                                          uint8_t *src, size_t srclen,
                                                          uint8_t *dst, size_t dstlen) {
    io_rw_8 *txfifo = (io_rw_8 *) &spi->pio->txf[spi->sm];
    io_rw_8 *rxfifo = (io_rw_8 *) &spi->pio->rxf[spi->sm];

    // TODO: this should be done nicer. while it's safe (since we drive the clock), it can be done much faster
    pio_sm_set_pindirs_with_mask(spi->pio, spi->sm, QSPI_DAT_MASK, QSPI_DAT_MASK);

    for (int i = 0; i < 4;) {
        if (!pio_sm_is_tx_fifo_full(spi->pio, spi->sm)) {
            *txfifo = *cmd++;
            (void) *rxfifo;
            i++;
        }
    }

    while (srclen) {
        if (!pio_sm_is_tx_fifo_full(spi->pio, spi->sm)) {
            *txfifo = *src++;
            (void) *rxfifo;
            --srclen;
        }
    }

    // TODO: this should be done nicer. while it's safe (since we drive the clock), it can be done much faster
    pio_sm_set_pindirs_with_mask(spi->pio, spi->sm, 0, QSPI_DAT_MASK);

    int i = 0;
    while (dstlen) {
        if (!pio_sm_is_rx_fifo_empty(spi->pio, spi->sm)) {
            if (i < 4)  {
                // wait cycles
                *txfifo = 0;
                (void) *rxfifo;
                ++i;
            } else {
                *txfifo = 0;
                *dst++ = *rxfifo;
                --dstlen;
            }
        }
    }
}

static dma_channel_config dma_tx_data_conf, dma_rx_data_conf,
                          dma_tx_cmd_conf, dma_rx_cmd_conf;
static volatile bool dma_active = false;

static void (*dma_done_cb)(void);

void __time_critical_func(pio_qspi_write8_dma)(const pio_spi_inst_t *spi, uint32_t addr, uint8_t *src, size_t srclen, void (*cb)(void)) {
    io_rw_8 *txfifo = (io_rw_8 *) &spi->pio->txf[spi->sm];
    io_rw_8 *rxfifo = (io_rw_8 *) &spi->pio->rxf[spi->sm];

    if (dma_channel_is_busy(PIO_SPI_DMA_RX_DATA_CHAN)) printf("WARNING!!!DMA ALREADY ACTIVE!!!!!!!!!\n");
    while (dma_active) {tight_loop_contents();};
    dma_active = true;
    dma_done_cb = cb;

    pio_sm_set_pindirs_with_mask(spi->pio, spi->sm, QSPI_DAT_MASK, QSPI_DAT_MASK);

    static uint8_t cmd_write[4] = { 0x38 };
    cmd_write[1] = (addr & 0xFF0000) >> 16;
    cmd_write[2] = (addr & 0xFF00) >> 8;
    cmd_write[3] = (addr & 0xFF);

    dma_claim_mask(1 << PIO_SPI_DMA_TX_DATA_CHAN | 1 << PIO_SPI_DMA_TX_CMD_CHAN | 1 << PIO_SPI_DMA_RX_DATA_CHAN | 1 << PIO_SPI_DMA_RX_CMD_CHAN );

    static uint8_t zero = 0;
    channel_config_set_write_increment(&dma_tx_data_conf, false);
    channel_config_set_read_increment(&dma_tx_data_conf, true);
    dma_channel_configure(PIO_SPI_DMA_TX_DATA_CHAN, &dma_tx_data_conf, txfifo, src, srclen, false);

    channel_config_set_write_increment(&dma_tx_cmd_conf, false);
    channel_config_set_read_increment(&dma_tx_cmd_conf, true);
    channel_config_set_chain_to(&dma_tx_cmd_conf, PIO_SPI_DMA_TX_DATA_CHAN);
    dma_channel_configure(PIO_SPI_DMA_TX_CMD_CHAN, &dma_tx_cmd_conf, txfifo, cmd_write, sizeof(cmd_write), false);

    channel_config_set_write_increment(&dma_rx_data_conf, false);
    channel_config_set_read_increment(&dma_rx_data_conf, false);
    dma_channel_configure(PIO_SPI_DMA_RX_DATA_CHAN, &dma_rx_data_conf, &zero, rxfifo, sizeof(cmd_write) + srclen, false);

    dma_start_channel_mask(1 << PIO_SPI_DMA_TX_CMD_CHAN | 1 << PIO_SPI_DMA_RX_DATA_CHAN);
}

void __time_critical_func(pio_qspi_read8_dma)(const pio_spi_inst_t *spi, uint32_t addr, uint8_t *dst, size_t dstlen, void (*cb)(void)) {
    io_rw_8 *txfifo = (io_rw_8 *) &spi->pio->txf[spi->sm];
    io_rw_8 *rxfifo = (io_rw_8 *) &spi->pio->rxf[spi->sm];

    if (dma_active) printf("WARNING!!!DMA ALREADY ACTIVE!!!!!!!!!\n");
    while (dma_active) {tight_loop_contents();};
    dma_active = true;
    dma_done_cb = cb;

    pio_sm_set_pindirs_with_mask(spi->pio, spi->sm, QSPI_DAT_MASK, QSPI_DAT_MASK);

    uint8_t cmd_read[4] = { 0xEB, (addr & 0xFF0000) >> 16, (addr & 0xFF00) >> 8, (addr & 0xFF) };
    for (int i = 0; i < 4;) {
        if (!pio_sm_is_tx_fifo_full(spi->pio, spi->sm)) {
            *txfifo = cmd_read[i];
            (void) *rxfifo;
            i++;
        }
    }

    pio_sm_set_pindirs_with_mask(spi->pio, spi->sm, 0, QSPI_DAT_MASK);

    dma_claim_mask(1 << PIO_SPI_DMA_TX_DATA_CHAN | 1 << PIO_SPI_DMA_TX_CMD_CHAN | 1 << PIO_SPI_DMA_RX_DATA_CHAN | 1 << PIO_SPI_DMA_RX_CMD_CHAN );

    static uint8_t zero = 0;
    channel_config_set_write_increment(&dma_tx_data_conf, false);
    channel_config_set_read_increment(&dma_tx_data_conf, false);
    channel_config_set_write_increment(&dma_rx_data_conf, true);
    channel_config_set_read_increment(&dma_rx_data_conf, false);
    dma_channel_configure(PIO_SPI_DMA_TX_DATA_CHAN, &dma_tx_data_conf, txfifo, &zero, dstlen, false);
    dma_channel_configure(PIO_SPI_DMA_RX_DATA_CHAN, &dma_rx_data_conf, dst, rxfifo, dstlen, false);

    channel_config_set_write_increment(&dma_tx_cmd_conf, false);
    channel_config_set_read_increment(&dma_tx_cmd_conf, false);
    channel_config_set_write_increment(&dma_rx_cmd_conf, false);
    channel_config_set_read_increment(&dma_rx_cmd_conf, false);
    channel_config_set_chain_to(&dma_tx_cmd_conf, PIO_SPI_DMA_TX_DATA_CHAN);
    channel_config_set_chain_to(&dma_rx_cmd_conf, PIO_SPI_DMA_RX_DATA_CHAN);
    dma_channel_configure(PIO_SPI_DMA_TX_CMD_CHAN, &dma_tx_cmd_conf, txfifo, &zero, WAIT_CYCLES, true);
    dma_channel_configure(PIO_SPI_DMA_RX_CMD_CHAN, &dma_rx_cmd_conf, &zero, rxfifo, WAIT_CYCLES, true);
}

static void __time_critical_func(dma_rx_done)(void) {
    /* note that this irq is called by core0 despite most dma tx started by core1 */
    if (dma_channel_get_irq0_status(PIO_SPI_DMA_RX_DATA_CHAN)) {
        dma_channel_acknowledge_irq0(PIO_SPI_DMA_RX_DATA_CHAN);
        gpio_put(PSRAM_CS, 1);
        dma_unclaim_mask(1 << PIO_SPI_DMA_TX_DATA_CHAN | 1 << PIO_SPI_DMA_TX_CMD_CHAN | 1 << PIO_SPI_DMA_RX_DATA_CHAN | 1 << PIO_SPI_DMA_RX_CMD_CHAN );

        dma_active = false;
        if (dma_done_cb)
            dma_done_cb();
    }
}

bool __time_critical_func(pio_qspi_dma_active)() {
    return dma_active;
}

void pio_qspi_dma_init(const pio_spi_inst_t *spi) {
    dma_tx_cmd_conf = dma_channel_get_default_config(PIO_SPI_DMA_TX_CMD_CHAN);
    channel_config_set_transfer_data_size(&dma_tx_cmd_conf, DMA_SIZE_8);
    channel_config_set_dreq(&dma_tx_cmd_conf, pio_get_dreq(spi->pio, spi->sm, true));

    dma_rx_cmd_conf = dma_channel_get_default_config(PIO_SPI_DMA_RX_CMD_CHAN);
    channel_config_set_transfer_data_size(&dma_rx_cmd_conf, DMA_SIZE_8);
    channel_config_set_dreq(&dma_rx_cmd_conf, pio_get_dreq(spi->pio, spi->sm, false));

    dma_tx_data_conf = dma_channel_get_default_config(PIO_SPI_DMA_TX_DATA_CHAN);
    channel_config_set_transfer_data_size(&dma_tx_data_conf, DMA_SIZE_8);
    channel_config_set_dreq(&dma_tx_data_conf, pio_get_dreq(spi->pio, spi->sm, true));

    dma_rx_data_conf = dma_channel_get_default_config(PIO_SPI_DMA_RX_DATA_CHAN);
    channel_config_set_transfer_data_size(&dma_rx_data_conf, DMA_SIZE_8);
    channel_config_set_dreq(&dma_rx_data_conf, pio_get_dreq(spi->pio, spi->sm, false));
    dma_channel_set_irq0_enabled(PIO_SPI_DMA_RX_DATA_CHAN, true);
    irq_set_exclusive_handler(DMA_IRQ_0, dma_rx_done);
    irq_set_enabled(DMA_IRQ_0, true);
}

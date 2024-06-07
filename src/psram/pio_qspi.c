/**
 * Copyright (c) 2020 Raspberry Pi (Trading) Ltd.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "pio_qspi.h"
#include "config.h"
#include "ps2_dirty.h"
#include "hardware/dma.h"

#define QSPI_DAT_MASK ((1 << (PSRAM_DAT+0)) | (1 << (PSRAM_DAT+1)) | (1 << (PSRAM_DAT+2)) | (1 << (PSRAM_DAT+3)))

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

static dma_channel_config dma_rx_conf, dma_tx_conf;

void __time_critical_func(pio_qspi_write8_read8_dma)(const pio_spi_inst_t *spi, uint8_t *src, size_t srclen, uint8_t *dst,
                                                     size_t dstlen) {
    io_rw_8 *txfifo = (io_rw_8 *) &spi->pio->txf[spi->sm];
    io_rw_8 *rxfifo = (io_rw_8 *) &spi->pio->rxf[spi->sm];

    // TODO: this should be done nicer. while it's safe (since we drive the clock), it can be done much faster
    pio_sm_set_pindirs_with_mask(spi->pio, spi->sm, QSPI_DAT_MASK, QSPI_DAT_MASK);
    while (srclen) {
        if (!pio_sm_is_tx_fifo_full(spi->pio, spi->sm)) {
            *txfifo = *src++;
            (void) *rxfifo;
            --srclen;
        }
    }
    // TODO: this should be done nicer. while it's safe (since we drive the clock), it can be done much faster
    pio_sm_set_pindirs_with_mask(spi->pio, spi->sm, 0, QSPI_DAT_MASK);

    static uint8_t zero = 0;
    dma_channel_configure(PIO_SPI_DMA_RX_CHAN, &dma_rx_conf, dst, &spi->pio->rxf[spi->sm], dstlen, true);
    /* just poke zeroes into the PIO tx so that it runs the bus */
    dma_channel_configure(PIO_SPI_DMA_TX_CHAN, &dma_tx_conf, &spi->pio->txf[spi->sm], &zero, dstlen, true);
}

static void __time_critical_func(dma_rx_done)(void) {
    /* note that this irq is called by core0 despite most dma tx started by core1 */
    dma_channel_acknowledge_irq0(PIO_SPI_DMA_RX_CHAN);
    gpio_put(PSRAM_CS, 1);
    ps2_dirty_unlock();
}

void pio_qspi_dma_init(const pio_spi_inst_t *spi) {
    dma_rx_conf = dma_channel_get_default_config(PIO_SPI_DMA_RX_CHAN);
    channel_config_set_transfer_data_size(&dma_rx_conf, DMA_SIZE_8);
    channel_config_set_read_increment(&dma_rx_conf, false);
    channel_config_set_write_increment(&dma_rx_conf, true);
    channel_config_set_dreq(&dma_rx_conf, pio_get_dreq(spi->pio, spi->sm, false));

    dma_channel_set_irq0_enabled(PIO_SPI_DMA_RX_CHAN, true);
    irq_set_exclusive_handler(DMA_IRQ_0, dma_rx_done);
    irq_set_enabled(DMA_IRQ_0, true);

    dma_tx_conf = dma_channel_get_default_config(PIO_SPI_DMA_TX_CHAN);
    channel_config_set_transfer_data_size(&dma_tx_conf, DMA_SIZE_8);
    channel_config_set_read_increment(&dma_tx_conf, false);
    channel_config_set_write_increment(&dma_tx_conf, false);
    channel_config_set_dreq(&dma_tx_conf, pio_get_dreq(spi->pio, spi->sm, true));
}

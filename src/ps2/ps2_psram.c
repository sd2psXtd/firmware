#include "config.h"
#include "ps2_psram.h"

#include "ps2_pio_qspi.h"
#include "hardware/timer.h"

#include <stdio.h>
#include <string.h>

#include "debug.h"

static pio_spi_inst_t spi = {
    .pio = pio1,
    .sm = 0,
    .cs_pin = PSRAM_CS
};

#define SPI_OP(stmt) \
    do { \
        gpio_put(spi.cs_pin, 0); \
        stmt; \
        gpio_put(spi.cs_pin, 1); \
    } while (0);

#define TEST_CYCLES 30
#define TEST_BLOCK_SIZE 1024

typedef void (*test_t)(uint8_t *dst);

static void prepare_test_0(uint8_t *dst) {
    for (size_t i = 0; i < TEST_BLOCK_SIZE; ++i)
        dst[i] = (i % 2 == 0) ? 0x55 : 0xAA;
}

static void prepare_test_1(uint8_t *dst) {
    for (size_t i = 0; i < TEST_BLOCK_SIZE; ++i)
        dst[i] = (i % 2 == 0) ? 0xFF : 0x00;
}

static void prepare_test_2(uint8_t *dst) {
    for (size_t i = 0; i < TEST_BLOCK_SIZE; ++i)
        dst[i] = i % 256;
}

static void prepare_test_3(uint8_t *dst) {
    uint32_t rng = 1;

    for (size_t i = 0; i < TEST_BLOCK_SIZE; ++i) {
        rng = rng * 1103515245 + 12345;
        dst[i] = rng % 256;
    }
}

static test_t psram_tests[] = {
    prepare_test_0,
    prepare_test_1,
    prepare_test_2,
    prepare_test_3,
};

#define NUM_TESTS (sizeof(psram_tests)/sizeof(*psram_tests))

static void psram_run_tests(void) {
    uint8_t cmd_write[4 + TEST_BLOCK_SIZE] = { 0x38 };
    uint8_t cmd_read[4] = { 0xEB };
    uint8_t buf[4 + TEST_BLOCK_SIZE] = { 0 };

    uint64_t start = time_us_64();

    for (size_t test = 0; test < NUM_TESTS; ++test) {
        printf("Start PSRAM test %d\n", test);
        psram_tests[test](cmd_write+4);

        uint32_t addr = 0;
        for (size_t i = 0; i < TEST_CYCLES; ++i) {
            memset(buf, 0, sizeof(buf));

            cmd_write[1] = cmd_read[1] = (addr & 0xFF0000) >> 16;
            cmd_write[2] = cmd_read[2] = (addr & 0xFF00) >> 8;
            cmd_write[3] = cmd_read[3] = (addr & 0xFF);

            SPI_OP(pio_qspi_write8_read8_blocking(&spi, cmd_write, sizeof(cmd_write), NULL, 0));
            SPI_OP(pio_qspi_write8_read8_blocking(&spi, cmd_read, sizeof(cmd_read), buf, sizeof(buf)));

            if (memcmp(cmd_write+4, buf+4, TEST_BLOCK_SIZE) != 0) {
                printf("test %d cycle %d\n", test, i);
                fatal("PSRAM failed test");
            }

            addr += TEST_BLOCK_SIZE;
        }
    }

    uint64_t end = time_us_64();
    printf("PSRAM passed all tests -- took %.2f ms -- avg speed %.2f kB/s\n",
        (end - start) / 1000.0,
        1000000.0 * (NUM_TESTS * TEST_CYCLES * TEST_BLOCK_SIZE * 2) / (end - start) / 1024);
}

void psram_read(uint32_t addr, void *vbuf, size_t sz) {
    uint8_t *buf = vbuf;
    uint8_t cmd_read[4] = { 0xEB, (addr & 0xFF0000) >> 16, (addr & 0xFF00) >> 8, (addr & 0xFF) };
    uint8_t tmpbuf[4 + 512];
    SPI_OP(pio_qspi_write8_read8_blocking(&spi, cmd_read, sizeof(cmd_read), tmpbuf, sizeof(tmpbuf)));
    memcpy(buf, tmpbuf+4, sz);
}

void __time_critical_func(psram_read_dma)(uint32_t addr, void *vbuf, size_t sz) {
    uint8_t *buf = vbuf;
    uint8_t cmd_read[4] = { 0xEB, (addr & 0xFF0000) >> 16, (addr & 0xFF00) >> 8, (addr & 0xFF) };
    gpio_put(spi.cs_pin, 0);
    pio_qspi_write8_read8_dma(&spi, cmd_read, sizeof(cmd_read), buf, sz);
}

void __time_critical_func(psram_write)(uint32_t addr, void *vbuf, size_t sz) {
    uint8_t *buf = vbuf;
    uint8_t cmd_write[4 + 512];
    cmd_write[0] = 0x38;
    cmd_write[1] = (addr & 0xFF0000) >> 16;
    cmd_write[2] = (addr & 0xFF00) >> 8;
    cmd_write[3] = (addr & 0xFF);
    memcpy(cmd_write + 4, buf, sz);
    SPI_OP(pio_qspi_write8_read8_blocking(&spi, cmd_write, 4 + sz, NULL, 0));
}

void psram_init(void) {
    uint32_t offset;

    gpio_init(spi.cs_pin);
    gpio_put(spi.cs_pin, 1);
    gpio_set_dir(spi.cs_pin, GPIO_OUT);

    /* start in SPI mode */
    offset = pio_add_program(spi.pio, &spi_cpha0_program);
    pio_spi_init(spi.pio, spi.sm, offset, 8, PSRAM_CLKDIV, 0, 0, PSRAM_CLK, PSRAM_DAT, PSRAM_DAT+1);

    /* make sure PSRAM chip is marked as Known Good Die as per datasheet */
    uint8_t read_id[] = { 0x9F, 0x00, 0x00, 0x00 };
    uint8_t read_id_rsp[32] = { 0 };
    SPI_OP(pio_spi_write8_read8_blocking(&spi, read_id, sizeof(read_id), read_id_rsp, sizeof(read_id_rsp)));

    if (read_id_rsp[3] != 0x0D || read_id_rsp[4] != 0x5D) {
        fatal("PSRAM ID is not Known Good Die\nexpected 0D 5D got %02X %02X\n\ntry to power-cycle it if PSRAM\nis stuck in the QPI mode",
            read_id_rsp[3], read_id_rsp[4]);
    }

    /* switch from SPI to Quad-SPI mode */
    uint8_t enter_qspi[] = { 0x35 };
    SPI_OP(pio_spi_write8_read8_blocking(&spi, enter_qspi, sizeof(enter_qspi), NULL, 0));

    pio_remove_program(spi.pio, &spi_cpha0_program, offset);
    offset = pio_add_program(spi.pio, &qspi_cpha0_program);
    pio_qspi_init(spi.pio, spi.sm, offset, 8, PSRAM_CLKDIV, 0, 0, PSRAM_CLK, PSRAM_DAT);
    pio_qspi_dma_init(&spi);

    /* validate PSRAM is working properly */
    psram_run_tests();

    /* and erase everything to 0xFF */
    uint8_t erasebuf[512];
    memset(erasebuf, 0xFF, sizeof(erasebuf));
    for (int i = 0; i < 8 * 1024 * 1024; i += 512) {
        psram_write(i, erasebuf, sizeof(erasebuf));
    }
}

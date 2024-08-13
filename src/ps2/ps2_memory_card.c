#include "hardware/gpio.h"
#include "hardware/regs/addressmap.h"
#include "hardware/timer.h"
#include "hardware/flash.h"
#include "hardware/dma.h"
#include "pico/platform.h"

#include "config.h"
#include "ps2_mc_spi.pio.h"
#include "flashmap.h"
#include "debug.h"
#include "keystore.h"
#include "des.h"

#include "ps2_dirty.h"
#include "ps2_psram.h"
#include "ps2_pio_qspi.h"
#include "ps2_cardman.h"
#include "ps2_exploit.h"

#include <stdbool.h>
#include <string.h>


#define DEBUG_MC_PROTOCOL

uint64_t us_startup;

int byte_count;
volatile int reset;
int ignore;
uint8_t flag;
bool flash_mode = false;

typedef struct {
    uint32_t offset;
    uint32_t sm;
} pio_t;

pio_t cmd_reader, dat_writer, dat_writer_slow, clock_probe;

#define ERASE_SECTORS 16
#define CARD_SIZE (8 * 1024 * 1024)

uint8_t term = 0xFF;
uint32_t read_sector, write_sector, erase_sector;
struct {
    uint32_t prefix;
    uint8_t buf[528];
} readtmp;
uint8_t *eccptr;
uint8_t writetmp[528];
int is_write, is_dma_read;
uint32_t readptr, writeptr;
static volatile int mc_exit_request, mc_exit_response, mc_enter_request, mc_enter_response;
static uint8_t hostkey[9];

static inline void __time_critical_func(read_mc)(uint32_t addr, void *buf, size_t sz) {
    if (flash_mode) {
        ps2_exploit_read(addr, buf, sz);
        ps2_dirty_unlock();
    } else {
        psram_read_dma(addr, buf, sz);
    }
}

static inline void __time_critical_func(write_mc)(uint32_t addr, void *buf, size_t sz) {
    if (!flash_mode) {
        psram_write(addr, buf, sz);
    } else {
        ps2_dirty_unlock();
    }
}

static inline void __time_critical_func(RAM_pio_sm_drain_tx_fifo)(PIO pio, uint sm) {
    uint instr = (pio->sm[sm].shiftctrl & PIO_SM0_SHIFTCTRL_AUTOPULL_BITS) ? pio_encode_out(pio_null, 32) :
                 pio_encode_pull(false, false);
    while (!pio_sm_is_tx_fifo_empty(pio, sm)) {
        pio_sm_exec(pio, sm, instr);
    }
}

static void __time_critical_func(reset_pio)(void) {
    pio_set_sm_mask_enabled(pio0, (1 << cmd_reader.sm) | (1 << dat_writer.sm) | (1 << dat_writer_slow.sm) | (1 << clock_probe.sm), false);
    pio_restart_sm_mask(pio0, (1 << cmd_reader.sm) | (1 << dat_writer.sm) | (1 << dat_writer_slow.sm) | (1 << clock_probe.sm));

    pio_sm_exec(pio0, cmd_reader.sm, pio_encode_jmp(cmd_reader.offset));
    pio_sm_exec(pio0, dat_writer.sm, pio_encode_jmp(dat_writer.offset));
    pio_sm_exec(pio0, dat_writer_slow.sm, pio_encode_jmp(dat_writer_slow.offset));
    pio_sm_exec(pio0, clock_probe.sm, pio_encode_jmp(clock_probe.offset));

    pio_sm_clear_fifos(pio0, cmd_reader.sm);
    RAM_pio_sm_drain_tx_fifo(pio0, dat_writer.sm);
    RAM_pio_sm_drain_tx_fifo(pio0, dat_writer_slow.sm);
    pio_sm_clear_fifos(pio0, clock_probe.sm);

    pio_enable_sm_mask_in_sync(pio0, (1 << cmd_reader.sm) | (1 << dat_writer.sm) | (1 << dat_writer_slow.sm) | (1 << clock_probe.sm));

    reset = 1;
}

static void __time_critical_func(init_pio)(void) {
    /* Set all pins as floating inputs */
    gpio_set_dir(PIN_PSX_ACK, 0);
    gpio_set_dir(PIN_PSX_SEL, 0);
    gpio_set_dir(PIN_PSX_CLK, 0);
    gpio_set_dir(PIN_PSX_CMD, 0);
    gpio_set_dir(PIN_PSX_DAT, 0);
    gpio_disable_pulls(PIN_PSX_ACK);
    gpio_disable_pulls(PIN_PSX_SEL);
    gpio_disable_pulls(PIN_PSX_CLK);
    gpio_disable_pulls(PIN_PSX_CMD);
    gpio_disable_pulls(PIN_PSX_DAT);

    cmd_reader.offset = pio_add_program(pio0, &cmd_reader_program);
    cmd_reader.sm = pio_claim_unused_sm(pio0, true);

    dat_writer.offset = pio_add_program(pio0, &dat_writer_program);
    dat_writer.sm = pio_claim_unused_sm(pio0, true);

    dat_writer_slow.offset = pio_add_program(pio0, &dat_writer_slow_program);
    dat_writer_slow.sm = pio_claim_unused_sm(pio0, true);

    clock_probe.offset = pio_add_program(pio0, &clock_probe_program);
    clock_probe.sm = pio_claim_unused_sm(pio0, true);

    cmd_reader_program_init(pio0, cmd_reader.sm, cmd_reader.offset);
    dat_writer_program_init(pio0, dat_writer.sm, dat_writer.offset);
    dat_writer_slow_program_init(pio0, dat_writer_slow.sm, dat_writer_slow.offset);
    clock_probe_program_init(pio0, clock_probe.sm, clock_probe.offset);
}

static void __time_critical_func(card_deselected)(uint gpio, uint32_t event_mask) {
    if (gpio == PIN_PSX_SEL && (event_mask & GPIO_IRQ_EDGE_RISE)) {
        reset_pio();
    }
}

#define recv() do { \
    while ( \
        pio_sm_is_rx_fifo_empty(pio0, cmd_reader.sm) && \
        pio_sm_is_rx_fifo_empty(pio0, cmd_reader.sm) && \
        pio_sm_is_rx_fifo_empty(pio0, cmd_reader.sm) && \
        pio_sm_is_rx_fifo_empty(pio0, cmd_reader.sm) && \
        pio_sm_is_rx_fifo_empty(pio0, cmd_reader.sm) && \
    1) { \
        if (reset) \
            goto NEXTCMD; \
    } \
    cmd = (uint8_t) (pio_sm_get(pio0, cmd_reader.sm) >> 24); \
} while (0);

#define recvfirst() do { \
    while ( \
        pio_sm_is_rx_fifo_empty(pio0, cmd_reader.sm) && \
        pio_sm_is_rx_fifo_empty(pio0, cmd_reader.sm) && \
        pio_sm_is_rx_fifo_empty(pio0, cmd_reader.sm) && \
        pio_sm_is_rx_fifo_empty(pio0, cmd_reader.sm) && \
        pio_sm_is_rx_fifo_empty(pio0, cmd_reader.sm) && \
    1) { \
        if (reset) \
            goto NEXTCMD; \
        if (mc_exit_request) \
            goto EXIT_REQUEST; \
    } \
    cmd = (uint8_t) (pio_sm_get(pio0, cmd_reader.sm) >> 24); \
} while (0);

static inline uint32_t __time_critical_func(probe_clock)(void) {
    return pio_sm_get_blocking(pio0, clock_probe.sm);
}

static inline void __time_critical_func(mc_respond_fast)(uint8_t ch) {
    pio_sm_put_blocking(pio0, dat_writer.sm, ch);
}

static inline void __time_critical_func(mc_respond_slow)(uint8_t ch) {
    pio_sm_put_blocking(pio0, dat_writer_slow.sm, ch);
}

static uint8_t EccTable[] = {
	0x00, 0x87, 0x96, 0x11, 0xa5, 0x22, 0x33, 0xb4,0xb4, 0x33, 0x22, 0xa5, 0x11, 0x96, 0x87, 0x00,
	0xc3, 0x44, 0x55, 0xd2, 0x66, 0xe1, 0xf0, 0x77,0x77, 0xf0, 0xe1, 0x66, 0xd2, 0x55, 0x44, 0xc3,
	0xd2, 0x55, 0x44, 0xc3, 0x77, 0xf0, 0xe1, 0x66,0x66, 0xe1, 0xf0, 0x77, 0xc3, 0x44, 0x55, 0xd2,
	0x11, 0x96, 0x87, 0x00, 0xb4, 0x33, 0x22, 0xa5,0xa5, 0x22, 0x33, 0xb4, 0x00, 0x87, 0x96, 0x11,
	0xe1, 0x66, 0x77, 0xf0, 0x44, 0xc3, 0xd2, 0x55,0x55, 0xd2, 0xc3, 0x44, 0xf0, 0x77, 0x66, 0xe1,
	0x22, 0xa5, 0xb4, 0x33, 0x87, 0x00, 0x11, 0x96,0x96, 0x11, 0x00, 0x87, 0x33, 0xb4, 0xa5, 0x22,
	0x33, 0xb4, 0xa5, 0x22, 0x96, 0x11, 0x00, 0x87,0x87, 0x00, 0x11, 0x96, 0x22, 0xa5, 0xb4, 0x33,
	0xf0, 0x77, 0x66, 0xe1, 0x55, 0xd2, 0xc3, 0x44,0x44, 0xc3, 0xd2, 0x55, 0xe1, 0x66, 0x77, 0xf0,
	0xf0, 0x77, 0x66, 0xe1, 0x55, 0xd2, 0xc3, 0x44,0x44, 0xc3, 0xd2, 0x55, 0xe1, 0x66, 0x77, 0xf0,
	0x33, 0xb4, 0xa5, 0x22, 0x96, 0x11, 0x00, 0x87,0x87, 0x00, 0x11, 0x96, 0x22, 0xa5, 0xb4, 0x33,
	0x22, 0xa5, 0xb4, 0x33, 0x87, 0x00, 0x11, 0x96,0x96, 0x11, 0x00, 0x87, 0x33, 0xb4, 0xa5, 0x22,
	0xe1, 0x66, 0x77, 0xf0, 0x44, 0xc3, 0xd2, 0x55,0x55, 0xd2, 0xc3, 0x44, 0xf0, 0x77, 0x66, 0xe1,
	0x11, 0x96, 0x87, 0x00, 0xb4, 0x33, 0x22, 0xa5,0xa5, 0x22, 0x33, 0xb4, 0x00, 0x87, 0x96, 0x11,
	0xd2, 0x55, 0x44, 0xc3, 0x77, 0xf0, 0xe1, 0x66,0x66, 0xe1, 0xf0, 0x77, 0xc3, 0x44, 0x55, 0xd2,
	0xc3, 0x44, 0x55, 0xd2, 0x66, 0xe1, 0xf0, 0x77,0x77, 0xf0, 0xe1, 0x66, 0xd2, 0x55, 0x44, 0xc3,
	0x00, 0x87, 0x96, 0x11, 0xa5, 0x22, 0x33, 0xb4,0xb4, 0x33, 0x22, 0xa5, 0x11, 0x96, 0x87, 0x00
};

// keysource and key are self generated values
uint8_t keysource[] = { 0xf5, 0x80, 0x95, 0x3c, 0x4c, 0x84, 0xa9, 0xc0 };
uint8_t dex_key[16] = { 0x17, 0x39, 0xd3, 0xbc, 0xd0, 0x2c, 0x18, 0x07, 0x4b, 0x17, 0xf0, 0xea, 0xc4, 0x66, 0x30, 0xf9 };
uint8_t cex_key[16] = { 0x06, 0x46, 0x7a, 0x6c, 0x5b, 0x9b, 0x82, 0x77, 0x0d, 0xdf, 0xe9, 0x7e, 0x24, 0x5b, 0x9f, 0xca };
uint8_t *key = cex_key;

static uint8_t iv[8];
static uint8_t seed[8];
static uint8_t nonce[8];
static uint8_t MechaChallenge3[8];
static uint8_t MechaChallenge2[8];
static uint8_t MechaChallenge1[8];
static uint8_t CardResponse1[8];
static uint8_t CardResponse2[8];
static uint8_t CardResponse3[8];

static void __time_critical_func(desEncrypt)(void *key, void *data)
{
	DesContext dc;
	desInit(&dc, (uint8_t *) key, 8);
	desEncryptBlock(&dc, (uint8_t *) data, (uint8_t *) data);
}

static void __time_critical_func(desDecrypt)(void *key, void *data)
{
	DesContext dc;
	desInit(&dc, (uint8_t *) key, 8);
	desDecryptBlock(&dc, (uint8_t *) data, (uint8_t *) data);
}

static void __time_critical_func(doubleDesEncrypt)(void *key, void *data)
{
	desEncrypt(key, data);
	desDecrypt(&((uint8_t *) key)[8], data);
	desEncrypt(key, data);
}

static void __time_critical_func(doubleDesDecrypt)(void *key, void *data)
{
	desDecrypt(key, data);
	desEncrypt(&((uint8_t *) key)[8], data);
	desDecrypt(key, data);
}

static void __time_critical_func(xor_bit)(const void* a, const void* b, void* Result, size_t Length) {
	size_t i;
	for (i = 0; i < Length; i++) {
		((uint8_t*)Result)[i] = ((uint8_t*)a)[i] ^ ((uint8_t*)b)[i];
	}
}

static void __time_critical_func(generateIvSeedNonce)() {
	for (int i = 0; i < 8; i++) {
		iv[i] = 0x42;
		seed[i] = keysource[i] ^ iv[i];
		nonce[i] = 0x42;
	}
}

static void __time_critical_func(generateResponse)() {
	doubleDesDecrypt(key, MechaChallenge1);
	uint8_t random[8] = { 0 };
	xor_bit(MechaChallenge1, ps2_civ, random, 8);

	// MechaChallenge2 and MechaChallenge3 let's the card verify the console

	xor_bit(nonce, ps2_civ, CardResponse1, 8);

	doubleDesEncrypt(key, CardResponse1);

	xor_bit(random, CardResponse1, CardResponse2, 8);
	doubleDesEncrypt(key, CardResponse2);

	uint8_t CardKey[] = { 'M', 'e', 'c', 'h', 'a', 'P', 'w', 'n' };
	xor_bit(CardKey, CardResponse2, CardResponse3, 8);
	doubleDesEncrypt(key, CardResponse3);
}

static void __time_critical_func(mc_main_loop)(void) {
    while (1) {
        uint8_t cmd, ch;

NEXTCMD:
        while (!reset && !reset && !reset && !reset && !reset) {
            if (mc_exit_request)
                goto EXIT_REQUEST;
        }
        reset = 0;

        recvfirst();

        if (cmd == 0x81) {
            if (probe_clock()) {
#define send mc_respond_fast
#include "ps2_memory_card.in.c"
#undef send
            } else {
#define send mc_respond_slow
#include "ps2_memory_card.in.c"
#undef send
            }
        } else {
            // not for us
            continue;
        }
    }

EXIT_REQUEST:
    mc_exit_response = 1;
}

static void __no_inline_not_in_flash_func(mc_main)(void) {
    while (1) {
        while (!mc_enter_request)
        {}
        mc_enter_response = 1;

        mc_main_loop();
    }
}

static gpio_irq_callback_t callbacks[NUM_CORES];

static void __time_critical_func(RAM_gpio_acknowledge_irq)(uint gpio, uint32_t events) {
    check_gpio_param(gpio);
    iobank0_hw->intr[gpio / 8] = events << (4 * (gpio % 8));
}

static void __time_critical_func(RAM_gpio_default_irq_handler)(void) {
    uint core = get_core_num();
    gpio_irq_callback_t callback = callbacks[core];
    io_irq_ctrl_hw_t *irq_ctrl_base = core ? &iobank0_hw->proc1_irq_ctrl : &iobank0_hw->proc0_irq_ctrl;
    for (uint gpio = 0; gpio < NUM_BANK0_GPIOS; gpio+=8) {
        uint32_t events8 = irq_ctrl_base->ints[gpio >> 3u];
        // note we assume events8 is 0 for non-existent GPIO
        for(uint i=gpio;events8 && i<gpio+8;i++) {
            uint32_t events = events8 & 0xfu;
            if (events) {
                RAM_gpio_acknowledge_irq(i, events);
                if (callback) {
                    callback(i, events);
                }
            }
            events8 >>= 4;
        }
    }
}

static void my_gpio_set_irq_callback(gpio_irq_callback_t callback) {
    uint core = get_core_num();
    if (callbacks[core]) {
        if (!callback) {
            irq_remove_handler(IO_IRQ_BANK0, RAM_gpio_default_irq_handler);
        }
        callbacks[core] = callback;
    } else if (callback) {
        callbacks[core] = callback;
        irq_add_shared_handler(IO_IRQ_BANK0, RAM_gpio_default_irq_handler, GPIO_IRQ_CALLBACK_ORDER_PRIORITY);
    }
}

static void my_gpio_set_irq_enabled_with_callback(uint gpio, uint32_t events, bool enabled, gpio_irq_callback_t callback) {
    gpio_set_irq_enabled(gpio, events, enabled);
    my_gpio_set_irq_callback(callback);
    if (enabled) irq_set_enabled(IO_IRQ_BANK0, true);
}

void ps2_memory_card_main(void) {
    init_pio();
    generateIvSeedNonce();

    us_startup = time_us_64();
    debug_printf("Secondary core!\n");

    my_gpio_set_irq_enabled_with_callback(PIN_PSX_SEL, GPIO_IRQ_EDGE_RISE, 1, card_deselected);

    gpio_set_slew_rate(PIN_PSX_DAT, GPIO_SLEW_RATE_FAST);
    gpio_set_drive_strength(PIN_PSX_DAT, GPIO_DRIVE_STRENGTH_12MA);
    mc_main();
}

static int memcard_running;

void ps2_memory_card_exit(void) {
    if (!memcard_running)
        return;

    mc_exit_request = 1;
    while (!mc_exit_response)
    {}
    mc_exit_request = mc_exit_response = 0;
    memcard_running = 0;
}

void ps2_memory_card_enter(void) {
    if (flash_mode) {
        ps2_memory_card_exit();
    } else if (memcard_running)
        return;

    mc_enter_request = 1;
    while (!mc_enter_response)
    {}
    mc_enter_request = mc_enter_response = 0;
    memcard_running = 1;
    flash_mode = false;
}

void ps2_memory_card_enter_flash(void) {
    mc_enter_request = 1;
    while (!mc_enter_response)
    {}
    mc_enter_request = mc_enter_response = 0;
    memcard_running = 1;
    flash_mode = true;
}

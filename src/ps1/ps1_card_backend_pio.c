#include "ps1_card_backend_pio.h"
#include "ps1_mc_spi.pio.h"

#include "hardware/pio.h"
#include "hardware/gpio.h"
#include "hardware/irq.h"
#include "hardware/structs/iobank0.h"
#include "pico/multicore.h"


#include <assert.h>

#include "debug.h"

#if LOG_LEVEL_PS1_PIO_BACKEND == 0
    #define log(x...)
#else
    #define log(level, fmt, x...) LOG_PRINT(LOG_LEVEL_PS1_PIO_BACKEND, level, fmt, ##x)
#endif

static void ps1_pio_set_irq_callback(gpio_irq_callback_t callback);

static void ps1_pio_reset(void);
static void ps1_pio_init(void);
static void ps1_pio_deinit(void);
static uint8_t ps1_pio_receive_cntrl(uint8_t *cmd);
static uint8_t ps1_pio_receive_mc(uint8_t *cmd);
static uint8_t ps1_pio_receive_first(uint8_t *cmd);
static void ps1_pio_respond(uint8_t ch);
static void ps1_pio_finish(void);
static void ps1_pio_irq_shutdown(void);
static void ps1_pio_memory_card_startup(void);

ps1_memory_card_backend_t ps1_card_backend_pio = {
    .init = ps1_pio_init,
    .deinit = ps1_pio_deinit,
    .reset = ps1_pio_reset,
    .startup = ps1_pio_memory_card_startup,
    .shutdown = ps1_pio_irq_shutdown,

    .recv_cntrl = ps1_pio_receive_cntrl,
    .recv_mc = ps1_pio_receive_mc,
    .receive_first = ps1_pio_receive_first,
    .respond = ps1_pio_respond,
    .finish = ps1_pio_finish,
};



typedef struct {
    uint32_t offset;
    uint32_t sm;
} pio_t;

static pio_t cmd_reader, dat_writer, cntrl_reader, ack_generator;

static void __time_critical_func(ps1_pio_reset)(void) {
    pio_set_sm_mask_enabled(pio0, (1 << cmd_reader.sm) | (1 << dat_writer.sm) | (1 << cntrl_reader.sm) | (1 << ack_generator.sm), false);
    pio_restart_sm_mask(pio0, (1 << cmd_reader.sm) | (1 << dat_writer.sm) | (1 << cntrl_reader.sm) | (1 << ack_generator.sm));

    pio_sm_exec(pio0, cmd_reader.sm, pio_encode_jmp(cmd_reader.offset));
    pio_sm_exec(pio0, dat_writer.sm, pio_encode_jmp(dat_writer.offset));
    pio_sm_exec(pio0, cntrl_reader.sm, pio_encode_jmp(cntrl_reader.offset));
    pio_sm_exec(pio0, ack_generator.sm,pio_encode_set(pio_pindirs, 0));
    pio_sm_exec(pio0, ack_generator.sm, pio_encode_jmp(ack_generator.offset));

    pio_sm_clear_fifos(pio0, cmd_reader.sm);
    pio_sm_clear_fifos(pio0, cntrl_reader.sm);
    pio_sm_drain_tx_fifo(pio0, dat_writer.sm);
    pio_interrupt_clear(pio0, 4);

    pio_enable_sm_mask_in_sync(pio0, (1 << cmd_reader.sm) | (1 << dat_writer.sm) | (1 << cntrl_reader.sm) | (1 << ack_generator.sm));

    ps1_mc_reset = 1;
}

static void __time_critical_func(ps1_pio_init)(void) {
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
    log(LOG_TRACE, "cmd_reader offset %d, sm %d\n", cmd_reader.offset, cmd_reader.sm);

    dat_writer.offset = pio_add_program(pio0, &dat_writer_program);
    dat_writer.sm = pio_claim_unused_sm(pio0, true);
    log(LOG_TRACE, "dat_writer offset %d, sm %d\n", dat_writer.offset, dat_writer.sm);

    cntrl_reader.offset = pio_add_program(pio0, &cmd_reader_program);
    cntrl_reader.sm = pio_claim_unused_sm(pio0, true);
    log(LOG_TRACE, "cntrl_reader offset %d, sm %d\n", cntrl_reader.offset, cntrl_reader.sm);

    ack_generator.offset = pio_add_program(pio0, &ack_generator_program);
    ack_generator.sm = pio_claim_unused_sm(pio0, true);
    log(LOG_TRACE, "ack_generator offset %d, sm %d\n", ack_generator.offset, ack_generator.sm);

    cmd_reader_program_init(pio0, cmd_reader.sm, cmd_reader.offset);
    dat_writer_program_init(pio0, dat_writer.sm, dat_writer.offset);
    controller_program_init(pio0, cntrl_reader.sm, cntrl_reader.offset);
    ack_generator_program_init(pio0, ack_generator.sm, ack_generator.offset);
}



static void ps1_pio_deinit(void) {
        // Stop the machines before their programs/resources are released.
    const uint32_t sm_mask =
        (1u << cmd_reader.sm) | (1u << dat_writer.sm) | (1u << cntrl_reader.sm) | (1u << ack_generator.sm);

    pio_set_sm_mask_enabled(pio0, sm_mask, false);

    pio_remove_program(pio0, &cmd_reader_program, cmd_reader.offset);
    pio_remove_program(pio0, &cmd_reader_program, cntrl_reader.offset);
    pio_remove_program(pio0, &dat_writer_program, dat_writer.offset);
    pio_remove_program(pio0, &ack_generator_program,
        ack_generator.offset);

    pio_sm_unclaim(pio0, cmd_reader.sm);
    pio_sm_unclaim(pio0, cntrl_reader.sm);
    pio_sm_unclaim(pio0, dat_writer.sm);
    pio_sm_unclaim(pio0, ack_generator.sm);

    log(LOG_TRACE, "Unclaimed %u, %u, %u!\n", cmd_reader.sm, dat_writer.sm, cntrl_reader.sm);
}

static uint8_t ps1_pio_receive_mc(uint8_t *cmd) {
    while (pio_sm_is_rx_fifo_empty(pio0, cmd_reader.sm) && pio_sm_is_rx_fifo_empty(pio0, cmd_reader.sm))  {
        if (ps1_mc_exit_request)
            return PS1_RECEIVE_EXIT;
        if (ps1_mc_reset)
            return PS1_RECEIVE_RESET;
    }
    *cmd = (pio_sm_get(pio0, cmd_reader.sm) >> 24);
    return PS1_RECEIVE_OK;
}

static uint8_t ps1_pio_receive_cntrl(uint8_t *cmd) {
    while (pio_sm_is_rx_fifo_empty(pio0, cntrl_reader.sm) && pio_sm_is_rx_fifo_empty(pio0, cntrl_reader.sm))  {
        if (ps1_mc_exit_request)
            return PS1_RECEIVE_EXIT;
        if (ps1_mc_reset)
            return PS1_RECEIVE_RESET;
    }
    *cmd = (pio_sm_get(pio0, cntrl_reader.sm) >> 24);
    return PS1_RECEIVE_OK;
}

static uint8_t ps1_pio_receive_first(uint8_t *cmd) {
    while (!ps1_mc_reset && !ps1_mc_reset && !ps1_mc_reset && !ps1_mc_reset && !ps1_mc_reset) {
        if (ps1_mc_exit_request)
            return PS1_RECEIVE_EXIT;
    }
    ps1_mc_reset = 0;
    return ps1_pio_receive_mc(cmd);
}

static void ps1_pio_finish(void) {}

static void ps1_pio_respond(uint8_t ch) {
    pio_sm_put_blocking(pio0, dat_writer.sm, ~ch & 0xFF);
}


static void ps1_pio_irq_shutdown(void) {
            // Stop the machines before their programs/resources are released.
    const uint32_t sm_mask =
        (1u << cmd_reader.sm) | (1u << dat_writer.sm) | (1u << cntrl_reader.sm) | (1u << ack_generator.sm);

    pio_set_sm_mask_enabled(pio0, sm_mask, false);

    log(LOG_TRACE, "Disable IRQ\n");
    // Prevent the handler from running again on this core.
    irq_set_enabled(IO_IRQ_BANK0, false);

    log(LOG_TRACE, "IRQ disabled\n");

    // Disable SEL rising events for core 1.
    // The SDK also acknowledges the selected event here.
    gpio_set_irq_enabled(PIN_PSX_SEL, GPIO_IRQ_EDGE_RISE, false);

    log(LOG_TRACE, "SEL rising edge interrupt disabled\n");
    ps1_pio_set_irq_callback(NULL);
}


static gpio_irq_callback_t callbacks[NUM_CORES];


static void __time_critical_func(card_deselected)(uint gpio, uint32_t event_mask) {
    if (gpio == PIN_PSX_SEL && (event_mask & GPIO_IRQ_EDGE_RISE)) {
        ps1_pio_reset();
        ps1_card_active = false;
    } else if (gpio == PIN_PSX_SEL && (event_mask & GPIO_IRQ_EDGE_FALL)) {
        ps1_card_active = true;
    }

}


static void __time_critical_func(RAM_gpio_acknowledge_irq)(uint gpio, uint32_t events) {
    check_gpio_param(gpio);
    iobank0_hw->intr[gpio / 8] = events << (4 * (gpio % 8));
}

static void __time_critical_func(RAM_gpio_default_irq_handler)(void) {
    uint core = get_core_num();
    gpio_irq_callback_t callback = callbacks[core];
    io_bank0_irq_ctrl_hw_t *irq_ctrl_base = core ? &iobank0_hw->proc1_irq_ctrl : &iobank0_hw->proc0_irq_ctrl;
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

static void ps1_pio_set_irq_callback(gpio_irq_callback_t callback) {
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

static void ps1_pio_set_irq_enabled_with_callback(uint gpio, uint32_t events, bool enabled, gpio_irq_callback_t callback) {
    gpio_set_irq_enabled(gpio, events, enabled);
    ps1_pio_set_irq_callback(callback);
    if (enabled) irq_set_enabled(IO_IRQ_BANK0, true);
}


static void ps1_pio_memory_card_startup(void) {
    DPRINTF("Secondary core!\n");

    ps1_pio_set_irq_enabled_with_callback(PIN_PSX_SEL, GPIO_IRQ_EDGE_RISE, 1, card_deselected);
    ps1_pio_set_irq_enabled_with_callback(PIN_PSX_SEL, GPIO_IRQ_EDGE_FALL, 1, card_deselected);

    gpio_set_slew_rate(PIN_PSX_DAT, GPIO_SLEW_RATE_SLOW);
    gpio_set_drive_strength(PIN_PSX_DAT, GPIO_DRIVE_STRENGTH_4MA);

}

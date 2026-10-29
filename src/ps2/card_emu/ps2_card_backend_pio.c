#include "ps2_card_backend_pio.h"

#include "ps2_mc_commands.h"
#include "ps2_mc_spi.pio.h"
#include "mmceman/ps2_mmceman.h"

#include "hardware/pio.h"
#include "hardware/gpio.h"
#include "hardware/irq.h"
#include "hardware/structs/iobank0.h"

#include <assert.h>

#include "debug.h"

#if LOG_LEVEL_PS2_PIO_BACKEND == 0
#define log(x...)
#else
#define log(level, fmt, x...) LOG_PRINT(LOG_LEVEL_PS2_PIO_BACKEND, level, fmt, ##x)
#endif


typedef struct {
    uint32_t offset;
    uint32_t sm;
} pio_t;


static void pio_reset(void);
static void pio_init(void);
static void pio_deinit(void);
static uint8_t pio_receiveFirst(uint8_t *cmd);
static uint8_t pio_receive(uint8_t *cmd);
static void pio_respond(uint8_t ch);
static bool pio_probe(void);
static void pio_irq_startup(void);
static void pio_irq_shutdown(void);
static void pio_memory_card_startup(void);
static void pio_set_irq_callback(gpio_irq_callback_t callback);


static pio_t cmd_reader, dat_writer;
static bool pio_initialized;
static bool pio_irq_registered;
static bool ps2_host_confirmed;
static gpio_irq_callback_t callbacks[NUM_CORES];

ps2_memory_card_backend_t ps2_card_backend_pio = {
    .init = pio_init,
    .deinit = pio_deinit,
    .reset = pio_reset,
    .startup = pio_memory_card_startup,
    .shutdown = pio_irq_shutdown,

    .receive = pio_receive,
    .receiveFirst = pio_receiveFirst,
    .respond = pio_respond,

    .probe = pio_probe,
};

static void __time_critical_func(pio_reset)(void) {

    /* NOTE: If the TX FIFO's not empty and this intr is triggered in the middle
     * of a MMCEMAN FS function, it's a safe bet the PS2 was reset or the SD2PSX was unplugged */
    if (mmceman_callback != NULL && pio_sm_is_tx_fifo_empty(pio0, dat_writer.sm) == false) {
        mmceman_timeout_detected = true;
    }

    const uint32_t sm_mask =
        (1u << cmd_reader.sm) | (1u << dat_writer.sm);

    pio_set_sm_mask_enabled(pio0, sm_mask, false);

    pio_sm_clear_fifos(pio0, cmd_reader.sm);
    pio_sm_clear_fifos(pio0, dat_writer.sm);

    pio_restart_sm_mask(pio0, sm_mask);

    pio_sm_exec(pio0, cmd_reader.sm,
                pio_encode_jmp(cmd_reader.offset));
    pio_sm_exec(pio0, dat_writer.sm,
                pio_encode_jmp(dat_writer.offset));

    pio_enable_sm_mask_in_sync(pio0, sm_mask);

    if (mmceman_tx_queued) {
        pio_respond(mmceman_tx_byte); //Preemptively place byte on tx for proper alignment with mmce fs read packets
        mmceman_tx_queued = false;
        mmceman_tx_byte = 0;
    }

    reset = 1;
}

static void __time_critical_func(pio_init)(void) {
    if (pio_initialized)
        // This should never happen, but if it does, we want to avoid reinitializing the PIO and causing a crash
        return;
    log(LOG_TRACE, "Initializing PIO for PS2 memory card emulation\n");
    ps2_host_confirmed = false;
    mmceman_tx_queued = false;
    mmceman_tx_byte = 0x0;

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

    cmd_reader_program_init(pio0, cmd_reader.sm, cmd_reader.offset);
    dat_writer_program_init(pio0, dat_writer.sm, dat_writer.offset);
    pio_initialized = true;

    log(LOG_TRACE, "PIO initialized for PS2 memory card emulation\n");
}


static uint8_t __time_critical_func(pio_receive)(uint8_t *cmd) {
    do {
        while (pio_sm_is_rx_fifo_empty(pio0, cmd_reader.sm) && pio_sm_is_rx_fifo_empty(pio0, cmd_reader.sm) && pio_sm_is_rx_fifo_empty(pio0, cmd_reader.sm) &&
            pio_sm_is_rx_fifo_empty(pio0, cmd_reader.sm) && pio_sm_is_rx_fifo_empty(pio0, cmd_reader.sm) && 1) {
            if (reset) {
                return RECEIVE_RESET;
            }
        }
        (*cmd) = (pio_sm_get(pio0, cmd_reader.sm) >> 24);
        return RECEIVE_OK;
    }
    while (0);
}

static uint8_t __time_critical_func(pio_receiveFirst)(uint8_t *cmd) {
    do {
        while (pio_sm_is_rx_fifo_empty(pio0, cmd_reader.sm)
                && pio_sm_is_rx_fifo_empty(pio0, cmd_reader.sm)
                && pio_sm_is_rx_fifo_empty(pio0, cmd_reader.sm)
                && pio_sm_is_rx_fifo_empty(pio0, cmd_reader.sm)
                && pio_sm_is_rx_fifo_empty(pio0, cmd_reader.sm)
                && 1) {
            if (reset)
                return RECEIVE_RESET;
            if (mc_exit_request)
                return RECEIVE_EXIT;
        }
        (*cmd) = (pio_sm_get(pio0, cmd_reader.sm) >> 24);
        card_active = true;
        return RECEIVE_OK;
    }
    while (0);
}

static void __time_critical_func(pio_respond)(uint8_t ch) {
    pio_sm_put_blocking(pio0, dat_writer.sm, ch);
}


static bool __time_critical_func(pio_probe)(void) {
    if (ps2_host_confirmed)
        return false;

    uint8_t cmd = 0;

    /* ACK stays open drain and DAT Hi-Z until the host is known to be a PS2. */
    gpio_set_oeover(PIN_PSX_ACK, GPIO_OVERRIDE_HIGH);
    pio_receive(&cmd);

    if (cmd == PS2_SIO2_CMD_0x11) {
        log(LOG_INFO, "PS2 host confirmed, enabling ACK and DAT push-pull drivers\n");
        gpio_set_outover(PIN_PSX_ACK, GPIO_OVERRIDE_NORMAL);
        gpio_set_oeover(PIN_PSX_ACK, GPIO_OVERRIDE_NORMAL);
        gpio_set_oeover(PIN_PSX_DAT, GPIO_OVERRIDE_NORMAL);
        ps2_mc_cmd_0x11();
        ps2_host_confirmed = true;
    } else {
        gpio_set_oeover(PIN_PSX_ACK, GPIO_OVERRIDE_LOW);
    }

    return true;
}

static void pio_deinit(void) {
    if (!pio_initialized)
        return;

    /* The owning core must detach its handler before Core 0 frees resources. */
    pio_sm_set_enabled(pio0, cmd_reader.sm, false);
    pio_sm_set_enabled(pio0, dat_writer.sm, false);
    pio_sm_clear_fifos(pio0, cmd_reader.sm);
    pio_sm_clear_fifos(pio0, dat_writer.sm);
    pio_remove_program(pio0, &cmd_reader_program, cmd_reader.offset);
    pio_sm_unclaim(pio0, cmd_reader.sm);
    pio_remove_program(pio0, &dat_writer_program, dat_writer.offset);
    pio_sm_unclaim(pio0, dat_writer.sm);
    log(LOG_TRACE, "Unclaimed %u, %u!\n", cmd_reader.sm, dat_writer.sm);
    gpio_set_oeover(PIN_PSX_ACK, GPIO_OVERRIDE_LOW);
    gpio_set_oeover(PIN_PSX_DAT, GPIO_OVERRIDE_LOW);
    cmd_reader = (pio_t){0};
    dat_writer = (pio_t){0};
    ps2_host_confirmed = false;
    card_active = false;
    pio_initialized = false;
}


static void pio_memory_card_startup(void) {

    log(LOG_TRACE, "Starting up PS2 memory card emulation\n");
    pio_irq_startup();

    gpio_set_slew_rate(PIN_PSX_DAT, GPIO_SLEW_RATE_FAST);
    gpio_set_drive_strength(PIN_PSX_DAT, GPIO_DRIVE_STRENGTH_12MA);

}

static void __time_critical_func(pio_card_deselected)(uint gpio, uint32_t event_mask) {
    if (gpio == PIN_PSX_SEL && (event_mask & GPIO_IRQ_EDGE_RISE)) {
        card_active = false;
        ps2_card_backend_pio.reset();
    }
}

// Call once on core 1, after mc_main_loop() has returned.
// Assumes IO_IRQ_BANK0 is owned exclusively by this subsystem.
static void pio_irq_shutdown(void) {

    log(LOG_TRACE, "%s\n", __func__);

    // Prevent the handler from running again on this core.
    irq_set_enabled(IO_IRQ_BANK0, false);

    // Disable SEL rising events for core 1.
    // The SDK also acknowledges the selected event here.
    gpio_set_irq_enabled(PIN_PSX_SEL, GPIO_IRQ_EDGE_RISE, false);

    // Stop the machines before their programs/resources are released.
    const uint32_t sm_mask =
        (1u << cmd_reader.sm) | (1u << dat_writer.sm);

    pio_set_sm_mask_enabled(pio0, sm_mask, false);

    pio_set_irq_callback(NULL);
    pio_irq_registered = false;

    gpio_set_oeover(PIN_PSX_ACK, GPIO_OVERRIDE_LOW);
    gpio_set_oeover(PIN_PSX_DAT, GPIO_OVERRIDE_LOW);
    card_active = false;
}


static void __time_critical_func(pio_acknowledge_irq)(uint gpio, uint32_t events) {
    check_gpio_param(gpio);
    iobank0_hw->intr[gpio / 8] = events << (4 * (gpio % 8));
}

static void __time_critical_func(pio_default_irq_handler)(void) {
    uint core = get_core_num();
    gpio_irq_callback_t callback = callbacks[core];
    io_bank0_irq_ctrl_hw_t *irq_ctrl_base = core ? &iobank0_hw->proc1_irq_ctrl : &iobank0_hw->proc0_irq_ctrl;
    for (uint gpio = 0; gpio < NUM_BANK0_GPIOS; gpio += 8) {
        uint32_t events8 = irq_ctrl_base->ints[gpio >> 3u];
        // note we assume events8 is 0 for non-existent GPIO
        for (uint i = gpio; events8 && i < gpio + 8; i++) {
            uint32_t events = events8 & 0xfu;
            if (events) {
                pio_acknowledge_irq(i, events);
                if (callback) {
                    callback(i, events);
                }
            }
            events8 >>= 4;
        }
    }
}

static void pio_set_irq_callback(gpio_irq_callback_t callback) {
    uint core = get_core_num();
    if (callbacks[core]) {
        if (!callback) {
            irq_remove_handler(IO_IRQ_BANK0, pio_default_irq_handler);
        }
        callbacks[core] = callback;
    } else if (callback) {
        callbacks[core] = callback;
        irq_add_shared_handler(IO_IRQ_BANK0, pio_default_irq_handler, GPIO_IRQ_CALLBACK_ORDER_PRIORITY);
    }
}

static void pio_set_irq_enabled_with_callback(uint gpio, uint32_t events, bool enabled, gpio_irq_callback_t callback) {
    gpio_set_irq_enabled(gpio, events, enabled);
    pio_set_irq_callback(callback);
    if (enabled)
        irq_set_enabled(IO_IRQ_BANK0, true);
}


static void __time_critical_func(pio_irq_startup)(void) {
    //if (pio_irq_registered)
    //    return;

    pio_set_irq_enabled_with_callback(PIN_PSX_SEL, GPIO_IRQ_EDGE_RISE, true, pio_card_deselected);

    pio_irq_registered = true;
    log(LOG_TRACE, "PS2 memory card SEL rising edge interrupt handler registered\n");
}

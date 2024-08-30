#include "hardware/pio.h"
#include "history_tracker/ps2_history_tracker.h"
#include "ps2_cardman.h"
#include "debug.h"
#include "hardware/gpio.h"
#include "hardware/timer.h"
#include "keystore.h"
#include "pico/multicore.h"
#include "pico/platform.h"
#include "ps2_mc_auth.h"
#include "ps2_mc_commands.h"
#include "ps2_mc_internal.h"
#include "ps2_mc_spi.pio.h"

#include <settings.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "ps2_sd2psxman_commands.h"

// #define DEBUG_MC_PROTOCOL

uint64_t us_startup;

volatile int reset;

typedef struct {
    uint32_t offset;
    uint32_t sm;
} pio_t;

pio_t cmd_reader, dat_writer, clock_probe;
uint8_t term = 0xFF;

static volatile uint8_t reset_tx_byte;
static volatile uint8_t reset_place_tx_byte;
static int memcard_running;

static volatile int mc_exit_request, mc_exit_response, mc_enter_request, mc_enter_response;

static void (*mc_callback)(void);


void __time_critical_func(ps2_queue_tx_byte_on_reset)(uint8_t byte)
{
    reset_place_tx_byte = 1;
    reset_tx_byte = byte;
}

static inline void __time_critical_func(RAM_pio_sm_drain_tx_fifo)(PIO pio, uint sm) {
    uint instr = (pio->sm[sm].shiftctrl & PIO_SM0_SHIFTCTRL_AUTOPULL_BITS) ? pio_encode_out(pio_null, 32) : pio_encode_pull(false, false);
    while (!pio_sm_is_tx_fifo_empty(pio, sm)) {
        pio_sm_exec(pio, sm, instr);
    }
}

static void __time_critical_func(reset_pio)(void) {
    pio_set_sm_mask_enabled(pio0, (1 << cmd_reader.sm) | (1 << dat_writer.sm) | (1 << clock_probe.sm), false);
    pio_restart_sm_mask(pio0, (1 << cmd_reader.sm) | (1 << dat_writer.sm) | (1 << clock_probe.sm));

    pio_sm_exec(pio0, cmd_reader.sm, pio_encode_jmp(cmd_reader.offset));
    pio_sm_exec(pio0, dat_writer.sm, pio_encode_jmp(dat_writer.offset));
    pio_sm_exec(pio0, clock_probe.sm, pio_encode_jmp(clock_probe.offset));

    pio_sm_clear_fifos(pio0, cmd_reader.sm);
    RAM_pio_sm_drain_tx_fifo(pio0, dat_writer.sm);
    pio_sm_clear_fifos(pio0, clock_probe.sm);

    pio_enable_sm_mask_in_sync(pio0, (1 << cmd_reader.sm) | (1 << dat_writer.sm) | (1 << clock_probe.sm));

    if (reset_place_tx_byte != 0) {
        mc_respond(reset_tx_byte);  //Preemptively place byte on tx for proper alignment with mmce fs read packets
        reset_place_tx_byte = 0;
        reset_tx_byte = 0;
    }

    reset = 1;
}

static void __time_critical_func(init_pio)(void) {

    reset_place_tx_byte = 0x0;
    reset_tx_byte = 0x0;

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

    clock_probe.offset = pio_add_program(pio0, &clock_probe_program);
    clock_probe.sm = pio_claim_unused_sm(pio0, true);

    cmd_reader_program_init(pio0, cmd_reader.sm, cmd_reader.offset);
    dat_writer_program_init(pio0, dat_writer.sm, dat_writer.offset);
    clock_probe_program_init(pio0, clock_probe.sm, clock_probe.offset);
}

static void __time_critical_func(card_deselected)(uint gpio, uint32_t event_mask) {
    if (gpio == PIN_PSX_SEL && (event_mask & GPIO_IRQ_EDGE_RISE)) {
        reset_pio();
    }
}

uint8_t __time_critical_func(receive)(uint8_t *cmd) {
    do {
        while (pio_sm_is_rx_fifo_empty(pio0, cmd_reader.sm) && pio_sm_is_rx_fifo_empty(pio0, cmd_reader.sm) && pio_sm_is_rx_fifo_empty(pio0, cmd_reader.sm) &&
            pio_sm_is_rx_fifo_empty(pio0, cmd_reader.sm) && pio_sm_is_rx_fifo_empty(pio0, cmd_reader.sm) && 1) {
            if (reset) {
                QPRINTF("Reset!!!!!\n");
                return RECEIVE_RESET;
            }
        }
        (*cmd) = (pio_sm_get(pio0, cmd_reader.sm) >> 24);
        return RECEIVE_OK;
    }
    while (0);
}

uint8_t __time_critical_func(receiveFirst)(uint8_t *cmd) {
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
        return RECEIVE_OK;
    }
    while (0);
}

void __time_critical_func(mc_respond)(uint8_t ch) {
    pio_sm_put_blocking(pio0, dat_writer.sm, ch);
}

void __time_critical_func(mc_respond_dma)(uint8_t* const ptr, uint8_t size) {

}

const uint8_t EccTable[] = {
    0x00, 0x87, 0x96, 0x11, 0xa5, 0x22, 0x33, 0xb4, 0xb4, 0x33, 0x22, 0xa5, 0x11, 0x96, 0x87, 0x00, 0xc3, 0x44, 0x55, 0xd2, 0x66, 0xe1, 0xf0, 0x77, 0x77, 0xf0,
    0xe1, 0x66, 0xd2, 0x55, 0x44, 0xc3, 0xd2, 0x55, 0x44, 0xc3, 0x77, 0xf0, 0xe1, 0x66, 0x66, 0xe1, 0xf0, 0x77, 0xc3, 0x44, 0x55, 0xd2, 0x11, 0x96, 0x87, 0x00,
    0xb4, 0x33, 0x22, 0xa5, 0xa5, 0x22, 0x33, 0xb4, 0x00, 0x87, 0x96, 0x11, 0xe1, 0x66, 0x77, 0xf0, 0x44, 0xc3, 0xd2, 0x55, 0x55, 0xd2, 0xc3, 0x44, 0xf0, 0x77,
    0x66, 0xe1, 0x22, 0xa5, 0xb4, 0x33, 0x87, 0x00, 0x11, 0x96, 0x96, 0x11, 0x00, 0x87, 0x33, 0xb4, 0xa5, 0x22, 0x33, 0xb4, 0xa5, 0x22, 0x96, 0x11, 0x00, 0x87,
    0x87, 0x00, 0x11, 0x96, 0x22, 0xa5, 0xb4, 0x33, 0xf0, 0x77, 0x66, 0xe1, 0x55, 0xd2, 0xc3, 0x44, 0x44, 0xc3, 0xd2, 0x55, 0xe1, 0x66, 0x77, 0xf0, 0xf0, 0x77,
    0x66, 0xe1, 0x55, 0xd2, 0xc3, 0x44, 0x44, 0xc3, 0xd2, 0x55, 0xe1, 0x66, 0x77, 0xf0, 0x33, 0xb4, 0xa5, 0x22, 0x96, 0x11, 0x00, 0x87, 0x87, 0x00, 0x11, 0x96,
    0x22, 0xa5, 0xb4, 0x33, 0x22, 0xa5, 0xb4, 0x33, 0x87, 0x00, 0x11, 0x96, 0x96, 0x11, 0x00, 0x87, 0x33, 0xb4, 0xa5, 0x22, 0xe1, 0x66, 0x77, 0xf0, 0x44, 0xc3,
    0xd2, 0x55, 0x55, 0xd2, 0xc3, 0x44, 0xf0, 0x77, 0x66, 0xe1, 0x11, 0x96, 0x87, 0x00, 0xb4, 0x33, 0x22, 0xa5, 0xa5, 0x22, 0x33, 0xb4, 0x00, 0x87, 0x96, 0x11,
    0xd2, 0x55, 0x44, 0xc3, 0x77, 0xf0, 0xe1, 0x66, 0x66, 0xe1, 0xf0, 0x77, 0xc3, 0x44, 0x55, 0xd2, 0xc3, 0x44, 0x55, 0xd2, 0x66, 0xe1, 0xf0, 0x77, 0x77, 0xf0,
    0xe1, 0x66, 0xd2, 0x55, 0x44, 0xc3, 0x00, 0x87, 0x96, 0x11, 0xa5, 0x22, 0x33, 0xb4, 0xb4, 0x33, 0x22, 0xa5, 0x11, 0x96, 0x87, 0x00};

void calcECC(uint8_t *ecc, const uint8_t *data) {
    int i, c;

    ecc[0] = ecc[1] = ecc[2] = 0;

    for (i = 0; i < 0x80; i++) {
        c = EccTable[data[i]];

        ecc[0] ^= c;
        if (c & 0x80) {
            ecc[1] ^= ~i;
            ecc[2] ^= i;
        }
    }
    ecc[0] = ~ecc[0];
    ecc[0] &= 0x77;

    ecc[1] = ~ecc[1];
    ecc[1] &= 0x7f;

    ecc[2] = ~ecc[2];
    ecc[2] &= 0x7f;

    return;
}

static void __time_critical_func(mc_main_loop)(void) {
    while (1) {
        uint8_t cmd = 0;

        while (!reset && !reset && !reset && !reset && !reset) {
            if (mc_exit_request) {
                mc_exit_response = 1;
                return;
            }
        }
        
        reset = 0;
        if (mc_callback != NULL) {
            mc_callback();
            continue;
        }
        uint8_t received = receiveFirst(&cmd);

        if (received == RECEIVE_EXIT) {
            mc_exit_response = 1;
            break;
        }
        if (received == RECEIVE_RESET)
            continue;


        if (cmd == PS2_SIO2_CMD_IDENTIFIER) {
            /* resp to 0x81 */
            mc_respond(0xFF);

            /* sub cmd */
            if (receive(&cmd) == RECEIVE_RESET)
                continue;

            switch (cmd) {
                case PS2_SIO2_CMD_0x11: ps2_mc_cmd_0x11(); break;
                case PS2_SIO2_CMD_0x12: ps2_mc_cmd_0x12(); break;
                case PS2_SIO2_CMD_SET_ERASE_ADDRESS: ps2_mc_cmd_setEraseAddress(); break;
                case PS2_SIO2_CMD_SET_WRITE_ADDRESS: ps2_mc_cmd_setWriteAddress(); break;
                case PS2_SIO2_CMD_SET_READ_ADDRESS: ps2_mc_cmd_setReadAddress(); break;
                case PS2_SIO2_CMD_GET_SPECS: ps2_mc_cmd_getSpecs(); break;
                case PS2_SIO2_CMD_SET_TERMINATOR: ps2_mc_cmd_setTerminator(); break;
                case PS2_SIO2_CMD_GET_TERMINATOR: ps2_mc_cmd_getTerminator(); break;
                case PS2_SIO2_CMD_WRITE_DATA: ps2_mc_cmd_writeData(); break;
                case PS2_SIO2_CMD_READ_DATA: ps2_mc_cmd_readData(); break;
                case PS2_SIO2_CMD_COMMIT_DATA: ps2_mc_cmd_commitData(); break;
                case PS2_SIO2_CMD_ERASE: ps2_mc_cmd_erase(); break;
                case PS2_SIO2_CMD_BF: ps2_mc_cmd_0xBF(); break;
                case PS2_SIO2_CMD_F3: ps2_mc_cmd_0xF3(); break;
                case PS2_SIO2_CMD_KEY_SELECT: ps2_mc_cmd_keySelect(); break;
                case PS2_SIO2_CMD_AUTH:
                    if (ps2_magicgate)
                        ps2_mc_auth();
                    break;
                case PS2_SIO2_CMD_SESSION_KEY_0:
                case PS2_SIO2_CMD_SESSION_KEY_1: ps2_mc_sessionKeyEncr(); break;
                default: DPRINTF("Unknown Subcommand: %02x\n", cmd); break;
            }
        } else if (cmd == PS2_SD2PSXMAN_CMD_IDENTIFIER) {
            /* resp to 0x8B */
            mc_respond(0xAA);

            /* sub cmd */
            receiveOrNextCmd(&cmd);

            //DPRINTF("RCVCMD %d\n", cmd);

            switch (cmd)
            {
                case SD2PSXMAN_PING: ps2_sd2psxman_cmds_ping(); break;
                case SD2PSXMAN_GET_STATUS: ps2_sd2psxman_cmds_get_status(); break;
                case SD2PSXMAN_GET_CARD: ps2_sd2psxman_cmds_get_card(); break;
                case SD2PSXMAN_SET_CARD: ps2_sd2psxman_cmds_set_card(); break;
                case SD2PSXMAN_GET_CHANNEL: ps2_sd2psxman_cmds_get_channel(); break;
                case SD2PSXMAN_SET_CHANNEL: ps2_sd2psxman_cmds_set_channel(); break;
                case SD2PSXMAN_GET_GAMEID: ps2_sd2psxman_cmds_get_gameid(); break;
                case SD2PSXMAN_SET_GAMEID: ps2_sd2psxman_cmds_set_gameid(); break;
                case SD2PSXMAN_UNMOUNT_BOOTCARD: ps2_sd2psxman_cmds_unmount_bootcard(); break;

                case MMCEMAN_CMD_FS_OPEN: ps2_mmceman_cmd_fs_open(); break;
                case MMCEMAN_CMD_FS_CLOSE: ps2_mmceman_cmd_fs_close(); break;
                case MMCEMAN_CMD_FS_READ: ps2_mmceman_cmd_fs_read(); break;
                case MMCEMAN_CMD_FS_WRITE: ps2_mmceman_cmd_fs_write(); break;
                case MMCEMAN_CMD_FS_LSEEK: ps2_mmceman_cmd_fs_lseek(); break;
                case MMCEMAN_CMD_FS_REMOVE: ps2_mmceman_cmd_fs_remove(); break;
                case MMCEMAN_CMD_FS_MKDIR: ps2_mmceman_cmd_fs_mkdir(); break;
                case MMCEMAN_CMD_FS_RMDIR: ps2_mmceman_cmd_fs_rmdir(); break;
                case MMCEMAN_CMD_FS_DOPEN: ps2_mmceman_cmd_fs_dopen(); break;
                case MMCEMAN_CMD_FS_DCLOSE: ps2_mmceman_cmd_fs_dclose(); break;
                case MMCEMAN_CMD_FS_DREAD: ps2_mmceman_cmd_fs_dread(); break;
                case MMCEMAN_CMD_FS_GETSTAT: ps2_mmceman_cmd_fs_getstat(); break;

                case MMCEMAN_CMD_FS_LSEEK64: ps2_mmceman_cmd_fs_lseek64(); break;
                case MMCEMAN_CMD_FS_READ_SECTOR: ps2_mmceman_cmd_fs_read_sector(); break;
                default: printf("Unknown Subcommand: %02x\n", cmd); break;
            }
        } else if (cmd == PS1_SIO2_CMD_IDENTIFIER) {
            settings_set_mode(MODE_TEMP_PS1);
            continue;;
        } else {
            // not for us
            continue;
        } 
    }
}

static void __no_inline_not_in_flash_func(mc_main)(void) {
    while (1) {
        while (!mc_enter_request) {}
        mc_enter_response = 1;
        while (!ps2_cardman_is_accessible()) {}
        ps2_history_tracker_card_changed();
        memcard_running = 1;
        
        reset_pio();
        mc_main_loop();
        QPRINTF("%s exit\n", __func__);
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
    for (uint gpio = 0; gpio < NUM_BANK0_GPIOS; gpio += 8) {
        uint32_t events8 = irq_ctrl_base->ints[gpio >> 3u];
        // note we assume events8 is 0 for non-existent GPIO
        for (uint i = gpio; events8 && i < gpio + 8; i++) {
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
    if (enabled)
        irq_set_enabled(IO_IRQ_BANK0, true);
}

void ps2_memory_card_main(void) {
    multicore_lockout_victim_init();
    init_pio();
    generateIvSeedNonce();

    us_startup = time_us_64();
    QPRINTF("Secondary core!\n");

    my_gpio_set_irq_enabled_with_callback(PIN_PSX_SEL, GPIO_IRQ_EDGE_RISE, 1, card_deselected);
    
    gpio_set_slew_rate(PIN_PSX_DAT, GPIO_SLEW_RATE_FAST);
    gpio_set_drive_strength(PIN_PSX_DAT, GPIO_DRIVE_STRENGTH_12MA);

    mc_main();
}


void ps2_memory_card_exit(void) {
    QPRINTF("MEMCARD EXIT!\n");
    if (!memcard_running)
        return;

    mc_exit_request = 1;
    while (!mc_exit_response) { };
    mc_exit_request = mc_exit_response = 0;
    memcard_running = 0;
    QPRINTF("MEMCARD EXIT END!\n");
}

void ps2_memory_card_enter(void) {
    if (memcard_running)
        return;

    mc_enter_request = 1;
    while (!mc_enter_response) {}
    mc_enter_request = mc_enter_response = 0;
}

void ps2_memory_card_set_cmd_callback(void (*cb)(void)) {
    mc_callback = cb;
}

void ps2_memory_card_unload(void) {
    pio_remove_program(pio0, &cmd_reader_program, cmd_reader.offset);
    pio_sm_unclaim(pio0, cmd_reader.sm);
    pio_remove_program(pio0, &dat_writer_program, dat_writer.offset);
    pio_sm_unclaim(pio0, dat_writer.sm);
    pio_remove_program(pio0, &clock_probe_program, clock_probe.offset);
    pio_sm_unclaim(pio0, clock_probe.sm);
}

bool ps2_memory_card_running(void) {
    return (memcard_running != 0);
}
#include "hardware/gpio.h"
#include "hardware/timer.h"
#include "pico/platform.h"
#include "string.h"
#include <stdint.h>

#include "config.h"
#include "ps1_mc_spi.pio.h"
#include "debug.h"
#include <psram/psram.h>
#include "ps1_dirty.h"
#include "ps1/ps1_memory_card.h"
#include "game_names/game_names.h"

static uint64_t us_startup;

static size_t byte_count;
static volatile int reset;
static int ignore;
static uint8_t flag;

static size_t game_id_length;
static char received_game_id[0x10];
static volatile uint8_t mc_pro_command;


typedef struct {
    uint32_t offset;
    uint32_t sm;
} pio_t;

static pio_t cmd_reader, dat_writer;
static volatile int mc_exit_request, mc_exit_response, mc_enter_request, mc_enter_response;


static void __time_critical_func(reset_pio)(void) {
    pio_set_sm_mask_enabled(pio0, (1 << cmd_reader.sm) | (1 << dat_writer.sm), false);
    pio_restart_sm_mask(pio0, (1 << cmd_reader.sm) | (1 << dat_writer.sm));

    pio_sm_exec(pio0, cmd_reader.sm, pio_encode_jmp(cmd_reader.offset));
    pio_sm_exec(pio0, dat_writer.sm, pio_encode_jmp(dat_writer.offset));

    pio_sm_clear_fifos(pio0, cmd_reader.sm);
    pio_sm_drain_tx_fifo(pio0, dat_writer.sm);

    pio_enable_sm_mask_in_sync(pio0, (1 << cmd_reader.sm) | (1 << dat_writer.sm));

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

    cmd_reader_program_init(pio0, cmd_reader.sm, cmd_reader.offset);
    dat_writer_program_init(pio0, dat_writer.sm, dat_writer.offset);
}

static void __time_critical_func(card_deselected)(uint gpio, uint32_t event_mask) {
    if (gpio == PIN_PSX_SEL && (event_mask & GPIO_IRQ_EDGE_RISE))
        reset_pio();
}

static uint8_t __time_critical_func(recv_cmd)(void) {
    while (pio_sm_is_rx_fifo_empty(pio0, cmd_reader.sm) && pio_sm_is_rx_fifo_empty(pio0, cmd_reader.sm))  {
        if (mc_exit_request)
            return 0;
    }
    return (uint8_t) (pio_sm_get(pio0, cmd_reader.sm) >> 24);
}

static int __time_critical_func(mc_do_state)(uint8_t ch) {
    static uint8_t payload[256];
    if (byte_count >= sizeof(payload))
        return -1;
    payload[byte_count++] = ch;

    if (byte_count == 1) {
        /* First byte - determine the device the command is for */
        if (ch == 0x81)
            return flag;
    } else if (payload[0] == 0x81) {
        /* Command for the memory card */
        uint8_t cmd = payload[1];

        if (cmd == 'S') {
            /* Memory card status */
            switch (byte_count) {
                case 2: return 0x5A;
                case 3: return 0x5D;
                case 4: return 0x5C;
                case 5: return 0x5D;
                case 6: return 0x04;
                case 7: return 0x00;
                case 8: return 0x00;
                case 9: return 0x80;
            }
        } else if (cmd == 'R') {
            /* Memory card read */
            #define MSB (payload[4])
            #define LSB (payload[5])
            #define OFF ((MSB * 256 + LSB) * 128 + byte_count - 10)

            static uint8_t chk;

            switch (byte_count) {
                case 2: return 0x5A;
                case 3: return 0x5D;
                case 4: return 0x00;
                case 5: return MSB;
                case 6: return 0x5C;
                case 7: return 0x5D;
                case 8: return MSB;
                case 9: chk = MSB ^ LSB; return LSB;
                case 10 ... 137: {
                    uint8_t data;
                    psram_read(OFF, &data, 1);
                    chk ^= data;
                    return data;
                }
                case 138: return chk;
                case 139: return 0x47;
            }

            #undef MSB
            #undef LSB
            #undef OFF
        } else if (cmd == 'W') {
            /* Memory card write */
            #define MSB (payload[4])
            #define LSB (payload[5])
            #define OFF ((MSB * 256 + LSB) * 128 + byte_count - 7)

            static uint8_t chk;

            switch (byte_count) {
                case 2: flag = 0; return 0x5A;
                case 3: return 0x5D;
                case 4: return 0x00;
                case 5: return MSB;
                case 6: return LSB;
                case 7: chk = MSB ^ LSB; // fallthrough
                case 8 ... 134: {
                    psram_write(OFF, &payload[byte_count - 1], 1);
                    chk ^= payload[byte_count - 1];
                    return payload[byte_count - 1];
                }
                case 135: return 0x5C;
                case 136: return 0x5D;
                case 137: {
                    if (chk == payload[byte_count - 3]) {
                        ps1_dirty_mark(MSB * 256 + LSB);
                        return 0x47;
                    } else
                        return 0x4E;
                }
            }

            #undef MSB
            #undef LSB
            #undef OFF
        }
        // Memcard Pro Commands after this line
        // See https://gitlab.com/chriz2600/ps1-game-id-transmission
        else if (cmd == 0x20) {   // MCP Ping Command
            switch (byte_count) {
                case 2:
                case 3: return 0x00;
                case 4: return 0x27;
                case 5: return 0xFF; 
            }
        } else if (cmd == 0x21) { // MCP Game ID
            if (byte_count == game_id_length + 4)
            {
                game_names_extract_title_id(&payload[4], received_game_id, game_id_length, sizeof(received_game_id));
                if (game_names_sanity_check_title_id(received_game_id))
                    mc_pro_command = MCP_GAME_ID;
                else
                    memset(received_game_id, 0, sizeof(received_game_id));
            }
            switch (byte_count) {
                case 2: memset(received_game_id, 0, sizeof(received_game_id)); return 0x00;
                case 3: return 0x00;
                case 4: game_id_length = payload[byte_count - 1]; return 0x00;
                case 5 ... 255: return payload[byte_count - 1];
            }
        } else if (cmd == 0x22) { // MCP Prv Channel
            switch (byte_count) {
                case 2: 
                case 3: return 0x00;
                case 4: return 0x20;
                case 5: mc_pro_command = MCP_PRV_CH; return 0xFF; 
            }
        } else if (cmd == 0x23) { // MCP Nxt Channel
            switch (byte_count) {
                case 2: 
                case 3: return 0x00;
                case 4: return 0x20;
                case 5: mc_pro_command = MCP_NXT_CH; return 0xFF; 
            }
        } else if (cmd == 0x24) { // MCP Prv Card
            
            switch (byte_count) {
                case 2: 
                case 3: return 0x00;
                case 4: return 0x20;
                case 5: mc_pro_command = MCP_PRV_CARD; return 0xFF; 
            }
        } else if (cmd == 0x25) { // MCP Nxt Card
            
            switch (byte_count) {
                case 2:
                case 3: return 0x00;
                case 4: return 0x20;
                case 5: mc_pro_command = MCP_NXT_CARD; return 0xFF; 
            }
        } else {
            debug_printf("Received unknown command: %u\n", ch);
        }
    }

    return -1;
}

static void __time_critical_func(mc_respond)(uint8_t ch) {
    pio_sm_put_blocking(pio0, dat_writer.sm, ~ch & 0xFF);
}

static void __time_critical_func(mc_main_loop)(void) {
    flag = 8;

    while (1) {
        uint8_t ch = recv_cmd();

        if (mc_exit_request)
            break;

        /* If this ch belongs to the next command sequence */
        if (reset)
            reset = ignore = byte_count = 0;

        /* If the command sequence is not to be processed (e.g. controller command or unknown) */
        if (ignore)
            continue;

        /* Process by the state machine */
        int next = mc_do_state(ch);

        /* Respond on next byte transfer or stop responding to this sequence altogether if it's not for us */
        if (next == -1)
            ignore = 1;
        else {
            // debug_printf("R %02X -> %02X\n", ch, next);
            mc_respond(next);
        }
    }

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

void ps1_memory_card_main(void) {
    init_pio();

    us_startup = time_us_64();
    debug_printf("Secondary core!\n");

    my_gpio_set_irq_enabled_with_callback(PIN_PSX_SEL, GPIO_IRQ_EDGE_RISE, 1, card_deselected);

    mc_main();
}

static int memcard_running;

void ps1_memory_card_exit(void) {
    if (!memcard_running)
        return;

    mc_exit_request = 1;
    while (!mc_exit_response)
    {}
    mc_exit_request = mc_exit_response = 0;
    memcard_running = 0;
}

void ps1_memory_card_enter(void) {
    if (memcard_running)
        return;

    mc_enter_request = 1;
    while (!mc_enter_response)
    {}
    mc_enter_request = mc_enter_response = 0;
    memcard_running = 1;
    mc_pro_command = 0;
}

void ps1_memory_card_reset_ode_command(void) {
    mc_pro_command = 0;
}

uint8_t ps1_memory_card_get_ode_command(void) {
    return mc_pro_command;
}

const char* ps1_memory_card_get_game_id(void) {
    return received_game_id;
}
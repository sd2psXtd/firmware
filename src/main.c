#include <stdio.h>
#include "hardware/watchdog.h"
#if WITH_LED
#include "led.h"
#endif
#include "pico/stdio.h"
#include "pico/stdlib.h"
#include "pico/bootrom.h"
#include "pico/multicore.h"
#include "hardware/clocks.h"
#include "hardware/structs/bus_ctrl.h"

#if WITH_GUI
#include "oled.h"
#include "gui.h"
#endif
#include "input.h"
#include "config.h"
#include "debug.h"
#include "pico/time.h"
#include "sd.h"
#include "settings.h"
#include "version/version.h"
#if WITH_PSRAM
#include "psram/psram.h"
#endif

#include "ps1/ps1_memory_card.h"
#include "ps1/ps1_cardman.h"

#include "ps2.h"
#include "ps1.h"

#include "card_emu/ps2_memory_card.h"
#include "mmceman/ps2_mmceman.h"
#include "mmceman/ps2_mmceman_commands.h"
#include "ps2_cardman.h"


#include "game_db/game_db.h"

/* reboot to bootloader if either button is held on startup
   to make the device easier to flash when assembled inside case */
static void check_bootloader_reset(void) {
    /* make sure at least DEBOUNCE interval passes or we won't get inputs */
    for (int i = 0; i < 2 * DEBOUNCE_MS; ++i) {
        input_task();
        sleep_ms(1);
    }

    if (input_is_down_raw(0) || input_is_down_raw(1))
        reset_usb_boot(0, 0);
}

static void debug_task(void) {

    for (int i = 0; i < 10; ++i) {
        char ch = debug_get();
        if (ch) {
            #if DEBUG_USB_UART
                putchar(ch);
            #else
                if (ch == '\n')
                    uart_putc_raw(UART_PERIPH, '\r');
                uart_putc_raw(UART_PERIPH, ch);
            #endif
        } else {
            break;
        }
    }
#if DEBUG_USB_UART
    int charin = getchar_timeout_us(0);
    if ((charin != PICO_ERROR_TIMEOUT) && (charin > 0x20) && (charin < 0x7A)) {
        QPRINTF("Got %c Input\n", charin);

        char in[3] = {0};
        in[0] = charin;
        in[1] = getchar_timeout_us(1000*1000*3);
        in[2] = getchar_timeout_us(1000*1000*3);
        if (in[0] == 'b') {
            if ((in[1] == 'l') && (in[2] == 'r')) {
                QPRINTF("Resetting to Bootloader");
                reset_usb_boot(0, 0);
            }
        } else if (in[0] == 'r') {
            if ((in[1] == 'r') && (in[2] == 'r')) {
                QPRINTF("Resetting");
                watchdog_reboot(0, 0, 0);
            }
        } else if (in[0] == 'c') {
            if ((in[1] == 'h') && (in[2] == '+')) {
                DPRINTF("Received Channel Up!\n");
                if (settings_get_mode(true) == MODE_PS2) {
                    mmceman_mode = MMCEMAN_MODE_NEXT;
                    mmceman_cmd = MMCEMAN_SET_CHANNEL;
                }  else {
                    ps1_memory_card_exit();
                    ps1_cardman_close();
                    ps1_cardman_next_channel();
                    ps1_cardman_open();
                    ps1_memory_card_enter();
                }
            } else if ((in[1] == 'h') && (in[2] == '-')) {
                DPRINTF("Received Channel Down!\n");
                if (settings_get_mode(true) == MODE_PS2) {
                    mmceman_mode = MMCEMAN_MODE_PREV;
                    mmceman_cmd = MMCEMAN_SET_CHANNEL;
                } else {
                    ps1_memory_card_exit();
                    ps1_cardman_close();
                    ps1_cardman_prev_channel();
                    ps1_cardman_open();
                    ps1_memory_card_enter();
                }
            } else if (in[1] == '+') {
                DPRINTF("Received Card Up!\n");
                if (settings_get_mode(true) == MODE_PS2) {
                    mmceman_mode = MMCEMAN_MODE_NEXT;
                    mmceman_cmd = MMCEMAN_SET_CARD;
                } else {
                    ps1_memory_card_exit();
                    ps1_cardman_close();
                    ps1_cardman_next_idx();
                    ps1_cardman_open();
                    ps1_memory_card_enter();
                }
            } else if (in[1] == '-') {
                DPRINTF("Received Card Down!\n");
                if (settings_get_mode(true) == MODE_PS2) {
                    mmceman_mode = MMCEMAN_MODE_PREV;
                    mmceman_cmd = MMCEMAN_SET_CARD;
                } else {
                    ps1_memory_card_exit();
                    ps1_cardman_close();
                    ps1_cardman_prev_idx();
                    ps1_cardman_open();
                    ps1_memory_card_enter();
                }
            }
        }
    }
#endif
}



int main() {
    input_init();
    check_bootloader_reset();

    printf("prepare...\n");
    int mhz = 240;
    set_sys_clock_khz(mhz * 1000, true);
    clock_configure(clk_peri, 0, CLOCKS_CLK_PERI_CTRL_AUXSRC_VALUE_CLK_SYS, mhz * 1000000, mhz * 1000000);

#if DEBUG_USB_UART
    stdio_usb_init();
#else
    stdio_uart_init_full(UART_PERIPH, UART_BAUD, UART_TX, UART_RX);
#endif

    /* set up core1 as high priority bus access */
    bus_ctrl_hw->priority |= BUSCTRL_BUS_PRIORITY_PROC1_BITS;
    while (!bus_ctrl_hw->priority_ack) {}

    printf("\n\n\nStarted! Clock %d; bus priority 0x%X\n", (int)clock_get_hz(clk_sys), (unsigned)bus_ctrl_hw->priority);
    printf("SD2PSX Version %s\n", sd2psx_version);
    printf("SD2PSX HW Variant: %s\n", sd2psx_variant);

    settings_init();
#if WITH_PSRAM
    psram_init();
#endif
    game_db_init();

#if WITH_LED
    led_init();
#endif

    while (1) {
        if (settings_get_mode(true) == MODE_PS2) {
            ps2_init();
            settings_load_sd();
            while (1) {
                debug_task();
                if (!ps2_task())
                    break;
            }
            ps2_deinit();

        } else {
            ps1_init();
            settings_load_sd();
            while (1) {
                debug_task();
                if (!ps1_task())
                    break;
            }
            ps1_deinit();
        }
    }
}

#include <serial_input.h>

#if WITH_LED
#include "led.h"
#endif
#include "pico/stdio.h"
#include "pico/stdlib.h"
#include "pico/bootrom.h"
#include "flashmap.h"
#include "hardware/clocks.h"
#include "hardware/structs/bus_ctrl.h"

#include "input.h"
#include "config.h"
#include "debug.h"
#include "pico/time.h"
#include "sd.h"
#include "settings.h"
#include "usb/mca_device.h"
#if WITH_PSRAM
#include "psram/psram.h"
#endif

#include "ps2.h"
#include "ps2/mmceman/ps2_mmceman.h"
#include "ps1.h"

#include "game_db/game_db.h"
#include "version.h"


uint flash_capacity = 0;
uint flash_border = 0;


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

    bool wrote_debug_output = false;
    while (true) {
        char ch = debug_get();
        if (ch) {
            if (!wrote_debug_output) {
                serial_input_begin_output();
            }
            wrote_debug_output = true;
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

    if (wrote_debug_output) {
        serial_input_notify_output();
    }
    serial_input_process();
}



int main() {
    int mhz = 240;
    input_init();
    update_flash_capacity();
    check_bootloader_reset();

    QPRINTF("prepare...\n");

    set_sys_clock_khz(mhz * 1000, true);
    clock_configure(clk_peri, 0, CLOCKS_CLK_PERI_CTRL_AUXSRC_VALUE_CLK_SYS, mhz * 1000000, mhz * 1000000);

#if DEBUG_USB_UART
    /* CDC is started after selecting the USB profile below. */
#else
    stdio_uart_init_full(UART_PERIPH, UART_BAUD, UART_TX, UART_RX);
#endif

    /* set up core1 as high priority bus access */
    bus_ctrl_hw->priority |= BUSCTRL_BUS_PRIORITY_PROC1_BITS;
    while (!bus_ctrl_hw->priority_ack) {}

    QPRINTF("\n\n\nStarted! Clock %d; bus priority 0x%X\n", (int)clock_get_hz(clk_sys), (unsigned)bus_ctrl_hw->priority);
    QPRINTF("SD2PSX Version %s\n", sd2psx_version);
    QPRINTF("SD2PSX HW Variant: %s\n", sd2psx_variant);
    QPRINTF("Flash size: %d MB\n", flash_capacity / (1024 * 1024));
    QPRINTF("EEPROM base: 0x%X\n", WEAR_LEVELING_RP2040_FLASH_BASE);
    QPRINTF("CIV base: 0x%X\n", FLASH_OFF_CIV);
    QPRINTF("Splash base: 0x%X\n", FLASH_OFF_SPLASH);

    settings_init();
    usb_session_init();
#if WITH_PSRAM
    psram_init();
#endif
    game_db_init();

#if WITH_LED
    led_init();
#endif

    sd_init();

    while (1) {
        /* Load the SD settings before choosing the mode, USB identity, and
         * memory-card backend for this session. Keep the per-session reload
         * so settings changed on SD are observed after every mode switch. */
        settings_load_sd();
        usb_session_prepare();
        void (*mode_init)(void);
        bool (*mode_task)(void);
        void (*mode_deinit)(void);
        if (settings_get_mode(true) == MODE_PS2) {
            QPRINTF("Starting PS2 mode...\n");
            mode_init = ps2_init;
            mode_task = ps2_task;
            mode_deinit = ps2_deinit;
        } else {
            QPRINTF("Starting PS1 mode...\n");
            mode_init = ps1_init;
            mode_task = ps1_task;
            mode_deinit = ps1_deinit;
        }

        mode_init();
        usb_session_start();
        while (true) {
            usb_session_poll();
            debug_task();
            bool mode_running = mode_task();
            if (!usb_session_continue(mode_running))
                break;
        }
        usb_session_drain();
        mode_deinit();

        // ReArm PS2 retry counter
        ps2_mmceman_rearm_retry_counter();
        usb_session_stop();
    }
}

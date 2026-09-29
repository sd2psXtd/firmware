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
#if WITH_GUI
#include "gui.h"
#endif
#if WITH_PSRAM
#include "psram/psram.h"
#endif

#include "ps2.h"
#include "ps2/ps2_cardman.h"
#include "ps1.h"

#include "game_db/game_db.h"
#include "version.h"
#include "usb/mca_usb.h"


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

static void start_usb_for_card(void) {
    bool enabled = settings_get_mca_enabled();
    if (mca_usb_is_enabled() != enabled) {
        mca_usb_set_enabled(enabled);
#if WITH_GUI
        gui_mca_mode_changed();
#endif
    }
    mca_usb_start();
}

int main() {
    int mhz = 240;
    input_init();
    update_flash_capacity();
    check_bootloader_reset();

    QPRINTF("prepare...\n");

    set_sys_clock_khz(mhz * 1000, true);
    clock_configure(clk_peri, 0, CLOCKS_CLK_PERI_CTRL_AUXSRC_VALUE_CLK_SYS, mhz * 1000000, mhz * 1000000);

    /* Select the saved transport before starting the card emulator. */
    settings_init();
    mca_usb_init();

#if DEBUG_USB_UART
    /* CDC stdio is registered when Pico USB starts. */
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

#if WITH_PSRAM
    psram_init();
#endif
    game_db_init();

#if WITH_LED
    led_init();
#endif

    sd_init();

    while (1) {
        if (settings_get_mode(true) == MODE_PS2) {
            QPRINTF("Starting PS2 mode...\n");
            ps2_init();
            settings_load_sd();
            /* ps2_init() opens the card, but PSRAM loading or card creation
             * continues in ps2_task(). Finish it before exposing MCA to USB. */
            if (!mca_usb_is_started() && settings_get_mca_enabled()) {
                while (!ps2_cardman_is_idle())
                    ps2_task();
            }
            start_usb_for_card();
            do {
                mca_usb_task();
                debug_task();
            } while(ps2_task());
            ps2_deinit();

        } else {
            QPRINTF("Starting PS1 mode...\n");
            ps1_init();
            settings_load_sd();
            start_usb_for_card();
            do {
                mca_usb_task();
                debug_task();
            } while(ps1_task());
            ps1_deinit();
        }
    }
}

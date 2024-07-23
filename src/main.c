#include <stdio.h>
#include "history_tracker/ps2_history_tracker.h"
#include "pico/stdlib.h"
#include "pico/bootrom.h"
#include "pico/multicore.h"
#include "hardware/clocks.h"
#include "hardware/structs/bus_ctrl.h"

#include "oled.h"
#include "gui.h"
#include "input.h"
#include "config.h"
#include "debug.h"
#include "pico/time.h"
#include "sd.h"
#include "keystore.h"
#include "settings.h"
#include "version/version.h"
#include "psram/psram.h"

#include "ps1/ps1_memory_card.h"
#include "ps1/ps1_dirty.h"
#include "ps1/ps1_cardman.h"
#include "ps1/ps1_odeman.h"

#include "ps2/ps2_dirty.h"
#include "ps2/card_emu/ps2_memory_card.h"
#include "ps2/ps2_cardman.h"

#include "ps2/card_emu/ps2_sd2psxman.h"
#include "mmce_fs/ps2_mmce_fs.h"

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
            if (ch == '\n')
                uart_putc_raw(UART_PERIPH, '\r');
            uart_putc_raw(UART_PERIPH, ch);
            #if DEBUG_USB_UART
                putchar(ch);
            #endif
        } else {
            break;
        }
    }
}

static void init_ps1(void) {
    printf("starting in PS1 mode\n");

    ps1_cardman_init();
    ps1_dirty_init();

    gui_init();

    multicore_launch_core1(ps1_memory_card_main);

    printf("Starting memory card... ");
    uint64_t start = time_us_64();
    gui_do_ps1_card_switch();
    uint64_t end = time_us_64();
    printf("DONE! (%d us)\n", (int)(end - start));
}

static void init_ps2(void) {
    printf("starting in PS2 mode\n");

    keystore_init();
    
    ps2_cardman_init();
    ps2_dirty_init();
    ps2_history_tracker_init();

    multicore_launch_core1(ps2_memory_card_main);
    gui_init();
    ps2_mmce_fs_init();

    printf("Starting memory card... ");
    uint64_t start = time_us_64();
    gui_do_ps2_card_switch();
    uint64_t end = time_us_64();
    printf("DONE! (%d us)\n", (int)(end - start));
}

static void run_ps1(void) {
    while (1) {
        debug_task();
        ps1_odeman_task();
        ps1_dirty_task();
        gui_task();
        input_task();
        oled_task();
        if ((settings_get_mode() == MODE_PS2))
            break;
    }
}

static void run_ps2(void) {
    while (1) {
        debug_task();
        ps2_sd2psxman_task();
        if (ps2_cardman_is_idle())
            ps2_dirty_task();
        else
            ps2_cardman_task();

        ps2_history_tracker_task();
        gui_task();
        input_task();
        oled_task();
        ps2_mmce_fs_run();
        if ((settings_get_mode() == MODE_PS1) && (ps2_cardman_is_idle()))
            break;
    }
}

int main() {
    input_init();
    check_bootloader_reset();

#if DEBUG_USB_UART
    stdio_usb_init();
    sleep_ms(STARTUP_DELAY * 1000);
#else
    stdio_uart_init_full(UART_PERIPH, UART_BAUD, UART_TX, UART_RX);
#endif

    printf("prepare...\n");
    int mhz = 240;
    set_sys_clock_khz(mhz * 1000, true);
    clock_configure(clk_peri, 0, CLOCKS_CLK_PERI_CTRL_AUXSRC_VALUE_CLK_SYS, mhz * 1000000, mhz * 1000000);

    stdio_uart_init_full(UART_PERIPH, UART_BAUD, UART_TX, UART_RX);

    /* set up core1 as high priority bus access */
    bus_ctrl_hw->priority |= BUSCTRL_BUS_PRIORITY_PROC1_BITS;
    while (!bus_ctrl_hw->priority_ack) {}

    printf("\n\n\nStarted! Clock %d; bus priority 0x%X\n", (int)clock_get_hz(clk_sys), (unsigned)bus_ctrl_hw->priority);
    printf("SD2PSX Version %s\n", sd2psx_version);

    settings_init();
    psram_init();

    game_db_init();

    while (1) {
        if (settings_get_mode() == MODE_PS2) {
            init_ps2();
            run_ps2();
            ps2_memory_card_exit();
            while(ps2_dirty_activity)
                ps2_dirty_task();
            multicore_reset_core1();
            ps2_cardman_close();
            ps2_memory_card_unload();
        } else {
            init_ps1();
            run_ps1();
            ps1_memory_card_exit();
            while(ps1_dirty_activity)
                ps1_dirty_task();        
            ps1_cardman_close();
            multicore_reset_core1();
            ps1_memory_card_unload();
        }
    }
}

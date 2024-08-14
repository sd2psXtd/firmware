#include "keystore.h"
#if WITH_GUI
#include "gui.h"
#include "input.h"
#include "oled.h"
#endif
#include "settings.h"
#include "card_emu/ps2_mc_data_interface.h"
#include "card_emu/ps2_sd2psxman.h"
#include "card_emu/ps2_memory_card.h"
#include "ps2_dirty.h"
#include "history_tracker/ps2_history_tracker.h"
#include "mmce_fs/ps2_mmce_fs.h"
#include "ps2_cardman.h"

#include <stdio.h>

void ps2_switch_card(void) {
    ps2_memory_card_exit();
    ps2_cardman_close();
#if WITH_GUI
    gui_do_ps2_card_switch();
#else
    ps2_cardman_open();
    ps2_memory_card_enter();
#endif
}

void ps2_init(void) {
    printf("starting in PS2 mode\n");

    keystore_init();
    ps2_mc_data_interface_card_changed();
    
    ps2_cardman_init();

    if (!settings_get_sd_mode()) {
#if WITH_PSRAM
        ps2_dirty_init();
#endif
    }
    ps2_history_tracker_init();

    multicore_launch_core1(ps2_memory_card_main);
#if WITH_GUI
    gui_init();
#endif
    ps2_mmce_fs_init();

    printf("Starting memory card... ");
    uint64_t start = time_us_64();
#if WITH_GUI
    gui_do_ps2_card_switch();
#else
    ps2_cardman_open();
    ps2_memory_card_enter();
#endif
    uint64_t end = time_us_64();
    printf("DONE! (%d us)\n", (int)(end - start));
}

bool ps2_task(void) {
    ps2_sd2psxman_task();
    ps2_cardman_task();
#if WITH_GUI
    gui_task();
    input_task();
    oled_task();
#endif
    ps2_mmce_fs_run();

    if (ps2_cardman_is_accessible()) 
        ps2_mc_data_interface_task();
    else
        ps2_history_tracker_task();

    if ((settings_get_mode() == MODE_PS1) && (ps2_cardman_is_accessible()))
        return false;

    return true;
}

void ps2_deinit(void) {

    ps2_memory_card_exit();
    while (ps2_mc_data_interface_write_occured())
        ps2_mc_data_interface_task();
    multicore_reset_core1();
    ps2_cardman_close();
    ps2_memory_card_unload();
}
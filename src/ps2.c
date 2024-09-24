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
#include "debug.h"

#include <stdio.h>

#if LOG_LEVEL_PS2_MAIN == 0
#define log(x...)
#else
#define log(level, fmt, x...) LOG_PRINT(LOG_LEVEL_PS2_MAIN, level, fmt, ##x)
#endif

void ps2_init(void) {
    log(LOG_INFO, "starting in PS2 mode\n");

    keystore_init();
    
    multicore_launch_core1(ps2_memory_card_main);

    ps2_history_tracker_init();

    ps2_memory_card_enter();

    ps2_mc_data_interface_init();
    
    ps2_cardman_init();

    log(LOG_INFO, "Starting memory card... ");
    ps2_cardman_open();

    ps2_mmce_fs_init();

    uint64_t start = time_us_64();

#if WITH_GUI
    gui_init();

    gui_do_ps2_card_switch();
#endif
    uint64_t end = time_us_64();
    log(LOG_INFO, "DONE! (%d us)\n", (int)(end - start));
}

bool ps2_task(void) {
    ps2_sd2psxman_task();
    ps2_cardman_task();
#if WITH_GUI
    gui_task();
    input_task();
    oled_task();
#endif
    log(LOG_TRACE, "%s after GUI\n", __func__);
    ps2_mmce_fs_run();
    log(LOG_TRACE, "%s mmcefs\n", __func__);

    if (ps2_cardman_is_accessible()) {
        ps2_history_tracker_task();
        ps2_mc_data_interface_task();
    }

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
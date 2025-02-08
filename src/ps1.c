
#include <input.h>
#include <settings.h>
#include <stdio.h>
#include "led.h"
#include "pico/multicore.h"
#include "ps1_mc_data_interface.h"

#if WITH_GUI
#include "gui.h"
#include "oled.h"
#endif
#include "ps1.h"
#include "ps1_cardman.h"
#include "ps1_dirty.h"
#include "ps1_memory_card.h"
#include "ps1_odeman.h"


void ps1_init() {
    printf("starting in PS1 mode\n");

    ps1_cardman_init();
    ps1_dirty_init();

#if WITH_GUI
    gui_init();
#endif

    multicore_launch_core1(ps1_memory_card_main);

    printf("Starting memory card... ");
    uint64_t start = time_us_64();
#if WITH_GUI
    gui_do_ps1_card_switch();
#else
    ps1_cardman_open();
    ps1_memory_card_enter();
#endif
    uint64_t end = time_us_64();
    printf("DONE! (%d us)\n", (int)(end - start));
}

bool ps1_task() {
    ps1_odeman_task();
    ps1_dirty_task();

#if WITH_GUI
    gui_task();
    input_task();
    oled_task();
#endif
#if WITH_LED
    led_task();
#endif
    ps1_mc_data_interface_task();
    if ((settings_get_mode() == MODE_PS2))
        return false;

    return true;
}

void ps1_deinit(void) {

    ps1_memory_card_exit();

    while(ps1_dirty_activity)
        ps1_dirty_task();
    ps1_cardman_close();
    multicore_reset_core1();
    ps1_memory_card_unload();
}
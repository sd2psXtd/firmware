#include "ps2/ps2_sd2psxman.h"

#include <stdio.h>

#include "debug.h"
#include "gui.h"
#include "ps2/ps2_memory_card.h"
#include "ps2/ps2_cardman.h"

#include "pico/platform.h"

volatile uint8_t sd2psxman_cmd;
volatile uint8_t sd2psxman_mode;
volatile uint16_t sd2psxman_cnum;
char sd2psxman_gameid[251] = {0x00};

void ps2_sd2psxman_task(void) {
    if (sd2psxman_cmd != 0) {
        uint16_t prev_card = ps2_cardman_get_idx();
        uint8_t prev_chan = ps2_cardman_get_channel();
        ps2_cardman_state_t prev_state = ps2_cardman_get_state();

        switch (sd2psxman_cmd) {
            

            case SD2PSXMAN_SET_GAMEID: 
                    ps2_cardman_set_gameid(sd2psxman_gameid); 
                    debug_printf("set next channel\n");
                break;

            default: break;
        }

        if (prev_card != ps2_cardman_get_idx() || prev_chan != ps2_cardman_get_channel() || (prev_state != ps2_cardman_get_state()) ||
            (SD2PSXMAN_SET_GAMEID == sd2psxman_cmd)) {
            // close old card
            ps2_memory_card_exit();
            ps2_cardman_close();

            // open new card
            gui_do_ps2_card_switch();
            // ps2_memory_card_enter();
            gui_request_refresh();
        }

        sd2psxman_cmd = 0;
    }
}

void __time_critical_func(ps2_sd2psxman_set_gameid)(const char* const game_id) {
    snprintf(sd2psxman_gameid, sizeof(sd2psxman_gameid), "%s", game_id);
}

const char* ps2_sd2psxman_get_gameid(void) {
    return sd2psxman_gameid;
}
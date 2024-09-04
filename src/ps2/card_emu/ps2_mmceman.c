#include "ps2/card_emu/ps2_mmceman.h"

#include <stdio.h>

#include "debug.h"
#include "gui.h"
#include "ps2/card_emu/ps2_memory_card.h"
#include "ps2/card_emu/ps2_mmceman_commands.h"
#include "ps2/ps2_cardman.h"

#include "pico/platform.h"
#include "ps2_dirty.h"

volatile uint8_t mmceman_cmd;
volatile uint8_t mmceman_mode;
volatile uint16_t mmceman_cnum;
char mmceman_gameid[251] = {0x00};

void ps2_mmceman_task(void) {
    if ((mmceman_cmd != 0) && (!ps2_dirty_activity)) {

        switch (mmceman_cmd) {
            case MMCEMAN_CMD_SET_CARD:
                if (mmceman_mode == MMCEMAN_MODE_NUM) {
                    ps2_cardman_set_idx(mmceman_cnum);
                    debug_printf("set num idx\n");
                } else if (mmceman_mode == MMCEMAN_MODE_NEXT) {
                    ps2_cardman_next_idx();
                    debug_printf("set next idx\n");
                } else if (mmceman_mode == MMCEMAN_MODE_PREV) {
                    ps2_cardman_prev_idx();
                    debug_printf("set prev idx\n");
                }
                break;

            case MMCEMAN_CMD_SET_CHANNEL:
                if (mmceman_mode == MMCEMAN_MODE_NUM) {
                    ps2_cardman_set_channel(mmceman_cnum);
                    debug_printf("set num channel\n");
                } else if (mmceman_mode == MMCEMAN_MODE_NEXT) {
                    ps2_cardman_next_channel();
                    debug_printf("set next channel\n");
                } else if (mmceman_mode == MMCEMAN_MODE_PREV) {
                    ps2_cardman_prev_channel();
                    debug_printf("set prev channel\n");
                }
                break;

            case MMCEMAN_CMD_SET_GAMEID: 
                    ps2_cardman_set_gameid(mmceman_gameid); 
                    debug_printf("set next channel\n");
                break;
            case SD2PSXMAN_UNMOUNT_BOOTCARD:
                if (ps2_cardman_get_idx() == 0) {
                    ps2_cardman_next_idx();
                }
                break;

            default: break;
        }

        if (ps2_cardman_needs_update()) {
            // close old card
            ps2_memory_card_exit();
            ps2_cardman_close();

            // open new card
            gui_do_ps2_card_switch();
            gui_request_refresh();
        }

        mmceman_cmd = 0;
    }
}

void __time_critical_func(ps2_mmceman_set_gameid)(const char* const game_id) {
    snprintf(mmceman_gameid, sizeof(mmceman_gameid), "%s", game_id);
}

const char* ps2_mmceman_get_gameid(void) {
    return mmceman_gameid;
}